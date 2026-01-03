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
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
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
