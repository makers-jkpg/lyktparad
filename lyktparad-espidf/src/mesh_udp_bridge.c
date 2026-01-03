/* Mesh UDP Bridge Module Implementation
 *
 * This module provides UDP communication bridge between the ESP32 root node
 * and an optional external web server. The bridge handles root node registration
 * and forwards mesh commands to the external server for monitoring purposes.
 *
 * Copyright (c) 2025 the_louie
 */

#include "mesh_udp_bridge.h"
#include "mesh_common.h"
#include "mesh_version.h"
#include "config/mesh_config.h"
#include "node_sequence.h"
#include "mesh_ota.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

static const char *TAG = "mesh_udp_bridge";

/* External server registration state */
static bool s_server_registered = false;
static struct sockaddr_in s_server_addr = {0};
static int s_udp_socket = -1;

/* Registration status tracking */
static bool s_registration_complete = false;

/* NVS namespace for cached server address */
#define NVS_NAMESPACE_UDP_BRIDGE "udp_bridge"

/*******************************************************
 *                UDP Socket Management
 *******************************************************/

/**
 * @brief Initialize UDP socket for communication.
 *
 * Creates and configures a non-blocking UDP socket.
 *
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t init_udp_socket(void)
{
    if (s_udp_socket >= 0) {
        /* Socket already initialized */
        return ESP_OK;
    }

    /* Create UDP socket */
    s_udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: %d", errno);
        return ESP_FAIL;
    }

    /* Set socket to non-blocking */
    int flags = fcntl(s_udp_socket, F_GETFL, 0);
    if (flags < 0) {
        ESP_LOGE(TAG, "Failed to get socket flags: %d", errno);
        close(s_udp_socket);
        s_udp_socket = -1;
        return ESP_FAIL;
    }
    if (fcntl(s_udp_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGE(TAG, "Failed to set socket non-blocking: %d", errno);
        close(s_udp_socket);
        s_udp_socket = -1;
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "UDP socket initialized");
    return ESP_OK;
}

/*******************************************************
 *                Data Gathering Functions
 *******************************************************/

/**
 * @brief Get root node IP address.
 *
 * @param ip_out Output buffer for IP address (4 bytes, network byte order)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t mesh_udp_bridge_get_root_ip(uint8_t *ip_out)
{
    if (ip_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif_sta = mesh_common_get_netif_sta();
    if (netif_sta == NULL) {
        ESP_LOGE(TAG, "STA netif is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(netif_sta, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP info: %s", esp_err_to_name(err));
        return err;
    }

    /* IP address is already in network byte order */
    memcpy(ip_out, &ip_info.ip.addr, 4);
    return ESP_OK;
}

/**
 * @brief Get mesh ID.
 *
 * @param mesh_id_out Output buffer for mesh ID (6 bytes)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t mesh_udp_bridge_get_mesh_id(uint8_t *mesh_id_out)
{
    if (mesh_id_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *mesh_id = mesh_common_get_mesh_id();
    if (mesh_id == NULL) {
        ESP_LOGE(TAG, "Mesh ID is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(mesh_id_out, mesh_id, 6);
    return ESP_OK;
}

/**
 * @brief Get node count.
 *
 * @return Number of connected child nodes (0-255)
 */
static uint8_t mesh_udp_bridge_get_node_count(void)
{
    if (!esp_mesh_is_root()) {
        return 0;
    }

    int route_table_size = esp_mesh_get_routing_table_size();
    /* Subtract 1 to exclude root node */
    if (route_table_size > 0) {
        return (uint8_t)(route_table_size - 1);
    }
    return 0;
}

/**
 * @brief Get firmware version string.
 *
 * @param version_out Output buffer for version string
 * @param max_len Maximum length of output buffer
 * @return Length of version string on success, 0 on failure
 */
static size_t mesh_udp_bridge_get_firmware_version(char *version_out, size_t max_len)
{
    if (version_out == NULL || max_len == 0) {
        return 0;
    }

    const char *version = mesh_version_get_string();
    if (version == NULL) {
        ESP_LOGE(TAG, "Version string is NULL");
        return 0;
    }

    size_t version_len = strlen(version);
    if (version_len >= max_len) {
        version_len = max_len - 1;
    }

    memcpy(version_out, version, version_len);
    version_out[version_len] = '\0';
    return version_len;
}

/**
 * @brief Get current timestamp.
 *
 * @return Unix timestamp in network byte order
 */
static uint32_t mesh_udp_bridge_get_timestamp(void)
{
    time_t now = time(NULL);
    if (now < 0) {
        ESP_LOGW(TAG, "Failed to get time, using 0");
        return 0;
    }
    return htonl((uint32_t)now);
}

/*******************************************************
 *                Payload Construction
 *******************************************************/

/**
 * @brief Build registration payload.
 *
 * @param payload Output payload structure
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t mesh_udp_bridge_build_registration_payload(mesh_registration_payload_t *payload)
{
    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(payload, 0, sizeof(mesh_registration_payload_t));

    /* Get root IP address */
    esp_err_t err = mesh_udp_bridge_get_root_ip(payload->root_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get root IP: %s", esp_err_to_name(err));
        return err;
    }

    /* Validate IP address is not 0.0.0.0 */
    if (payload->root_ip[0] == 0 && payload->root_ip[1] == 0 &&
        payload->root_ip[2] == 0 && payload->root_ip[3] == 0) {
        ESP_LOGE(TAG, "Root IP address is 0.0.0.0");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get mesh ID */
    err = mesh_udp_bridge_get_mesh_id(payload->mesh_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get mesh ID: %s", esp_err_to_name(err));
        return err;
    }

    /* Validate mesh ID is not all zeros */
    bool all_zeros = true;
    for (int i = 0; i < 6; i++) {
        if (payload->mesh_id[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    if (all_zeros) {
        ESP_LOGE(TAG, "Mesh ID is all zeros");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get node count */
    payload->node_count = mesh_udp_bridge_get_node_count();

    /* Get firmware version */
    size_t version_len = mesh_udp_bridge_get_firmware_version(payload->firmware_version,
                                                              sizeof(payload->firmware_version));
    if (version_len == 0) {
        ESP_LOGE(TAG, "Failed to get firmware version");
        return ESP_ERR_INVALID_STATE;
    }
    payload->firmware_version_len = (uint8_t)version_len;

    /* Validate version length */
    if (payload->firmware_version_len == 0 || payload->firmware_version_len >= sizeof(payload->firmware_version)) {
        ESP_LOGE(TAG, "Invalid version length: %d", payload->firmware_version_len);
        return ESP_ERR_INVALID_STATE;
    }

    /* Get timestamp */
    payload->timestamp = mesh_udp_bridge_get_timestamp();

    return ESP_OK;
}

/*******************************************************
 *                ACK Reception
 *******************************************************/

/**
 * @brief Wait for registration ACK with timeout.
 *
 * @param timeout_ms Timeout in milliseconds
 * @param success Output pointer for success status
 * @return ESP_OK if ACK received, ESP_ERR_TIMEOUT if timeout, error on failure
 */
static esp_err_t mesh_udp_bridge_wait_for_registration_ack(uint32_t timeout_ms, bool *success)
{
    if (success == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_udp_socket < 0) {
        ESP_LOGE(TAG, "UDP socket not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Temporarily set socket to blocking mode for ACK reception with timeout */
    int flags = fcntl(s_udp_socket, F_GETFL, 0);
    if (flags < 0) {
        ESP_LOGE(TAG, "Failed to get socket flags: %d", errno);
        return ESP_FAIL;
    }
    if (fcntl(s_udp_socket, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        ESP_LOGE(TAG, "Failed to set socket blocking: %d", errno);
        return ESP_FAIL;
    }

    /* Set receive timeout */
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(s_udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket timeout: %d", errno);
        /* Restore non-blocking mode before returning */
        fcntl(s_udp_socket, F_SETFL, flags);
        return ESP_FAIL;
    }

    /* Receive ACK packet */
    uint8_t ack_buffer[64];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(s_udp_socket, ack_buffer, sizeof(ack_buffer), 0,
                                 (struct sockaddr *)&from_addr, &from_len);

    /* Restore non-blocking mode */
    if (fcntl(s_udp_socket, F_SETFL, flags) < 0) {
        ESP_LOGW(TAG, "Failed to restore socket non-blocking mode: %d", errno);
        /* Continue anyway - socket will remain blocking but this is not critical */
    }

    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
            ESP_LOGD(TAG, "ACK timeout");
            return ESP_ERR_TIMEOUT;
        }
        ESP_LOGE(TAG, "Failed to receive ACK: %d", errno);
        return ESP_FAIL;
    }

    /* Parse ACK packet: [CMD:0xE3][LEN:2][STATUS:1][CHKSUM:2] (6 bytes total) */
    if (received < 6) {
        ESP_LOGE(TAG, "ACK packet too short: %d bytes (expected 6)", received);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (ack_buffer[0] != UDP_CMD_REGISTRATION_ACK) {
        ESP_LOGD(TAG, "Received non-ACK packet (command: 0x%02x)", ack_buffer[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Verify length field (should be 1 for STATUS byte) */
    uint16_t payload_len = (ack_buffer[1] << 8) | ack_buffer[2];
    if (payload_len != 1) {
        ESP_LOGE(TAG, "Invalid ACK payload length: %d (expected 1)", payload_len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Extract status byte (0=success, 1=failure) */
    uint8_t status = ack_buffer[3];
    *success = (status == 0);

    if (*success) {
        ESP_LOGI(TAG, "Registration ACK received: success");
    } else {
        ESP_LOGW(TAG, "Registration ACK received: failure (status: %d)", status);
    }

    return ESP_OK;
}

/*******************************************************
 *                Registration Function
 *******************************************************/

esp_err_t mesh_udp_bridge_register(void)
{
    /* Check if external server is discovered (via set_registration or cached) */
    if (!s_server_registered) {
        /* Try to use cached server address as fallback */
        char cached_ip[16] = {0};
        uint16_t cached_port = 0;
        esp_err_t cache_err = mesh_udp_bridge_get_cached_server(cached_ip, &cached_port);
        if (cache_err == ESP_OK) {
            /* Convert cached IP to network byte order and set registration */
            struct in_addr addr;
            if (inet_aton(cached_ip, &addr) != 0) {
                uint8_t ip_bytes[4];
                memcpy(ip_bytes, &addr.s_addr, 4);
                mesh_udp_bridge_set_registration(true, ip_bytes, cached_port);
                ESP_LOGI(TAG, "Using cached server address for registration: %s:%d", cached_ip, cached_port);
            } else {
                ESP_LOGD(TAG, "External server not discovered and cached IP invalid, skipping registration");
                return ESP_ERR_NOT_FOUND;
            }
        } else {
            ESP_LOGD(TAG, "External server not discovered, skipping registration");
            return ESP_ERR_NOT_FOUND;
        }
    }

    /* Only root node can register */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "Not root node, skipping registration");
        return ESP_ERR_INVALID_STATE;
    }

    /* Initialize UDP socket if needed */
    esp_err_t err = init_udp_socket();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UDP socket: %s", esp_err_to_name(err));
        return err;
    }

    /* Build registration payload */
    mesh_registration_payload_t payload;
    err = mesh_udp_bridge_build_registration_payload(&payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build registration payload: %s", esp_err_to_name(err));
        return err;
    }

    /* Calculate payload size: root_ip(4) + mesh_id(6) + node_count(1) + version_len(1) + version(N) + timestamp(4) */
    size_t payload_size = 4 + 6 + 1 + 1 + payload.firmware_version_len + 4;
    /* Validate payload size doesn't exceed maximum (structure size with full version string) */
    size_t max_payload_size = 4 + 6 + 1 + 1 + (sizeof(payload.firmware_version) - 1) + 4; /* -1 to exclude null terminator from max */
    if (payload_size > max_payload_size) {
        ESP_LOGE(TAG, "Payload size exceeds maximum: %d > %d", payload_size, max_payload_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Construct UDP packet: [CMD:0xE0][LEN:2][PAYLOAD:N][CHKSUM:2] */
    size_t packet_size = 1 + 2 + payload_size + 2;
    uint8_t *packet = (uint8_t *)malloc(packet_size);
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate packet buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Fill packet */
    packet[0] = UDP_CMD_REGISTRATION;
    packet[1] = (payload_size >> 8) & 0xff;
    packet[2] = payload_size & 0xff;
    memcpy(&packet[3], &payload, payload_size);

    /* Calculate checksum (simple 16-bit sum of all bytes except checksum) */
    uint16_t checksum = 0;
    for (size_t i = 0; i < packet_size - 2; i++) {
        checksum += packet[i];
    }
    packet[packet_size - 2] = (checksum >> 8) & 0xff;
    packet[packet_size - 1] = checksum & 0xff;

    /* Retry logic: 3 attempts with exponential backoff */
    const int max_retries = 3;
    const uint32_t backoff_delays[] = {1000, 2000, 4000}; /* 1s, 2s, 4s */
    bool registration_success = false;

    for (int attempt = 0; attempt < max_retries; attempt++) {
        if (attempt > 0) {
            ESP_LOGI(TAG, "Registration retry attempt %d/%d (backoff: %lu ms)",
                     attempt, max_retries, (unsigned long)backoff_delays[attempt - 1]);
            vTaskDelay(pdMS_TO_TICKS(backoff_delays[attempt - 1]));
        }

        /* Send UDP packet */
        ssize_t sent = sendto(s_udp_socket, packet, packet_size, 0,
                              (struct sockaddr *)&s_server_addr, sizeof(s_server_addr));
        if (sent < 0) {
            ESP_LOGW(TAG, "Failed to send registration packet (attempt %d): %d", attempt + 1, errno);
            continue;
        }

        if (sent != (ssize_t)packet_size) {
            ESP_LOGW(TAG, "Partial send: %d/%d bytes (attempt %d)", sent, packet_size, attempt + 1);
            continue;
        }

        ESP_LOGI(TAG, "Registration packet sent (attempt %d/%d)", attempt + 1, max_retries);

        /* Wait for ACK with 5 second timeout */
        bool ack_success = false;
        err = mesh_udp_bridge_wait_for_registration_ack(5000, &ack_success);
        if (err == ESP_OK && ack_success) {
            registration_success = true;
            s_registration_complete = true;
            ESP_LOGI(TAG, "Registration successful");
            /* Start heartbeat and state updates after successful registration */
            mesh_udp_bridge_start_heartbeat();
            mesh_udp_bridge_start_state_updates();
            break;
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Registration ACK timeout (attempt %d/%d)", attempt + 1, max_retries);
        } else {
            ESP_LOGW(TAG, "Registration ACK error: %s (attempt %d/%d)",
                     esp_err_to_name(err), attempt + 1, max_retries);
        }
    }

    free(packet);

    if (!registration_success) {
        ESP_LOGW(TAG, "Registration failed after %d attempts", max_retries);
        s_registration_complete = false;
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/*******************************************************
 *                Registration State Management
 *******************************************************/

bool mesh_udp_bridge_is_registered(void)
{
    return s_server_registered && s_registration_complete;
}

bool mesh_udp_bridge_is_server_discovered(void)
{
    return s_server_registered;
}

void mesh_udp_bridge_set_registration(bool registered, const uint8_t *server_ip, uint16_t server_port)
{
    s_server_registered = registered;

    if (registered && server_ip != NULL) {
        /* Set server address */
        memset(&s_server_addr, 0, sizeof(s_server_addr));
        s_server_addr.sin_family = AF_INET;
        s_server_addr.sin_port = htons(server_port);
        memcpy(&s_server_addr.sin_addr.s_addr, server_ip, 4);
        ESP_LOGI(TAG, "External server registered: %d.%d.%d.%d:%d",
                 server_ip[0], server_ip[1], server_ip[2], server_ip[3], server_port);
        /* Clear registration status when server changes */
        s_registration_complete = false;
    } else {
        /* Clear server address */
        memset(&s_server_addr, 0, sizeof(s_server_addr));
        ESP_LOGI(TAG, "External server registration cleared");
        s_registration_complete = false;
        /* Stop heartbeat and state updates when registration is cleared */
        mesh_udp_bridge_stop_heartbeat();
        mesh_udp_bridge_stop_state_updates();
    }
}

/*******************************************************
 *                Cached Server Address
 *******************************************************/

esp_err_t mesh_udp_bridge_get_cached_server(char *server_ip, uint16_t *server_port)
{
    if (server_ip == NULL || server_port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    /* Read server IP */
    size_t required_size = 16;
    err = nvs_get_str(nvs_handle, "server_ip", server_ip, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        return err;
    }

    /* Read server port */
    err = nvs_get_u16(nvs_handle, "server_port", server_port);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        return err;
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

/*******************************************************
 *                Heartbeat Implementation
 *******************************************************/

/* Heartbeat task state */
static TaskHandle_t s_heartbeat_task_handle = NULL;
static bool s_heartbeat_running = false;

/* Heartbeat interval (default 45 seconds) */
#define HEARTBEAT_INTERVAL_SECONDS  45

/**
 * @brief Build heartbeat payload.
 *
 * @param payload Output buffer for payload
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t mesh_udp_bridge_build_heartbeat_payload(mesh_heartbeat_payload_t *payload)
{
    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Get current timestamp */
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        /* Fallback to esp_timer if time() fails */
        int64_t us = esp_timer_get_time();
        now = (time_t)(us / 1000000);
    }
    payload->timestamp = htonl((uint32_t)now);

    /* Get node count */
    payload->node_count = mesh_udp_bridge_get_node_count();

    return ESP_OK;
}

/**
 * @brief Send a single heartbeat UDP packet.
 *
 * This function sends a heartbeat packet to the external web server (if registered).
 * Heartbeat is fire-and-forget (no ACK required) and completely optional.
 *
 * @return ESP_OK on send attempt (even if send fails), ESP_ERR_INVALID_STATE if not registered or not root
 */
esp_err_t mesh_udp_bridge_send_heartbeat(void)
{
    /* Check if registered */
    if (!mesh_udp_bridge_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if root node */
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Build heartbeat payload */
    mesh_heartbeat_payload_t payload;
    esp_err_t err = mesh_udp_bridge_build_heartbeat_payload(&payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build heartbeat payload: %s", esp_err_to_name(err));
        return err;
    }

    /* Initialize UDP socket if needed */
    err = init_udp_socket();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UDP socket: %s", esp_err_to_name(err));
        return err;
    }

    /* Construct UDP packet: [CMD:0xE1][LEN:2][PAYLOAD:5 bytes] */
    size_t payload_size = sizeof(mesh_heartbeat_payload_t); /* 5 bytes */
    size_t packet_size = 1 + 2 + payload_size; /* CMD + LEN + PAYLOAD */
    uint8_t *packet = (uint8_t *)malloc(packet_size);
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate packet buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Pack packet */
    packet[0] = UDP_CMD_HEARTBEAT;
    packet[1] = (payload_size >> 8) & 0xFF; /* Length MSB */
    packet[2] = payload_size & 0xFF; /* Length LSB */
    memcpy(&packet[3], &payload, payload_size);

    /* Send UDP packet (fire-and-forget, no ACK wait) */
    ssize_t sent = sendto(s_udp_socket, packet, packet_size, 0,
                          (struct sockaddr *)&s_server_addr, sizeof(s_server_addr));
    if (sent < 0) {
        /* Log at debug level - packet loss is acceptable for heartbeat */
        ESP_LOGD(TAG, "Heartbeat send failed: %d (acceptable for fire-and-forget)", errno);
    } else {
        ESP_LOGD(TAG, "Heartbeat sent: timestamp=%lu, node_count=%d",
                 (unsigned long)ntohl(payload.timestamp), payload.node_count);
    }

    free(packet);
    return ESP_OK; /* Return OK even if send failed (fire-and-forget) */
}

/**
 * @brief Heartbeat task function.
 *
 * This task periodically sends heartbeat packets to the external web server.
 * The task exits if the node is no longer root or registration is lost.
 */
static void mesh_udp_bridge_heartbeat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Heartbeat task started");

    while (1) {
        /* Check if still root and registered */
        if (!esp_mesh_is_root() || !mesh_udp_bridge_is_registered()) {
            ESP_LOGI(TAG, "Heartbeat task exiting: not root or not registered");
            break;
        }

        /* Send heartbeat */
        esp_err_t err = mesh_udp_bridge_send_heartbeat();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            /* Log at debug level - heartbeat failures are acceptable */
            ESP_LOGD(TAG, "Heartbeat send error: %s (continuing)", esp_err_to_name(err));
        }

        /* Sleep for interval */
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_SECONDS * 1000));
    }

    /* Clean up on exit */
    s_heartbeat_task_handle = NULL;
    s_heartbeat_running = false;
    ESP_LOGI(TAG, "Heartbeat task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief Start the heartbeat task.
 *
 * Starts a periodic FreeRTOS task that sends heartbeat packets at regular intervals.
 * Only starts if the node is root and registered with an external server.
 * Heartbeat is completely optional and does not affect embedded web server operation.
 */
void mesh_udp_bridge_start_heartbeat(void)
{
    /* Check if task already running */
    if (s_heartbeat_running && s_heartbeat_task_handle != NULL) {
        ESP_LOGD(TAG, "Heartbeat task already running");
        return;
    }

    /* Check if registered */
    if (!mesh_udp_bridge_is_registered()) {
        ESP_LOGD(TAG, "Not registered, skipping heartbeat start");
        return;
    }

    /* Check if root node */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "Not root node, skipping heartbeat start");
        return;
    }

    /* Create FreeRTOS task */
    BaseType_t task_err = xTaskCreate(mesh_udp_bridge_heartbeat_task, "udp_heartbeat",
                                       2048, NULL, 1, &s_heartbeat_task_handle);
    if (task_err != pdPASS) {
        ESP_LOGW(TAG, "Failed to create heartbeat task");
        s_heartbeat_task_handle = NULL;
        s_heartbeat_running = false;
    } else {
        s_heartbeat_running = true;
        ESP_LOGI(TAG, "Heartbeat task started");
    }
}

/**
 * @brief Stop the heartbeat task.
 *
 * Stops the periodic heartbeat task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_stop_heartbeat(void)
{
    if (!s_heartbeat_running || s_heartbeat_task_handle == NULL) {
        /* Task not running, nothing to stop */
        return;
    }

    /* Delete task */
    vTaskDelete(s_heartbeat_task_handle);
    s_heartbeat_task_handle = NULL;
    s_heartbeat_running = false;
    ESP_LOGI(TAG, "Heartbeat task stopped");
}

/*******************************************************
 *                Mesh Command Forwarding
 *******************************************************/

/**
 * @brief Forward a mesh command to external server via UDP (non-blocking).
 *
 * This function forwards a mesh command to the external web server for monitoring purposes.
 * The forwarding is completely optional and non-blocking. If the external server is not
 * registered or forwarding fails, mesh operations continue normally.
 *
 * Packet format: [CMD:0xE6][LEN:2][PAYLOAD:N][CHKSUM:2]
 * Payload format: [mesh_cmd_id:1][mesh_payload_len:2][mesh_payload:N][timestamp:4]
 *
 * @param mesh_cmd Mesh command ID (first byte of mesh data)
 * @param mesh_payload Mesh command payload (data after command ID)
 * @param mesh_payload_len Length of mesh payload in bytes
 */
void mesh_udp_bridge_forward_mesh_command_async(uint8_t mesh_cmd,
                                                   const void *mesh_payload,
                                                   size_t mesh_payload_len)
{
    /* Check if external server is registered */
    if (!mesh_udp_bridge_is_registered()) {
        /* Not registered - don't forward (normal case, no logging) */
        return;
    }

    /* Initialize UDP socket if needed */
    esp_err_t err = init_udp_socket();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to initialize UDP socket for forwarding: %s", esp_err_to_name(err));
        return;
    }

    /* Calculate UDP payload size: mesh_cmd (1) + payload_len (2) + payload (N) + timestamp (4) */
    size_t udp_payload_size = 1 + 2 + mesh_payload_len + 4;

    /* Calculate total packet size: CMD (1) + LEN (2) + PAYLOAD (N) + CHKSUM (2) */
    size_t packet_size = 1 + 2 + udp_payload_size + 2;

    /* Check UDP MTU limit (1472 bytes) */
    if (packet_size > 1472) {
        ESP_LOGW(TAG, "Mesh command forward packet too large: %zu bytes (max 1472), skipping forward", packet_size);
        return;
    }

    /* Allocate buffer for UDP payload */
    uint8_t *udp_payload = (uint8_t *)malloc(udp_payload_size);
    if (udp_payload == NULL) {
        ESP_LOGD(TAG, "Failed to allocate UDP payload buffer for forwarding");
        return;
    }

    /* Pack UDP payload: [mesh_cmd:1][mesh_payload_len:2][mesh_payload:N][timestamp:4] */
    udp_payload[0] = mesh_cmd;
    udp_payload[1] = (mesh_payload_len >> 8) & 0xFF; /* Payload length MSB */
    udp_payload[2] = mesh_payload_len & 0xFF; /* Payload length LSB */
    if (mesh_payload_len > 0 && mesh_payload != NULL) {
        memcpy(&udp_payload[3], mesh_payload, mesh_payload_len);
    }
    /* Add timestamp (network byte order) */
    uint32_t timestamp = mesh_udp_bridge_get_timestamp();
    memcpy(&udp_payload[3 + mesh_payload_len], &timestamp, 4);

    /* Allocate buffer for complete UDP packet */
    uint8_t *packet = (uint8_t *)malloc(packet_size);
    if (packet == NULL) {
        free(udp_payload);
        ESP_LOGD(TAG, "Failed to allocate UDP packet buffer for forwarding");
        return;
    }

    /* Pack UDP packet: [CMD:0xE6][LEN:2][PAYLOAD:N][CHKSUM:2] */
    packet[0] = UDP_CMD_MESH_COMMAND_FORWARD;
    packet[1] = (udp_payload_size >> 8) & 0xFF; /* Length MSB */
    packet[2] = udp_payload_size & 0xFF; /* Length LSB */
    memcpy(&packet[3], udp_payload, udp_payload_size);

    /* Calculate checksum (16-bit sum of all bytes excluding checksum) */
    uint16_t checksum = 0;
    for (size_t i = 0; i < packet_size - 2; i++) {
        checksum = (checksum + packet[i]) & 0xFFFF;
    }
    packet[packet_size - 2] = (checksum >> 8) & 0xFF; /* Checksum MSB */
    packet[packet_size - 1] = checksum & 0xFF; /* Checksum LSB */

    /* Send UDP packet (non-blocking, fire-and-forget) */
    ssize_t sent = sendto(s_udp_socket, packet, packet_size, 0,
                          (struct sockaddr *)&s_server_addr, sizeof(s_server_addr));
    if (sent < 0) {
        /* Log at debug level - packet loss is acceptable for forwarded commands */
        ESP_LOGD(TAG, "Mesh command forward send failed: %d (acceptable for fire-and-forget)", errno);
    } else {
        ESP_LOGD(TAG, "Mesh command forwarded: cmd=0x%02X, payload_len=%zu", mesh_cmd, mesh_payload_len);
    }

    /* Free allocated buffers */
    free(udp_payload);
    free(packet);
}

/*******************************************************
 *                State Update Implementation
 *******************************************************/

/* State update task state */
static TaskHandle_t s_state_update_task_handle = NULL;
static bool s_state_update_running = false;

/* State update interval (default 3 seconds) */
#define STATE_UPDATE_INTERVAL_SECONDS  3

/* Maximum number of nodes in state update (safety limit) */
#define MAX_STATE_UPDATE_NODES  50

/**
 * @brief Collect mesh state information.
 *
 * Collects complete mesh network state including nodes, sequence state, and OTA state.
 * This function allocates memory for the node list which must be freed by the caller.
 *
 * @param state Output structure to fill with state data
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_collect_state(mesh_state_data_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Initialize state structure */
    memset(state, 0, sizeof(mesh_state_data_t));
    state->nodes = NULL;

    /* Check if root node */
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot collect state");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get root IP address */
    esp_err_t err = mesh_udp_bridge_get_root_ip(state->root_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get root IP: %s", esp_err_to_name(err));
        return err;
    }

    /* Get mesh ID */
    err = mesh_udp_bridge_get_mesh_id(state->mesh_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get mesh ID: %s", esp_err_to_name(err));
        return err;
    }

    /* Get timestamp */
    state->timestamp = mesh_udp_bridge_get_timestamp();

    /* Get mesh state (connected/disconnected) */
    state->mesh_state = mesh_common_is_connected() ? 1 : 0;

    /* Get routing table */
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *)route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate child node count (excluding root) */
    mesh_addr_t root_addr;
    uint8_t mac[6];
    esp_err_t mac_err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (mac_err == ESP_OK) {
        memcpy(root_addr.addr, mac, 6);
    } else {
        /* Fallback: use first entry in routing table */
        if (route_table_size > 0) {
            root_addr = route_table[0];
        } else {
            root_addr.addr[0] = 0;
        }
    }

    /* Count child nodes (excluding root) */
    int child_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        bool is_root = true;
        for (int j = 0; j < 6; j++) {
            if (route_table[i].addr[j] != root_addr.addr[j]) {
                is_root = false;
                break;
            }
        }
        if (!is_root) {
            child_count++;
        }
    }

    state->node_count = (uint8_t)child_count;

    /* Allocate node list */
    if (child_count > 0 && child_count <= MAX_STATE_UPDATE_NODES) {
        state->nodes = (mesh_node_entry_t *)malloc(child_count * sizeof(mesh_node_entry_t));
        if (state->nodes == NULL) {
            ESP_LOGE(TAG, "Failed to allocate node list");
            return ESP_ERR_NO_MEM;
        }
        memset(state->nodes, 0, child_count * sizeof(mesh_node_entry_t));

        /* Fill node list */
        int node_idx = 0;
        for (int i = 0; i < route_table_size; i++) {
            /* Skip root node */
            bool is_root = true;
            for (int j = 0; j < 6; j++) {
                if (route_table[i].addr[j] != root_addr.addr[j]) {
                    is_root = false;
                    break;
                }
            }
            if (is_root) {
                continue;
            }

            /* Get node information */
            mesh_node_entry_t *node = &state->nodes[node_idx];
            memcpy(node->node_id, route_table[i].addr, 6);

            /* Get node IP address (from routing table or mesh API) */
            /* Note: ESP-MESH routing table doesn't directly provide IP addresses */
            /* We'll set IP to 0.0.0.0 for now - this can be enhanced later */
            node->ip[0] = 0;
            node->ip[1] = 0;
            node->ip[2] = 0;
            node->ip[3] = 0;

            /* Layer information: ESP-MESH routing table doesn't provide individual node layers */
            /* For nodes in root's routing table, they are typically layer 1 (direct children) */
            /* Set to 1 as default for direct children of root */
            node->layer = 1;

            /* Parent ID: For nodes in root's routing table, parent is typically the root */
            /* Note: This may be incorrect for multi-layer meshes, but ESP-MESH API doesn't provide parent info */
            memcpy(node->parent_id, root_addr.addr, 6);

            /* Role: 0=root, 1=child, 2=leaf */
            /* Nodes in routing table (excluding root) are child nodes */
            node->role = 1;

            /* Status: 1=connected (nodes in routing table are connected) */
            node->status = 1;

            node_idx++;
        }

        /* Validate node_count matches actual nodes collected */
        if (node_idx != child_count) {
            ESP_LOGW(TAG, "Node count mismatch: expected %d, collected %d", child_count, node_idx);
            /* Update node_count to match actual collected nodes */
            state->node_count = (uint8_t)node_idx;
        }
    } else if (child_count > MAX_STATE_UPDATE_NODES) {
        ESP_LOGW(TAG, "Too many nodes (%d > %d), limiting to %d", child_count, MAX_STATE_UPDATE_NODES, MAX_STATE_UPDATE_NODES);
        state->node_count = MAX_STATE_UPDATE_NODES;
        /* Allocate for maximum nodes */
        state->nodes = (mesh_node_entry_t *)malloc(MAX_STATE_UPDATE_NODES * sizeof(mesh_node_entry_t));
        if (state->nodes == NULL) {
            ESP_LOGE(TAG, "Failed to allocate node list");
            return ESP_ERR_NO_MEM;
        }
        memset(state->nodes, 0, MAX_STATE_UPDATE_NODES * sizeof(mesh_node_entry_t));

        /* Fill node list (limited to MAX_STATE_UPDATE_NODES) */
        int node_idx = 0;
        for (int i = 0; i < route_table_size && node_idx < MAX_STATE_UPDATE_NODES; i++) {
            /* Skip root node */
            bool is_root = true;
            for (int j = 0; j < 6; j++) {
                if (route_table[i].addr[j] != root_addr.addr[j]) {
                    is_root = false;
                    break;
                }
            }
            if (is_root) {
                continue;
            }

            /* Get node information */
            mesh_node_entry_t *node = &state->nodes[node_idx];
            memcpy(node->node_id, route_table[i].addr, 6);
            node->ip[0] = 0;
            node->ip[1] = 0;
            node->ip[2] = 0;
            node->ip[3] = 0;
            node->layer = 1;
            memcpy(node->parent_id, root_addr.addr, 6);
            node->role = 1;
            node->status = 1;
            node_idx++;
        }

        /* Validate node_count matches actual nodes collected */
        if (node_idx != MAX_STATE_UPDATE_NODES) {
            ESP_LOGW(TAG, "Node count mismatch: expected %d, collected %d", MAX_STATE_UPDATE_NODES, node_idx);
            /* Update node_count to match actual collected nodes */
            state->node_count = (uint8_t)node_idx;
        }
    }

    /* Get sequence state */
    state->sequence_active = mode_sequence_root_is_active() ? 1 : 0;
    if (state->sequence_active) {
        state->sequence_position = htons(mode_sequence_root_get_pointer());
        /* Note: sequence_total is not directly available, we'll set it to 0 for now */
        state->sequence_total = 0;
    } else {
        state->sequence_position = 0;
        state->sequence_total = 0;
    }

    /* Get OTA state */
    mesh_ota_distribution_status_t ota_status;
    err = mesh_ota_get_distribution_status(&ota_status);
    if (err == ESP_OK && ota_status.distributing) {
        state->ota_in_progress = 1;
        float progress = mesh_ota_get_distribution_progress();
        state->ota_progress = (uint8_t)(progress * 100.0f);
        if (state->ota_progress > 100) {
            state->ota_progress = 100;
        }
    } else {
        state->ota_in_progress = 0;
        state->ota_progress = 0;
    }

    return ESP_OK;
}

/**
 * @brief Build binary payload from state data.
 *
 * Encodes the state data structure into a binary payload for UDP transmission.
 *
 * @param state State data structure
 * @param buffer Output buffer for binary payload
 * @param buffer_size Size of output buffer
 * @param payload_size Output parameter for actual payload size
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if buffer too small, error code on failure
 */
esp_err_t mesh_udp_bridge_build_state_payload(const mesh_state_data_t *state,
                                                uint8_t *buffer, size_t buffer_size,
                                                size_t *payload_size)
{
    if (state == NULL || buffer == NULL || payload_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate required payload size */
    size_t required_size = 4 + 6 + 4 + 1 + 1 + (state->node_count * sizeof(mesh_node_entry_t)) + 1 + 2 + 2 + 1 + 1;
    if (buffer_size < required_size) {
        ESP_LOGE(TAG, "Buffer too small: %zu < %zu", buffer_size, required_size);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;

    /* Encode root IP (4 bytes, network byte order) */
    memcpy(&buffer[offset], state->root_ip, 4);
    offset += 4;

    /* Encode mesh ID (6 bytes) */
    memcpy(&buffer[offset], state->mesh_id, 6);
    offset += 6;

    /* Encode timestamp (4 bytes, network byte order) */
    memcpy(&buffer[offset], &state->timestamp, 4);
    offset += 4;

    /* Encode mesh state (1 byte) */
    buffer[offset++] = state->mesh_state;

    /* Encode node count (1 byte) */
    buffer[offset++] = state->node_count;

    /* Encode node list */
    if (state->nodes != NULL && state->node_count > 0) {
        for (uint8_t i = 0; i < state->node_count; i++) {
            mesh_node_entry_t *node = &state->nodes[i];
            /* Node ID (6 bytes) */
            memcpy(&buffer[offset], node->node_id, 6);
            offset += 6;
            /* IP (4 bytes, network byte order) */
            memcpy(&buffer[offset], node->ip, 4);
            offset += 4;
            /* Layer (1 byte) */
            buffer[offset++] = node->layer;
            /* Parent ID (6 bytes) */
            memcpy(&buffer[offset], node->parent_id, 6);
            offset += 6;
            /* Role (1 byte) */
            buffer[offset++] = node->role;
            /* Status (1 byte) */
            buffer[offset++] = node->status;
        }
    }

    /* Encode sequence state */
    buffer[offset++] = state->sequence_active;
    memcpy(&buffer[offset], &state->sequence_position, 2);
    offset += 2;
    memcpy(&buffer[offset], &state->sequence_total, 2);
    offset += 2;

    /* Encode OTA state */
    buffer[offset++] = state->ota_in_progress;
    buffer[offset++] = state->ota_progress;

    *payload_size = offset;
    return ESP_OK;
}

/**
 * @brief Send state update to external server.
 *
 * Sends a state update UDP packet to the external web server (if registered).
 * State updates are fire-and-forget (no ACK required) and completely optional.
 *
 * @param payload Binary payload buffer
 * @param payload_size Size of payload in bytes
 * @return ESP_OK on send attempt (even if send fails), ESP_ERR_INVALID_STATE if not registered or not root
 */
esp_err_t mesh_udp_bridge_send_state_update(const uint8_t *payload, size_t payload_size)
{
    /* Check if registered */
    if (!mesh_udp_bridge_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if root node */
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Initialize UDP socket if needed */
    esp_err_t err = init_udp_socket();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UDP socket: %s", esp_err_to_name(err));
        return err;
    }

    /* Check UDP MTU limit (1472 bytes) */
    size_t packet_size = 1 + 2 + payload_size + 2; /* CMD + LEN + PAYLOAD + CHKSUM */
    if (packet_size > 1472) {
        ESP_LOGW(TAG, "State update packet too large: %zu bytes (max 1472), skipping", packet_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Allocate packet buffer */
    uint8_t *packet = (uint8_t *)malloc(packet_size);
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate packet buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Pack UDP packet: [CMD:0xE2][LEN:2][PAYLOAD:N][CHKSUM:2] */
    packet[0] = UDP_CMD_STATE_UPDATE;
    packet[1] = (payload_size >> 8) & 0xFF; /* Length MSB */
    packet[2] = payload_size & 0xFF; /* Length LSB */
    memcpy(&packet[3], payload, payload_size);

    /* Calculate checksum (16-bit sum of all bytes excluding checksum) */
    uint16_t checksum = 0;
    for (size_t i = 0; i < packet_size - 2; i++) {
        checksum = (checksum + packet[i]) & 0xFFFF;
    }
    packet[packet_size - 2] = (checksum >> 8) & 0xFF; /* Checksum MSB */
    packet[packet_size - 1] = checksum & 0xFF; /* Checksum LSB */

    /* Send UDP packet (fire-and-forget, no ACK wait) */
    ssize_t sent = sendto(s_udp_socket, packet, packet_size, 0,
                          (struct sockaddr *)&s_server_addr, sizeof(s_server_addr));
    if (sent < 0) {
        /* Log at debug level - packet loss is acceptable for state updates */
        ESP_LOGD(TAG, "State update send failed: %d (acceptable for fire-and-forget)", errno);
    } else {
        ESP_LOGD(TAG, "State update sent: payload_size=%zu", payload_size);
    }

    free(packet);
    return ESP_OK; /* Return OK even if send failed (fire-and-forget) */
}

/**
 * @brief State update task function.
 *
 * This task periodically collects mesh state and sends state updates to the external web server.
 * The task exits if the node is no longer root or registration is lost.
 */
static void mesh_udp_bridge_state_update_task(void *pvParameters)
{
    ESP_LOGI(TAG, "State update task started");

    while (1) {
        /* Check if still root and registered */
        if (!esp_mesh_is_root() || !mesh_udp_bridge_is_registered()) {
            ESP_LOGI(TAG, "State update task exiting: not root or not registered");
            break;
        }

        /* Collect mesh state */
        mesh_state_data_t state;
        esp_err_t err = mesh_udp_bridge_collect_state(&state);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to collect state: %s (continuing)", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(STATE_UPDATE_INTERVAL_SECONDS * 1000));
            continue;
        }

        /* Build binary payload */
        size_t payload_size = 0;
        size_t buffer_size = 4 + 6 + 4 + 1 + 1 + (MAX_STATE_UPDATE_NODES * sizeof(mesh_node_entry_t)) + 1 + 2 + 2 + 1 + 1;
        uint8_t *payload_buffer = (uint8_t *)malloc(buffer_size);
        if (payload_buffer == NULL) {
            ESP_LOGW(TAG, "Failed to allocate payload buffer (continuing)");
            /* Free node list */
            if (state.nodes != NULL) {
                free(state.nodes);
            }
            vTaskDelay(pdMS_TO_TICKS(STATE_UPDATE_INTERVAL_SECONDS * 1000));
            continue;
        }

        err = mesh_udp_bridge_build_state_payload(&state, payload_buffer, buffer_size, &payload_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to build state payload: %s (continuing)", esp_err_to_name(err));
            free(payload_buffer);
            /* Free node list */
            if (state.nodes != NULL) {
                free(state.nodes);
            }
            vTaskDelay(pdMS_TO_TICKS(STATE_UPDATE_INTERVAL_SECONDS * 1000));
            continue;
        }

        /* Send state update */
        err = mesh_udp_bridge_send_state_update(payload_buffer, payload_size);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGD(TAG, "State update send error: %s (continuing)", esp_err_to_name(err));
        }

        /* Free buffers */
        free(payload_buffer);
        if (state.nodes != NULL) {
            free(state.nodes);
        }

        /* Sleep for interval */
        vTaskDelay(pdMS_TO_TICKS(STATE_UPDATE_INTERVAL_SECONDS * 1000));
    }

    /* Clean up on exit */
    s_state_update_task_handle = NULL;
    s_state_update_running = false;
    ESP_LOGI(TAG, "State update task stopped");
    vTaskDelete(NULL);
}

/*******************************************************
 *                UDP Broadcast Listener Implementation
 *******************************************************/

/* Broadcast listener task state */
static TaskHandle_t s_broadcast_listener_task_handle = NULL;
static bool s_broadcast_listener_running = false;
static int s_broadcast_listener_socket = -1;

/* Broadcast port (default 5353) */
#define BROADCAST_LISTENER_PORT  5353

/* Maximum broadcast payload size */
#define MAX_BROADCAST_PAYLOAD_SIZE  256

/**
 * @brief Simple JSON parser for broadcast payload.
 *
 * Parses a simple JSON object like: {"service":"lyktparad-web","port":8080,"udp_port":8081,"protocol":"udp","version":"1.0.0"}
 * This is a minimal parser that only handles the specific format we need.
 *
 * @param json JSON string to parse
 * @param json_len Length of JSON string
 * @param service_out Output buffer for service name (must be at least 32 bytes)
 * @param port_out Output pointer for HTTP port number (optional, can be NULL)
 * @param udp_port_out Output pointer for UDP port number (required)
 * @param protocol_out Output buffer for protocol (must be at least 16 bytes)
 * @param version_out Output buffer for version (must be at least 16 bytes)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t parse_broadcast_json(const char *json, size_t json_len,
                                      char *service_out, uint16_t *port_out,
                                      uint16_t *udp_port_out,
                                      char *protocol_out, char *version_out)
{
    if (json == NULL || service_out == NULL || udp_port_out == NULL ||
        protocol_out == NULL || version_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Initialize outputs */
    memset(service_out, 0, 32);
    if (port_out != NULL) {
        *port_out = 0;
    }
    *udp_port_out = 0;
    memset(protocol_out, 0, 16);
    memset(version_out, 0, 16);

    /* Simple JSON parsing - look for key-value pairs */
    const char *service_key = "\"service\":\"";
    const char *port_key = "\"port\":";
    const char *udp_port_key = "\"udp_port\":";
    const char *protocol_key = "\"protocol\":\"";
    const char *version_key = "\"version\":\"";

    /* Find service */
    const char *service_start = strstr(json, service_key);
    if (service_start != NULL) {
        service_start += strlen(service_key);
        const char *service_end = strchr(service_start, '"');
        if (service_end != NULL && (service_end - service_start) < 32) {
            size_t service_len = service_end - service_start;
            memcpy(service_out, service_start, service_len);
            service_out[service_len] = '\0';
        }
    }

    /* Find HTTP port (optional) */
    if (port_out != NULL) {
        const char *port_start = strstr(json, port_key);
        if (port_start != NULL) {
            port_start += strlen(port_key);
            *port_out = (uint16_t)atoi(port_start);
        }
    }

    /* Find UDP port (required) */
    const char *udp_port_start = strstr(json, udp_port_key);
    if (udp_port_start != NULL) {
        udp_port_start += strlen(udp_port_key);
        *udp_port_out = (uint16_t)atoi(udp_port_start);
    } else {
        /* Fallback: if udp_port not found, try to use port field */
        const char *port_start = strstr(json, port_key);
        if (port_start != NULL) {
            port_start += strlen(port_key);
            *udp_port_out = (uint16_t)atoi(port_start);
        }
    }

    /* Find protocol */
    const char *protocol_start = strstr(json, protocol_key);
    if (protocol_start != NULL) {
        protocol_start += strlen(protocol_key);
        const char *protocol_end = strchr(protocol_start, '"');
        if (protocol_end != NULL && (protocol_end - protocol_start) < 16) {
            size_t protocol_len = protocol_end - protocol_start;
            memcpy(protocol_out, protocol_start, protocol_len);
            protocol_out[protocol_len] = '\0';
        }
    }

    /* Find version */
    const char *version_start = strstr(json, version_key);
    if (version_start != NULL) {
        version_start += strlen(version_key);
        const char *version_end = strchr(version_start, '"');
        if (version_end != NULL && (version_end - version_start) < 16) {
            size_t version_len = version_end - version_start;
            memcpy(version_out, version_start, version_len);
            version_out[version_len] = '\0';
        }
    }

    /* Validate required fields */
    if (strlen(service_out) == 0 || *udp_port_out == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief Handle received UDP broadcast packet.
 *
 * Parses the broadcast payload and updates the discovered server address.
 *
 * @param buffer Received packet buffer
 * @param len Length of received packet
 * @param from_addr Source address of the broadcast
 */
static void handle_broadcast_packet(const uint8_t *buffer, size_t len,
                                    const struct sockaddr_in *from_addr)
{
    if (buffer == NULL || len == 0 || from_addr == NULL) {
        return;
    }

    /* Limit payload size */
    if (len > MAX_BROADCAST_PAYLOAD_SIZE) {
        ESP_LOGD(TAG, "Broadcast packet too large: %zu bytes (max %d), ignoring", len, MAX_BROADCAST_PAYLOAD_SIZE);
        return;
    }

    /* Null-terminate JSON string */
    char json[MAX_BROADCAST_PAYLOAD_SIZE + 1];
    if (len >= sizeof(json)) {
        len = sizeof(json) - 1;
    }
    memcpy(json, buffer, len);
    json[len] = '\0';

    /* Parse JSON */
    char service[32] = {0};
    uint16_t http_port = 0;
    uint16_t udp_port = 0;
    char protocol[16] = {0};
    char version[16] = {0};

    esp_err_t err = parse_broadcast_json(json, len, service, &http_port, &udp_port, protocol, version);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to parse broadcast JSON: %s", esp_err_to_name(err));
        return;
    }

    /* Validate service name */
    if (strcmp(service, "lyktparad-web") != 0) {
        ESP_LOGD(TAG, "Broadcast from wrong service: %s (expected lyktparad-web), ignoring", service);
        return;
    }

    /* Validate UDP port (required for registration) */
    if (udp_port == 0 || udp_port > 65535) {
        ESP_LOGD(TAG, "Invalid UDP port in broadcast: %d, ignoring", udp_port);
        return;
    }

    /* Extract IP address from source address */
    uint8_t server_ip[4];
    memcpy(server_ip, &from_addr->sin_addr.s_addr, 4);

    /* Cache discovered address in NVS */
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 server_ip[0], server_ip[1], server_ip[2], server_ip[3]);

        nvs_set_str(nvs_handle, "server_ip", ip_str);
        nvs_set_u16(nvs_handle, "server_port", udp_port);
        nvs_set_u32(nvs_handle, "server_timestamp", (uint32_t)time(NULL));
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);

        ESP_LOGI(TAG, "UDP broadcast discovery: server=%s:%d (HTTP:%d, protocol=%s, version=%s)",
                 ip_str, udp_port, http_port, protocol, version);

        /* Update registration state (if not already registered) */
        if (!s_server_registered) {
            mesh_udp_bridge_set_registration(true, server_ip, udp_port);
        }
    } else {
        ESP_LOGW(TAG, "Failed to cache discovered server address: %s", esp_err_to_name(err));
    }
}

/**
 * @brief UDP broadcast listener task function.
 *
 * This task listens for UDP broadcast packets on port 5353 and processes
 * discovery broadcasts from the external web server.
 */
static void mesh_udp_bridge_broadcast_listener_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UDP broadcast listener task started");

    /* Create UDP socket for listening */
    s_broadcast_listener_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_broadcast_listener_socket < 0) {
        ESP_LOGE(TAG, "Failed to create broadcast listener socket: %d", errno);
        s_broadcast_listener_task_handle = NULL;
        s_broadcast_listener_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* Set socket to allow broadcast reception */
    int broadcast = 1;
    if (setsockopt(s_broadcast_listener_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to set broadcast option: %d", errno);
        close(s_broadcast_listener_socket);
        s_broadcast_listener_socket = -1;
        s_broadcast_listener_task_handle = NULL;
        s_broadcast_listener_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* Set socket to non-blocking */
    int flags = fcntl(s_broadcast_listener_socket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s_broadcast_listener_socket, F_SETFL, flags | O_NONBLOCK);
    }

    /* Bind to broadcast port */
    struct sockaddr_in listen_addr = {0};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(BROADCAST_LISTENER_PORT);

    if (bind(s_broadcast_listener_socket, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind broadcast listener socket: %d", errno);
        close(s_broadcast_listener_socket);
        s_broadcast_listener_socket = -1;
        s_broadcast_listener_task_handle = NULL;
        s_broadcast_listener_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP broadcast listener bound to port %d", BROADCAST_LISTENER_PORT);

    /* Receive loop */
    uint8_t recv_buffer[MAX_BROADCAST_PAYLOAD_SIZE];
    while (1) {
        /* Check if task should exit */
        if (!s_broadcast_listener_running) {
            break;
        }

        /* Receive broadcast packet */
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t received = recvfrom(s_broadcast_listener_socket, recv_buffer, sizeof(recv_buffer), 0,
                                     (struct sockaddr *)&from_addr, &from_len);

        if (received > 0) {
            /* Handle received broadcast */
            handle_broadcast_packet(recv_buffer, received, &from_addr);
        } else if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGD(TAG, "Broadcast receive error: %d (non-critical)", errno);
            }
        }

        /* Small delay to prevent busy-waiting */
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Clean up on exit */
    if (s_broadcast_listener_socket >= 0) {
        close(s_broadcast_listener_socket);
        s_broadcast_listener_socket = -1;
    }

    s_broadcast_listener_task_handle = NULL;
    s_broadcast_listener_running = false;
    ESP_LOGI(TAG, "UDP broadcast listener task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief Start the UDP broadcast listener task.
 *
 * Starts a FreeRTOS task that listens for UDP broadcast packets on port 5353.
 * The listener is completely optional and does not affect embedded web server operation.
 */
void mesh_udp_bridge_broadcast_listener_start(void)
{
    /* Check if task already running */
    if (s_broadcast_listener_running && s_broadcast_listener_task_handle != NULL) {
        ESP_LOGD(TAG, "Broadcast listener task already running");
        return;
    }

    /* Only root node should listen for broadcasts */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "Not root node, skipping broadcast listener start");
        return;
    }

    /* Create FreeRTOS task */
    BaseType_t task_err = xTaskCreate(mesh_udp_bridge_broadcast_listener_task, "udp_broadcast_listener",
                                      4096, NULL, 1, &s_broadcast_listener_task_handle);
    if (task_err != pdPASS) {
        ESP_LOGW(TAG, "Failed to create broadcast listener task");
        s_broadcast_listener_task_handle = NULL;
        s_broadcast_listener_running = false;
    } else {
        s_broadcast_listener_running = true;
        ESP_LOGI(TAG, "Broadcast listener task started");
    }
}

/**
 * @brief Stop the UDP broadcast listener task.
 *
 * Stops the broadcast listener task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_broadcast_listener_stop(void)
{
    if (!s_broadcast_listener_running || s_broadcast_listener_task_handle == NULL) {
        /* Task not running, nothing to stop */
        return;
    }

    /* Signal task to exit */
    s_broadcast_listener_running = false;

    /* Wait a bit for task to exit */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Delete task if still running */
    if (s_broadcast_listener_task_handle != NULL) {
        vTaskDelete(s_broadcast_listener_task_handle);
        s_broadcast_listener_task_handle = NULL;
    }

    /* Close socket if still open */
    if (s_broadcast_listener_socket >= 0) {
        close(s_broadcast_listener_socket);
        s_broadcast_listener_socket = -1;
    }

    s_broadcast_listener_running = false;
    ESP_LOGI(TAG, "Broadcast listener task stopped");
}

/**
 * @brief Start the state update task.
 *
 * Starts a periodic FreeRTOS task that sends state updates at regular intervals.
 * Only starts if the node is root and registered with an external server.
 * State updates are completely optional and do not affect embedded web server operation.
 */
void mesh_udp_bridge_start_state_updates(void)
{
    /* Check if task already running */
    if (s_state_update_running && s_state_update_task_handle != NULL) {
        ESP_LOGD(TAG, "State update task already running");
        return;
    }

    /* Check if registered */
    if (!mesh_udp_bridge_is_registered()) {
        ESP_LOGD(TAG, "Not registered, skipping state update start");
        return;
    }

    /* Check if root node */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "Not root node, skipping state update start");
        return;
    }

    /* Create FreeRTOS task */
    BaseType_t task_err = xTaskCreate(mesh_udp_bridge_state_update_task, "udp_state_update",
                                      4096, NULL, 1, &s_state_update_task_handle);
    if (task_err != pdPASS) {
        ESP_LOGW(TAG, "Failed to create state update task");
        s_state_update_task_handle = NULL;
        s_state_update_running = false;
    } else {
        s_state_update_running = true;
        ESP_LOGI(TAG, "State update task started");
    }
}

/**
 * @brief Stop the state update task.
 *
 * Stops the periodic state update task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_stop_state_updates(void)
{
    if (!s_state_update_running || s_state_update_task_handle == NULL) {
        /* Task not running, nothing to stop */
        return;
    }

    /* Delete task */
    vTaskDelete(s_state_update_task_handle);
    s_state_update_task_handle = NULL;
    s_state_update_running = false;
    ESP_LOGI(TAG, "State update task stopped");
}
