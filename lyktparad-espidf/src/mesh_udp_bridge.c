/* Mesh UDP Bridge Module Implementation
 *
 * This module provides UDP communication bridge between the ESP32 root node
 * and an optional external web server. The bridge handles root node registration
 * and forwards mesh commands to the external server for monitoring purposes.
 *
 * Copyright (c) 2025 the_louie
 */

/* Ensure CONFIG_MESH_ROUTE_TABLE_SIZE is defined before any includes */
#ifndef CONFIG_MESH_ROUTE_TABLE_SIZE
#define CONFIG_MESH_ROUTE_TABLE_SIZE 50
#endif

#include "mesh_udp_bridge.h"
#include "mesh_commands.h"
#include "mesh_common.h"
#include "mesh_version.h"
#include "config/mesh_config.h"
#include "config/mesh_device_config.h"
#include "plugin_system.h"
#include "mesh_ota.h"
#include "light_neopixel.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"
/* mDNS is required at build time and is the primary discovery method.
 * UDP broadcast is only used as a runtime fallback when mDNS discovery fails (server not found).
 * The build will fail if mDNS component is unavailable, so these stubs should never be used in normal operation. */
#if defined(CONFIG_MDNS_MAX_INTERFACES) || defined(CONFIG_MDNS_TASK_PRIORITY)
    #include "mdns.h"
    #define MDNS_AVAILABLE 1
#else
    /* mDNS component not available - provide stubs (should not happen - mDNS is required) */
    #define MDNS_AVAILABLE 0
    /* Stub type definitions */
    typedef struct {
        const char *key;
        const char *value;
    } mdns_txt_item_t;
    typedef struct {
        struct {
            struct {
                int type;
                union {
                    struct {
                        uint32_t addr;
                    } ip4;
                } u_addr;
            } addr;
        } *addr;
        uint16_t port;
        mdns_txt_item_t *txt;
        size_t txt_count;
    } mdns_result_t;
    /* Stub function implementations */
    static inline esp_err_t mdns_init(void) { return ESP_ERR_NOT_SUPPORTED; }
    static inline esp_err_t mdns_hostname_set(const char *hostname) { (void)hostname; return ESP_ERR_NOT_SUPPORTED; }
    static inline esp_err_t mdns_query_ptr(const char *service, const char *proto, uint32_t timeout, int max_results, mdns_result_t **results) { (void)service; (void)proto; (void)timeout; (void)max_results; if (results) *results = NULL; return ESP_ERR_NOT_SUPPORTED; }
    static inline void mdns_query_results_free(mdns_result_t *results) { (void)results; }
#endif
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

/* Ensure MACSTR is defined - it should be in esp_mac.h */
#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

static const char *TAG = "mesh_udp_bridge";

/* External server registration state */
static bool s_server_registered = false;
static struct sockaddr_in s_server_addr = {0};
static int s_udp_socket = -1;

/* Registration status tracking */
static bool s_registration_complete = false;

/* NVS namespace for cached server address */
#define NVS_NAMESPACE_UDP_BRIDGE "udp_bridge"
#define NVS_KEY_SERVER_IP "server_ip"
#define NVS_KEY_SERVER_PORT "server_port"

/* NVS keys for manual server configuration */
#define NVS_KEY_MANUAL_SERVER_IP "manual_server_ip"
#define NVS_KEY_MANUAL_SERVER_PORT "manual_server_port"
#define NVS_KEY_MANUAL_SERVER_RESOLVED_IP "manual_server_resolved_ip"

/* mDNS initialization state */
#if MDNS_AVAILABLE
static bool s_mdns_initialized = false;
#endif

/* Retry task state */
static TaskHandle_t s_retry_task_handle = NULL;
static bool s_retry_task_running = false;

/* Broadcast guard - prevents duplicate broadcasts */
static bool s_broadcast_sent = false;

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
 mesh_udp_bridge_get_mesh_id(payload->mesh_id);
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
#ifdef ONLY_ONBOARD_HTTP
    ESP_LOGD(TAG, "ONLY_ONBOARD_HTTP enabled - registration disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    /* Check for manual server IP first (takes precedence over discovery) */
    char manual_ip[64] = {0};
    char manual_resolved_ip[16] = {0};
    uint16_t manual_port = 0;
    esp_err_t manual_err = mesh_udp_bridge_get_manual_config(manual_ip, sizeof(manual_ip), &manual_port, manual_resolved_ip, sizeof(manual_resolved_ip));
    if (manual_err == ESP_OK) {
        /* Manual IP is configured, use it for registration */
        const char *ip_to_use = (manual_resolved_ip[0] != '\0') ? manual_resolved_ip : manual_ip;
        struct in_addr addr;
        if (inet_aton(ip_to_use, &addr) != 0) {
            uint8_t ip_bytes[4];
            memcpy(ip_bytes, &addr.s_addr, 4);
            mesh_udp_bridge_set_registration(true, ip_bytes, manual_port);
            ESP_LOGI(TAG, "Using manual server address for registration: %s:%d (hostname: %s)", ip_to_use, manual_port, manual_ip);
        } else {
            ESP_LOGE(TAG, "Manual server IP invalid: %s", ip_to_use);
            return ESP_ERR_INVALID_ARG;
        }
    } else if (manual_err == ESP_ERR_NOT_FOUND) {
        /* No manual IP configured, check if external server is discovered (via set_registration or cached) */
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
    } else {
        ESP_LOGE(TAG, "Failed to read manual configuration: %s", esp_err_to_name(manual_err));
        return manual_err;
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
 mesh_udp_bridge_build_registration_payload(&payload);
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
 mesh_udp_bridge_wait_for_registration_ack(5000, &ack_success);
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
        /* Reset broadcast guard when new server is registered */
        s_broadcast_sent = false;
    } else {
        /* Clear server address */
        memset(&s_server_addr, 0, sizeof(s_server_addr));
        ESP_LOGI(TAG, "External server registration cleared");
        s_registration_complete = false;
        /* Stop heartbeat and state updates when registration is cleared */
        mesh_udp_bridge_stop_heartbeat();
        mesh_udp_bridge_stop_state_updates();
        /* Reset broadcast guard when server is disconnected */
        s_broadcast_sent = false;
    }
}

/*******************************************************
 *                Cached Server Address
 *******************************************************/

/**
 * @brief Initialize mDNS component for service discovery.
 *
 * Initializes the ESP-IDF mDNS component and sets the hostname.
 * mDNS is required at build time and is the primary discovery method for the external web server.
 * This function is idempotent - calling it multiple times is safe.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_mdns_init(void)
{
#ifdef ONLY_ONBOARD_HTTP
    ESP_LOGD(TAG, "ONLY_ONBOARD_HTTP enabled - mDNS initialization disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif

#if MDNS_AVAILABLE
    if (s_mdns_initialized) {
        return ESP_OK;  /* Already initialized */
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return err;
    }

    /* Set hostname for mDNS */
    err = mdns_hostname_set("lyktparad-root");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
        /* Continue even if hostname set fails - discovery can still work */
    }

    s_mdns_initialized = true;
    ESP_LOGI(TAG, "mDNS initialized successfully");
    return ESP_OK;
#else
    /* ERROR: mDNS component not available - this should never happen since mDNS is required at build time. */
    ESP_LOGE(TAG, "ERROR: mDNS component not available - build configuration error (mDNS is required)");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/*******************************************************
 *                Service Discovery
 *******************************************************/

/**
 * @brief Discover external web server via mDNS (primary discovery method).
 *
 * Queries for _lyktparad-web._tcp service and extracts server IP and UDP port.
 * The UDP port is extracted from TXT records if available, otherwise uses HTTP port.
 * mDNS is the primary discovery method. UDP broadcast is only used as a runtime fallback
 * when mDNS discovery fails (server not found), not when the mDNS component is unavailable.
 *
 * @param timeout_ms Query timeout in milliseconds (10000-30000)
 * @param server_ip Output buffer for server IP (must be at least 16 bytes)
 * @param server_port Output pointer for UDP port
 * @return ESP_OK on success, error code on failure (ESP_ERR_NOT_FOUND if server not found)
 */
esp_err_t mesh_udp_bridge_discover_server(uint32_t timeout_ms, char *server_ip, uint16_t *server_port)
{
#ifdef ONLY_ONBOARD_HTTP
    ESP_LOGD(TAG, "ONLY_ONBOARD_HTTP enabled - server discovery disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if (server_ip == NULL || server_port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if MDNS_AVAILABLE
    /* Ensure mDNS is initialized */
    if (!s_mdns_initialized) {
        esp_err_t err = mesh_udp_bridge_mdns_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to initialize mDNS for discovery");
            return err;
        }
    }

    /* Query for _lyktparad-web._tcp service */
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_lyktparad-web", "_tcp", timeout_ms, 20, &results);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return err;
    }

    if (results == NULL) {
        ESP_LOGI(TAG, "No external web server found via mDNS");
        return ESP_ERR_NOT_FOUND;
    }

    /* Use first result (if multiple services found, use first) */
    mdns_result_t *result = results;

    /* Extract IP address */
    if (result->addr == NULL) {
        ESP_LOGE(TAG, "mDNS result has no IP address");
        mdns_query_results_free(results);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Convert IP address to string */
    if (result->addr->addr.type == IPADDR_TYPE_V4) {
        /* IPv4 address */
        struct in_addr addr;
        addr.s_addr = result->addr->addr.u_addr.ip4.addr;
        const char *ip_str = inet_ntoa(addr);
        if (ip_str == NULL) {
            ESP_LOGE(TAG, "Failed to convert IP address to string");
            mdns_query_results_free(results);
            return ESP_ERR_INVALID_RESPONSE;
        }
        strncpy(server_ip, ip_str, 15);
        server_ip[15] = '\0';
    } else {
        ESP_LOGE(TAG, "Unsupported IP address type (only IPv4 supported)");
        mdns_query_results_free(results);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Extract port - start with HTTP port from service record */
    uint16_t discovered_port = result->port;

    /* Parse TXT records for protocol and UDP port */
    bool protocol_valid = false;
    if (result->txt != NULL && result->txt_count > 0) {
        for (size_t i = 0; i < result->txt_count; i++) {
            mdns_txt_item_t *item = &result->txt[i];
            if (item->key != NULL && item->value != NULL) {
                /* Check for protocol key (should be "udp") */
                if (strcmp(item->key, "protocol") == 0) {
                    if (strcmp(item->value, "udp") == 0) {
                        protocol_valid = true;
                        ESP_LOGI(TAG, "Found protocol in TXT record: %s", item->value);
                    } else {
                        ESP_LOGW(TAG, "Unexpected protocol in TXT record: %s (expected 'udp')", item->value);
                    }
                }
                /* Check for UDP port in TXT records */
                else if (strcmp(item->key, "udp_port") == 0) {
                    /* Found UDP port in TXT record */
                    int udp_port = atoi(item->value);
                    if (udp_port > 0 && udp_port <= 65535) {
                        discovered_port = (uint16_t)udp_port;
                        ESP_LOGI(TAG, "Found UDP port in TXT record: %d", discovered_port);
                    } else {
                        ESP_LOGW(TAG, "Invalid UDP port in TXT record: %s", item->value);
                    }
                }
            }
        }
    }

    /* Log warning if protocol not found or invalid (but continue anyway) */
    if (!protocol_valid && result->txt != NULL) {
        ESP_LOGW(TAG, "Protocol 'udp' not found in TXT records (service type already validated)");
    }

    *server_port = discovered_port;

    ESP_LOGI(TAG, "Discovered external web server: %s:%d", server_ip, *server_port);

    /* Free query results */
    mdns_query_results_free(results);

    return ESP_OK;
#else
    /* ERROR: mDNS component not available - this should never happen since mDNS is required at build time. */
    ESP_LOGE(TAG, "ERROR: mDNS component not available - build configuration error (mDNS is required)");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/*******************************************************
 *                NVS Cache Management
 *******************************************************/

#define NVS_NAMESPACE_UDP_BRIDGE "udp_bridge"
#define NVS_KEY_SERVER_IP "server_ip"
#define NVS_KEY_SERVER_PORT "server_port"

/**
 * @brief Cache discovered server address in NVS.
 *
 * Stores the server IP address and UDP port in NVS for use on subsequent boots.
 *
 * @param server_ip Server IP address string (e.g., "192.168.1.100")
 * @param server_port Server UDP port
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_cache_server(const char *server_ip, uint16_t server_port)
{
    if (server_ip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE_UDP_BRIDGE, esp_err_to_name(err));
        return err;
    }

    /* Store server IP address */
 nvs_set_str(nvs_handle, NVS_KEY_SERVER_IP, server_ip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store server IP in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    /* Store server port */
 nvs_set_u16(nvs_handle, NVS_KEY_SERVER_PORT, server_port);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store server port in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    /* Commit changes */
 nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Cached server address: %s:%d", server_ip, server_port);
    return ESP_OK;
}

/**
 * @brief Retrieve cached server address from NVS.
 *
 * Reads the server IP address and UDP port from NVS cache.
 *
 * @param server_ip Output buffer for server IP (must be at least 16 bytes)
 * @param server_port Output pointer for UDP port
 * @return ESP_OK if cache exists, ESP_ERR_NOT_FOUND if cache is missing, error code on failure
 */
esp_err_t mesh_udp_bridge_get_cached_server(char *server_ip, uint16_t *server_port)
{
    if (server_ip == NULL || server_port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE_UDP_BRIDGE, esp_err_to_name(err));
        return err;
    }

    /* Read server IP address */
    size_t required_size = 16;
 nvs_get_str(nvs_handle, NVS_KEY_SERVER_IP, server_ip, &required_size);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "No cached server IP found in NVS");
        } else {
            ESP_LOGW(TAG, "Failed to read server IP from NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return err;
    }

    /* Read server port */
 nvs_get_u16(nvs_handle, NVS_KEY_SERVER_PORT, server_port);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "No cached server port found in NVS");
        } else {
            ESP_LOGW(TAG, "Failed to read server port from NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Retrieved cached server address: %s:%d", server_ip, *server_port);
    return ESP_OK;
}

/*******************************************************
 *                Hostname Resolution
 *******************************************************/

/**
 * @brief Resolve hostname to IP address.
 *
 * Resolves a hostname to an IPv4 address using DNS. If the input is already
 * an IP address, it is copied directly without DNS resolution.
 *
 * @param hostname Hostname or IP address string
 * @param ip_out Output buffer for resolved IP address (must be at least 16 bytes)
 * @param ip_len Size of ip_out buffer
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_resolve_hostname(const char *hostname, char *ip_out, size_t ip_len)
{
    if (hostname == NULL || ip_out == NULL || ip_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if input is already an IP address */
    struct in_addr addr;
    if (inet_aton(hostname, &addr) != 0) {
        /* Input is already an IP address, copy directly */
        strncpy(ip_out, hostname, ip_len - 1);
        ip_out[ip_len - 1] = '\0';
        ESP_LOGD(TAG, "Input is already an IP address: %s", ip_out);
        return ESP_OK;
    }

    /* Input is a hostname, resolve using DNS */
    ESP_LOGD(TAG, "Resolving hostname: %s", hostname);

    struct addrinfo hints;
    struct addrinfo *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  /* IPv4 only */
    hints.ai_socktype = SOCK_DGRAM;

    int err = getaddrinfo(hostname, NULL, &hints, &result);
    if (err != 0) {
        ESP_LOGW(TAG, "Failed to resolve hostname '%s': %s", hostname, gai_strerror(err));
        return ESP_ERR_NOT_FOUND;
    }

    /* Extract first IPv4 address from results */
    bool found = false;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)rp->ai_addr;
            const char *ip_str = inet_ntoa(sin->sin_addr);
            if (ip_str != NULL) {
                strncpy(ip_out, ip_str, ip_len - 1);
                ip_out[ip_len - 1] = '\0';
                found = true;
                ESP_LOGI(TAG, "Resolved hostname '%s' to IP: %s", hostname, ip_out);
                break;
            }
        }
    }

    freeaddrinfo(result);

    if (!found) {
        ESP_LOGW(TAG, "No IPv4 address found for hostname '%s'", hostname);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/*******************************************************
 *                Manual Server Configuration
 *******************************************************/

/**
 * @brief Store manual server configuration in NVS.
 *
 * Stores the server IP/hostname, port, and resolved IP in NVS for manual configuration.
 *
 * @param ip_or_hostname Server IP address or hostname string
 * @param port Server UDP port
 * @param resolved_ip Resolved IP address (if hostname was resolved), NULL if not resolved
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_store_manual_config(const char *ip_or_hostname, uint16_t port, const char *resolved_ip)
{
    if (ip_or_hostname == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE_UDP_BRIDGE, esp_err_to_name(err));
        return err;
    }

    /* Store server IP/hostname */
    err = nvs_set_str(nvs_handle, NVS_KEY_MANUAL_SERVER_IP, ip_or_hostname);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store manual server IP in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    /* Store server port */
    err = nvs_set_u16(nvs_handle, NVS_KEY_MANUAL_SERVER_PORT, port);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store manual server port in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    /* Store resolved IP if provided */
    if (resolved_ip != NULL) {
        err = nvs_set_str(nvs_handle, NVS_KEY_MANUAL_SERVER_RESOLVED_IP, resolved_ip);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to store manual server resolved IP in NVS: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    } else {
        /* Clear resolved IP if not provided */
        nvs_erase_key(nvs_handle, NVS_KEY_MANUAL_SERVER_RESOLVED_IP);
    }

    /* Commit changes */
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Stored manual server configuration: %s:%d (resolved: %s)", ip_or_hostname, port, resolved_ip ? resolved_ip : "N/A");
    return ESP_OK;
}

/**
 * @brief Retrieve manual server configuration from NVS.
 *
 * Reads the manual server IP/hostname, port, and resolved IP from NVS.
 *
 * @param ip_or_hostname Output buffer for IP/hostname (must be at least hostname_len bytes)
 * @param hostname_len Size of ip_or_hostname buffer
 * @param port Output pointer for UDP port
 * @param resolved_ip Output buffer for resolved IP (must be at least resolved_len bytes), can be NULL
 * @param resolved_len Size of resolved_ip buffer
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if not configured, error code on failure
 */
esp_err_t mesh_udp_bridge_get_manual_config(char *ip_or_hostname, size_t hostname_len, uint16_t *port, char *resolved_ip, size_t resolved_len)
{
    if (ip_or_hostname == NULL || port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE_UDP_BRIDGE, esp_err_to_name(err));
        return err;
    }

    /* Read server IP/hostname */
    size_t required_size = hostname_len;
    err = nvs_get_str(nvs_handle, NVS_KEY_MANUAL_SERVER_IP, ip_or_hostname, &required_size);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "No manual server IP found in NVS");
        } else {
            ESP_LOGW(TAG, "Failed to read manual server IP from NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return err;
    }

    /* Read server port */
    err = nvs_get_u16(nvs_handle, NVS_KEY_MANUAL_SERVER_PORT, port);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "No manual server port found in NVS");
        } else {
            ESP_LOGW(TAG, "Failed to read manual server port from NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return err;
    }

    /* Read resolved IP if buffer provided */
    if (resolved_ip != NULL && resolved_len > 0) {
        required_size = resolved_len;
        err = nvs_get_str(nvs_handle, NVS_KEY_MANUAL_SERVER_RESOLVED_IP, resolved_ip, &required_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            /* Resolved IP not stored, that's okay */
            resolved_ip[0] = '\0';
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read manual server resolved IP from NVS: %s", esp_err_to_name(err));
            resolved_ip[0] = '\0';
        }
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Retrieved manual server configuration: %s:%d", ip_or_hostname, *port);
    return ESP_OK;
}

/**
 * @brief Clear manual server configuration from NVS.
 *
 * Erases all manual server configuration keys from NVS.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_clear_manual_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE_UDP_BRIDGE, esp_err_to_name(err));
        return err;
    }

    /* Erase all manual configuration keys */
    nvs_erase_key(nvs_handle, NVS_KEY_MANUAL_SERVER_IP);
    nvs_erase_key(nvs_handle, NVS_KEY_MANUAL_SERVER_PORT);
    nvs_erase_key(nvs_handle, NVS_KEY_MANUAL_SERVER_RESOLVED_IP);

    /* Commit changes */
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Cleared manual server configuration");
    return ESP_OK;
}

/**
 * @brief Set manual server IP and register with external server.
 *
 * Resolves hostname if needed, stores configuration in NVS, and sets registration state.
 *
 * @param ip_or_hostname Server IP address or hostname string
 * @param port Server UDP port
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_set_manual_server_ip(const char *ip_or_hostname, uint16_t port)
{
    if (ip_or_hostname == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Resolve hostname to IP if needed */
    char resolved_ip[16] = {0};
    esp_err_t resolve_err = mesh_udp_bridge_resolve_hostname(ip_or_hostname, resolved_ip, sizeof(resolved_ip));
    if (resolve_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resolve hostname '%s': %s", ip_or_hostname, esp_err_to_name(resolve_err));
        return resolve_err;
    }

    /* Store manual configuration in NVS */
    esp_err_t err = mesh_udp_bridge_store_manual_config(ip_or_hostname, port, resolved_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store manual configuration: %s", esp_err_to_name(err));
        return err;
    }

    /* Convert resolved IP to network byte order and set registration */
    struct in_addr addr;
    if (inet_aton(resolved_ip, &addr) != 0) {
        uint8_t ip_bytes[4];
        memcpy(ip_bytes, &addr.s_addr, 4);
        mesh_udp_bridge_set_registration(true, ip_bytes, port);
        ESP_LOGI(TAG, "Manual server IP set: %s:%d (resolved: %s)", ip_or_hostname, port, resolved_ip);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to convert resolved IP address: %s", resolved_ip);
        return ESP_ERR_INVALID_ARG;
    }
}

/**
 * @brief Clear manual server IP configuration.
 *
 * Clears manual configuration from NVS and clears registration state.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_clear_manual_server_ip(void)
{
    /* Clear registration state first (if it was using manual IP) */
    mesh_udp_bridge_set_registration(false, NULL, 0);

    /* Clear manual configuration from NVS */
    esp_err_t err = mesh_udp_bridge_clear_manual_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear manual configuration: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Manual server IP cleared and registration state reset");
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
 init_udp_socket();
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
 mesh_udp_bridge_get_mesh_id(state->mesh_id);
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

    /* Get sequence state using plugin query interface */
    bool is_active = false;
    esp_err_t query_err = plugin_query_state("sequence", 0x01, &is_active);  /* SEQUENCE_QUERY_IS_ACTIVE */
    state->sequence_active = (query_err == ESP_OK && is_active) ? 1 : 0;
    if (state->sequence_active) {
        uint16_t pointer = 0;
        query_err = plugin_query_state("sequence", 0x02, &pointer);  /* SEQUENCE_QUERY_GET_POINTER */
        if (query_err != ESP_OK) {
            pointer = 0;  /* Default to 0 on error */
        }
        state->sequence_position = htons(pointer);
        /* Note: sequence_total is not directly available, we'll set it to 0 for now */
        state->sequence_total = htons(0);
    } else {
        state->sequence_position = htons(0);
        state->sequence_total = htons(0);
    }

    /* Get OTA state */
    mesh_ota_distribution_status_t ota_status;
 mesh_ota_get_distribution_status(&ota_status);
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

 mesh_udp_bridge_build_state_payload(&state, payload_buffer, buffer_size, &payload_size);
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
 mesh_udp_bridge_send_state_update(payload_buffer, payload_size);
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
    if (udp_port == 0) {
        ESP_LOGD(TAG, "Invalid UDP port in broadcast: %d, ignoring", udp_port);
        return;
    }

    /* Extract IP address from source address */
    uint8_t server_ip[4];
    memcpy(server_ip, &from_addr->sin_addr.s_addr, 4);

    /* Cache discovered address in NVS */
    nvs_handle_t nvs_handle;
 nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READWRITE, &nvs_handle);
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

        /* Clear any existing discovery failure state */
        mesh_common_clear_discovery_failed();

        /* Update registration state (if not already registered) */
        if (!s_server_registered) {
            mesh_udp_bridge_set_registration(true, server_ip, udp_port);
            /* Stop mDNS discovery retry task since UDP broadcast discovery succeeded (optional optimization) */
            mesh_udp_bridge_stop_retry_task();
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
#ifdef ONLY_ONBOARD_HTTP
    ESP_LOGD(TAG, "ONLY_ONBOARD_HTTP enabled - broadcast listener disabled");
    return;
#endif

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

/*******************************************************
 *                UDP API Command Listener Implementation
 *******************************************************/

/* API listener task state */
static TaskHandle_t s_api_listener_task_handle = NULL;
static bool s_api_listener_running = false;
static int s_api_listener_socket = -1;

/* API listener port (uses same port as registration, but bound to receive) */
/* Note: We'll bind to INADDR_ANY to receive from any source */

/* Maximum API command payload size */
#define MAX_API_PAYLOAD_SIZE  1500

/**
 * @brief Calculate checksum for UDP packet.
 *
 * @param data Data buffer
 * @param len Length of data
 * @return 16-bit checksum
 */
static uint16_t calculate_udp_checksum(const uint8_t *data, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (sum + data[i]) & 0xFFFF;
    }
    return sum;
}

/**
 * @brief Build UDP API response packet.
 *
 * Packet format: [CMD:1][LEN:2][SEQ:2][PAYLOAD:N][CHKSUM:2]
 *
 * @param commandId Command ID (same as request)
 * @param seqNum Sequence number (same as request)
 * @param payload Response payload buffer
 * @param payload_size Payload size
 * @param packet_out Output buffer for complete packet
 * @param packet_size_out Output parameter for packet size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t build_api_response_packet(uint8_t commandId, uint16_t seqNum,
                                            const uint8_t *payload, size_t payload_size,
                                            uint8_t *packet_out, size_t *packet_size_out)
{
    if (packet_out == NULL || packet_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate packet size: CMD(1) + LEN(2) + SEQ(2) + PAYLOAD(N) + CHKSUM(2) */
    size_t packet_size = 1 + 2 + 2 + payload_size + 2;

    /* Check UDP MTU limit */
    if (packet_size > 1472) {
        ESP_LOGW(TAG, "API response packet too large: %zu bytes (max 1472)", packet_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Build packet without checksum */
    size_t offset = 0;
    packet_out[offset++] = commandId;
    packet_out[offset++] = (payload_size >> 8) & 0xFF; /* Length MSB */
    packet_out[offset++] = payload_size & 0xFF; /* Length LSB */
    packet_out[offset++] = (seqNum >> 8) & 0xFF; /* Sequence MSB */
    packet_out[offset++] = seqNum & 0xFF; /* Sequence LSB */
    if (payload_size > 0 && payload != NULL) {
        memcpy(&packet_out[offset], payload, payload_size);
        offset += payload_size;
    }

    /* Calculate checksum over packet without checksum bytes */
    uint16_t checksum = calculate_udp_checksum(packet_out, offset);

    /* Add checksum */
    packet_out[offset++] = (checksum >> 8) & 0xFF; /* Checksum MSB */
    packet_out[offset++] = checksum & 0xFF; /* Checksum LSB */

    *packet_size_out = packet_size;
    return ESP_OK;
}

/**
 * @brief Handle API command: GET /api/nodes
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_nodes(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int node_count = mesh_get_node_count();

    /* Response format: [node_count:4 bytes, network byte order] */
    if (max_payload_size < 4) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t node_count_net = htonl((uint32_t)node_count);
    memcpy(payload_out, &node_count_net, 4);
    *payload_size_out = 4;

    return ESP_OK;
}

/**
 * @brief Handle API command: GET /api/color
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_color_get(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Response format: [r:1][g:1][b:1][is_set:1] */
    if (max_payload_size < 4) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t r, g, b;
    bool is_set;
    mesh_get_current_rgb(&r, &g, &b, &is_set);

    payload_out[0] = r;
    payload_out[1] = g;
    payload_out[2] = b;
    payload_out[3] = is_set ? 1 : 0;
    *payload_size_out = 4;

    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/color
 *
 * @param payload Request payload [r:1][g:1][b:1]
 * @param payload_size Payload size
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_color_post(const uint8_t *payload, size_t payload_size,
                                       uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size < 3 || response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Request format: [r:1][g:1][b:1] */
    uint8_t r = payload[0];
    uint8_t g = payload[1];
    uint8_t b = payload[2];

    /* Apply color via mesh */
    esp_err_t err = mesh_send_rgb(r, g, b);

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: POST /api/sequence
 *
 * @param payload Request payload [rhythm:1][num_rows:1][color_data:N]
 * @param payload_size Payload size
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_sequence_post(const uint8_t *payload, size_t payload_size,
                                          uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size < 2 || response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        /* Response format: [success:1] (0=failure) */
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    /* Request format: [rhythm:1][num_rows:1][color_data:N] */
    uint8_t rhythm = payload[0];
    uint8_t num_rows = payload[1];

    /* Validate rhythm (1-255) */
    if (rhythm == 0) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate num_rows (1-16) */
    if (num_rows < 1 || num_rows > 16) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate expected payload size using plugin helper */
    uint16_t expected_size = 0;
    esp_err_t helper_err = plugin_get_helper("sequence", 0x01, &num_rows, &expected_size);  /* SEQUENCE_HELPER_PAYLOAD_SIZE */
    if (helper_err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }
    if (payload_size != expected_size) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_SIZE;
    }

    /* Extract color data */
    const uint8_t *color_data = &payload[2];
    uint16_t color_data_len = payload_size - 2;

    /* Store and broadcast sequence data using plugin operation */
    typedef struct {
        uint8_t rhythm;
        uint8_t num_rows;
        uint8_t *color_data;
        uint16_t color_data_len;
    } store_params_t;
    store_params_t store_params = {
        .rhythm = rhythm,
        .num_rows = num_rows,
        .color_data = (uint8_t *)color_data,
        .color_data_len = color_data_len
    };
    esp_err_t err = plugin_execute_operation("sequence", 0x01, &store_params);  /* SEQUENCE_OP_STORE */

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: GET /api/sequence/pointer
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_sequence_pointer(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        /* Return 0 if not root */
        if (max_payload_size < 2) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint16_t zero = 0;
        memcpy(payload_out, &zero, 2);
        *payload_size_out = 2;
        return ESP_ERR_INVALID_STATE;
    }

    /* Response format: [pointer:2 bytes, network byte order] */
    if (max_payload_size < 2) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Get pointer using plugin query */
    uint16_t pointer = 0;
    esp_err_t query_err = plugin_query_state("sequence", 0x02, &pointer);  /* SEQUENCE_QUERY_GET_POINTER */
    if (query_err != ESP_OK) {
        pointer = 0;  /* Default to 0 on error */
    }
    uint16_t pointer_net = htons(pointer);
    memcpy(payload_out, &pointer_net, 2);
    *payload_size_out = 2;

    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/sequence/start
 *
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_sequence_start(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    /* Use plugin system API to ensure proper broadcasting to child nodes */
    esp_err_t err = plugin_activate("sequence");

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: POST /api/sequence/stop
 *
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_sequence_stop(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    /* Use plugin system API to ensure proper broadcasting to child nodes */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name("sequence", &plugin_id);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_FAIL;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_STOP] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_STOP;

    /* Send STOP command via plugin system (from API - processes locally and broadcasts) */
    err = plugin_system_handle_plugin_command_from_api(cmd_data, sizeof(cmd_data));

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: POST /api/sequence/reset
 *
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_sequence_reset(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    /* Use plugin system API to ensure proper broadcasting to child nodes */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name("sequence", &plugin_id);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_FAIL;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_RESET] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_RESET;

    /* Send RESET command via plugin system (from API - processes locally and broadcasts) */
    err = plugin_system_handle_plugin_command_from_api(cmd_data, sizeof(cmd_data));

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: GET /api/sequence/status
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_sequence_status(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        /* Return inactive if not root */
        if (max_payload_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        payload_out[0] = 0;
        *payload_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    /* Response format: [active:1] (0=inactive, 1=active) */
    if (max_payload_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Query active state using plugin query interface */
    bool active = false;
    esp_err_t query_err = plugin_query_state("sequence", 0x01, &active);  /* SEQUENCE_QUERY_IS_ACTIVE */
    if (query_err != ESP_OK) {
        active = false;  /* Default to false on error */
    }
    payload_out[0] = active ? 1 : 0;
    *payload_size_out = 1;

    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/ota/download
 *
 * @param payload Request payload [url_len:1][url:N bytes]
 * @param payload_size Payload size
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_download(const uint8_t *payload, size_t payload_size,
                                         uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size < 1 || response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    /* Request format: [url_len:1][url:N bytes] */
    uint8_t url_len = payload[0];
    if (url_len == 0 || url_len > (payload_size - 1)) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract URL string */
    char url[256];
    if (url_len >= (sizeof(url) - 1)) {
        url_len = sizeof(url) - 1;
    }
    memcpy(url, &payload[1], url_len);
    url[url_len] = '\0';

    /* Start OTA download */
    esp_err_t err = mesh_ota_download_firmware(url);

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: GET /api/ota/status
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_status(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Response format: [downloading:1][progress:4 bytes float, network byte order] */
    if (max_payload_size < 5) {
        return ESP_ERR_INVALID_SIZE;
    }

    bool downloading = mesh_ota_is_downloading();
    float progress = mesh_ota_get_download_progress();

    payload_out[0] = downloading ? 1 : 0;
    /* Convert float to network byte order (simple approach: use uint32_t representation) */
    union {
        float f;
        uint32_t i;
    } progress_union;
    progress_union.f = progress;
    uint32_t progress_net = htonl(progress_union.i);
    memcpy(&payload_out[1], &progress_net, 4);

    *payload_size_out = 5;

    return ESP_OK;
}

/**
 * @brief Handle API command: GET /api/ota/version
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_version(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *version = mesh_version_get_string();
    if (version == NULL) {
        version = "unknown";
    }

    size_t version_len = strlen(version);
    if (version_len > 255) {
        version_len = 255;
    }

    /* Response format: [version_len:1][version:N bytes] */
    if (max_payload_size < 1 + version_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    payload_out[0] = (uint8_t)version_len;
    memcpy(&payload_out[1], version, version_len);
    *payload_size_out = 1 + version_len;

    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/ota/cancel
 *
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_cancel(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = mesh_ota_cancel_download();

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: POST /api/ota/distribute
 *
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_distribute(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = mesh_ota_distribute_firmware();

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: GET /api/ota/distribution/status
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_distribution_status(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    mesh_ota_distribution_status_t status;
    esp_err_t err = mesh_ota_get_distribution_status(&status);

    /* Response format: [distributing:1] (0=not distributing, 1=distributing) */
    if (max_payload_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    payload_out[0] = (err == ESP_OK && status.distributing) ? 1 : 0;
    *payload_size_out = 1;

    return ESP_OK;
}

/**
 * @brief Handle API command: GET /api/ota/distribution/progress
 *
 * @param payload_out Output buffer for response payload
 * @param payload_size_out Output parameter for payload size
 * @param max_payload_size Maximum payload buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_distribution_progress(uint8_t *payload_out, size_t *payload_size_out, size_t max_payload_size)
{
    if (payload_out == NULL || payload_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float progress = mesh_ota_get_distribution_progress();

    /* Response format: [progress:4 bytes float, network byte order] */
    if (max_payload_size < 4) {
        return ESP_ERR_INVALID_SIZE;
    }

    union {
        float f;
        uint32_t i;
    } progress_union;
    progress_union.f = progress;
    uint32_t progress_net = htonl(progress_union.i);
    memcpy(payload_out, &progress_net, 4);

    *payload_size_out = 4;

    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/ota/distribution/cancel
 *
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_distribution_cancel(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = mesh_ota_cancel_distribution();

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: POST /api/ota/reboot
 *
 * @param payload Request payload [timeout:2][delay:2] (network byte order)
 * @param payload_size Payload size
 * @param response_out Output buffer for response payload
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_ota_reboot(const uint8_t *payload, size_t payload_size,
                                       uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size < 4 || response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_ERR_INVALID_STATE;
    }

    /* Request format: [timeout:2 bytes, network byte order][delay:2 bytes, network byte order] */
    /* Read network byte order values safely (avoid alignment issues) */
    uint16_t timeout = ((uint16_t)payload[0] << 8) | payload[1];
    uint16_t delay = ((uint16_t)payload[2] << 8) | payload[3];

    /* Defaults if 0 */
    if (timeout == 0) {
        timeout = 10;
    }
    if (delay == 0) {
        delay = 1000;
    }

    /* Initiate coordinated reboot */
    esp_err_t err = mesh_ota_initiate_coordinated_reboot(timeout, delay);

    /* Response format: [success:1] (0=failure, 1=success) */
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = (err == ESP_OK) ? 1 : 0;
    *response_size_out = 1;

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Handle API command: POST /api/plugin/activate
 *
 * @param payload Request payload: [name_len:1][name:N bytes]
 * @param payload_size Payload size
 * @param response_out Output buffer for response
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_plugin_activate(const uint8_t *payload, size_t payload_size,
                                            uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size == 0 || response_out == NULL || response_size_out == NULL) {
        if (response_size_out) *response_size_out = 0;
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse plugin name from payload: [name_len:1][name:N bytes] */
    if (payload_size < 1) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t name_len = payload[0];
    if (name_len == 0 || name_len >= payload_size) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    char plugin_name[64] = {0};
    if (name_len >= sizeof(plugin_name)) {
        name_len = sizeof(plugin_name) - 1;
    }
    memcpy(plugin_name, &payload[1], name_len);
    plugin_name[name_len] = '\0';

    /* Activate plugin */
    esp_err_t err = plugin_activate(plugin_name);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Success response: [success:1][name_len:1][name:N bytes] */
    if (max_response_size < 2 + name_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_out[0] = 1; /* success */
    response_out[1] = name_len;
    memcpy(&response_out[2], plugin_name, name_len);
    *response_size_out = 2 + name_len;
    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/plugin/deactivate
 *
 * @param payload Request payload: [name_len:1][name:N bytes]
 * @param payload_size Payload size
 * @param response_out Output buffer for response
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_plugin_deactivate(const uint8_t *payload, size_t payload_size,
                                             uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size == 0 || response_out == NULL || response_size_out == NULL) {
        if (response_size_out) *response_size_out = 0;
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse plugin name from payload: [name_len:1][name:N bytes] */
    if (payload_size < 1) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t name_len = payload[0];
    if (name_len == 0 || name_len >= payload_size) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    char plugin_name[64] = {0};
    if (name_len >= sizeof(plugin_name)) {
        name_len = sizeof(plugin_name) - 1;
    }
    memcpy(plugin_name, &payload[1], name_len);
    plugin_name[name_len] = '\0';

    /* Deactivate plugin */
    esp_err_t err = plugin_deactivate(plugin_name);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Success response: [success:1][name_len:1][name:N bytes] */
    if (max_response_size < 2 + name_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_out[0] = 1; /* success */
    response_out[1] = name_len;
    memcpy(&response_out[2], plugin_name, name_len);
    *response_size_out = 2 + name_len;
    return ESP_OK;
}

/**
 * @brief Handle API command: GET /api/plugin/active
 *
 * @param response_out Output buffer for response
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_plugin_active(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *active = plugin_get_active();
    if (active == NULL) {
        /* No active plugin: [0] */
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0;
        *response_size_out = 1;
        return ESP_OK;
    }

    /* Active plugin: [name_len:1][name:N bytes] */
    uint8_t name_len = strlen(active);
    if (name_len > 63) {
        name_len = 63; /* Limit to 63 bytes */
    }

    if (max_response_size < 1 + name_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    response_out[0] = name_len;
    memcpy(&response_out[1], active, name_len);
    *response_size_out = 1 + name_len;
    return ESP_OK;
}

/**
 * @brief Handle API command: GET /api/plugins
 *
 * @param response_out Output buffer for response
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_plugins_list(uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (response_out == NULL || response_size_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *names[16]; /* MAX_PLUGINS = 16 */
    uint8_t count = 0;
    esp_err_t err = plugin_get_all_names(names, 16, &count);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Response format: [count:1][name1_len:1][name1:N bytes][name2_len:1][name2:N bytes]... */
    size_t offset = 0;
    if (max_response_size < 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_out[offset++] = count;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t name_len = strlen(names[i]);
        if (name_len > 63) {
            name_len = 63; /* Limit to 63 bytes */
        }

        if (offset + 1 + name_len > max_response_size) {
            /* Truncate if buffer is too small */
            break;
        }

        response_out[offset++] = name_len;
        memcpy(&response_out[offset], names[i], name_len);
        offset += name_len;
    }

    *response_size_out = offset;
    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/plugin/stop
 *
 * @param payload Request payload: [name_len:1][name:N bytes]
 * @param payload_size Payload size
 * @param response_out Output buffer for response
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_plugin_stop(const uint8_t *payload, size_t payload_size,
                                        uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size == 0 || response_out == NULL || response_size_out == NULL) {
        if (response_size_out) *response_size_out = 0;
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse plugin name from payload: [name_len:1][name:N bytes] */
    if (payload_size < 1) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t name_len = payload[0];
    if (name_len == 0 || name_len >= payload_size) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    char plugin_name[64] = {0};
    if (name_len >= sizeof(plugin_name)) {
        name_len = sizeof(plugin_name) - 1;
    }
    memcpy(plugin_name, &payload[1], name_len);
    plugin_name[name_len] = '\0';

    /* Check if plugin is active */
    if (!plugin_is_active(plugin_name)) {
        /* Plugin is not active, return success */
        if (max_response_size < 2 + name_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 1; /* success */
        response_out[1] = name_len;
        memcpy(&response_out[2], plugin_name, name_len);
        *response_size_out = 2 + name_len;
        return ESP_OK;
    }

    /* Get plugin info to check if it has on_pause callback */
    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin != NULL && plugin->callbacks.on_pause != NULL) {
        /* Call on_pause callback first (graceful stop) */
        esp_err_t pause_err = plugin->callbacks.on_pause();
        if (pause_err != ESP_OK) {
            ESP_LOGW(TAG, "Plugin '%s' on_pause callback returned error: %s", plugin_name, esp_err_to_name(pause_err));
            /* Continue with deactivation even if pause fails */
        }
    }

    /* Force deactivation (mutual exclusivity enforcement) */
    esp_err_t err = plugin_deactivate(plugin_name);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Success response: [success:1][name_len:1][name:N bytes] */
    if (max_response_size < 2 + name_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_out[0] = 1; /* success */
    response_out[1] = name_len;
    memcpy(&response_out[2], plugin_name, name_len);
    *response_size_out = 2 + name_len;
    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/plugin/pause
 *
 * @param payload Request payload: [name_len:1][name:N bytes]
 * @param payload_size Payload size
 * @param response_out Output buffer for response
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_plugin_pause(const uint8_t *payload, size_t payload_size,
                                         uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size == 0 || response_out == NULL || response_size_out == NULL) {
        if (response_size_out) *response_size_out = 0;
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse plugin name from payload: [name_len:1][name:N bytes] */
    if (payload_size < 1) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t name_len = payload[0];
    if (name_len == 0 || name_len >= payload_size) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    char plugin_name[64] = {0};
    if (name_len >= sizeof(plugin_name)) {
        name_len = sizeof(plugin_name) - 1;
    }
    memcpy(plugin_name, &payload[1], name_len);
    plugin_name[name_len] = '\0';

    /* Get plugin ID by name */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name(plugin_name, &plugin_id);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_PAUSE] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_PAUSE;

    /* Send PAUSE command via plugin system */
    err = plugin_system_handle_plugin_command(cmd_data, sizeof(cmd_data));
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Success response: [success:1][name_len:1][name:N bytes] */
    if (max_response_size < 2 + name_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_out[0] = 1; /* success */
    response_out[1] = name_len;
    memcpy(&response_out[2], plugin_name, name_len);
    *response_size_out = 2 + name_len;
    return ESP_OK;
}

/**
 * @brief Handle API command: POST /api/plugin/reset
 *
 * @param payload Request payload: [name_len:1][name:N bytes]
 * @param payload_size Payload size
 * @param response_out Output buffer for response
 * @param response_size_out Output parameter for response size
 * @param max_response_size Maximum response buffer size
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t handle_api_plugin_reset(const uint8_t *payload, size_t payload_size,
                                         uint8_t *response_out, size_t *response_size_out, size_t max_response_size)
{
    if (payload == NULL || payload_size == 0 || response_out == NULL || response_size_out == NULL) {
        if (response_size_out) *response_size_out = 0;
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse plugin name from payload: [name_len:1][name:N bytes] */
    if (payload_size < 1) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t name_len = payload[0];
    if (name_len == 0 || name_len >= payload_size) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return ESP_ERR_INVALID_ARG;
    }

    char plugin_name[64] = {0};
    if (name_len >= sizeof(plugin_name)) {
        name_len = sizeof(plugin_name) - 1;
    }
    memcpy(plugin_name, &payload[1], name_len);
    plugin_name[name_len] = '\0';

    /* Get plugin ID by name */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name(plugin_name, &plugin_id);
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_RESET] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_RESET;

    /* Send RESET command via plugin system */
    err = plugin_system_handle_plugin_command(cmd_data, sizeof(cmd_data));
    if (err != ESP_OK) {
        if (max_response_size < 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        response_out[0] = 0; /* failure */
        *response_size_out = 1;
        return err;
    }

    /* Success response: [success:1][name_len:1][name:N bytes] */
    if (max_response_size < 2 + name_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    response_out[0] = 1; /* success */
    response_out[1] = name_len;
    memcpy(&response_out[2], plugin_name, name_len);
    *response_size_out = 2 + name_len;
    return ESP_OK;
}

/**
 * @brief Process incoming UDP API command and send response.
 *
 * @param commandId Command ID
 * @param seqNum Sequence number
 * @param payload Request payload
 * @param payload_size Payload size
 * @param from_addr Source address (to send response back)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t process_api_command(uint8_t commandId, uint16_t seqNum,
                                     const uint8_t *payload, size_t payload_size,
                                     const struct sockaddr_in *from_addr)
{
    if (from_addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Note: payload can be NULL for GET commands (no request body) */

    /* Response buffer */
    uint8_t response_payload[512];
    size_t response_payload_size = 0;

    /* Route command to appropriate handler */
    switch (commandId) {
        case UDP_CMD_API_NODES:
            handle_api_nodes(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_COLOR_GET:
            handle_api_color_get(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_COLOR_POST:
            handle_api_color_post(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_SEQUENCE_POST:
            handle_api_sequence_post(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_SEQUENCE_POINTER:
            handle_api_sequence_pointer(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_SEQUENCE_START:
            handle_api_sequence_start(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_SEQUENCE_STOP:
            handle_api_sequence_stop(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_SEQUENCE_RESET:
            handle_api_sequence_reset(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_SEQUENCE_STATUS:
            handle_api_sequence_status(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_DOWNLOAD:
            handle_api_ota_download(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_STATUS:
            handle_api_ota_status(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_VERSION:
            handle_api_ota_version(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_CANCEL:
            handle_api_ota_cancel(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_DISTRIBUTE:
            handle_api_ota_distribute(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_DISTRIBUTION_STATUS:
            handle_api_ota_distribution_status(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS:
            handle_api_ota_distribution_progress(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_DISTRIBUTION_CANCEL:
            handle_api_ota_distribution_cancel(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_OTA_REBOOT:
            handle_api_ota_reboot(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_PLUGIN_ACTIVATE:
            handle_api_plugin_activate(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_PLUGIN_DEACTIVATE:
            handle_api_plugin_deactivate(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_PLUGIN_ACTIVE:
            handle_api_plugin_active(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_PLUGINS_LIST:
            handle_api_plugins_list(response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_PLUGIN_STOP:
            handle_api_plugin_stop(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_PLUGIN_PAUSE:
            handle_api_plugin_pause(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        case UDP_CMD_API_PLUGIN_RESET:
            handle_api_plugin_reset(payload, payload_size, response_payload, &response_payload_size, sizeof(response_payload));
            break;

        default:
            ESP_LOGW(TAG, "Unknown API command: 0x%02X", commandId);
            /* Send error response */
            if (sizeof(response_payload) >= 1) {
                response_payload[0] = 0; /* failure */
                response_payload_size = 1;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
            break;
    }

    /* Build response packet */
    uint8_t response_packet[1472];
    size_t response_packet_size = 0;
    esp_err_t build_err = build_api_response_packet(commandId, seqNum, response_payload, response_payload_size,
                                                     response_packet, &response_packet_size);
    if (build_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build API response packet: %s", esp_err_to_name(build_err));
        return build_err;
    }

    /* Send response back to sender */
    if (s_api_listener_socket < 0) {
        ESP_LOGE(TAG, "API listener socket not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ssize_t sent = sendto(s_api_listener_socket, response_packet, response_packet_size, 0,
                          (struct sockaddr *)from_addr, sizeof(*from_addr));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send API response: %d", errno);
        return ESP_FAIL;
    }

    if (sent != (ssize_t)response_packet_size) {
        ESP_LOGW(TAG, "Partial send: %d/%zu bytes", sent, response_packet_size);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "API command 0x%02X processed, response sent", commandId);
    return ESP_OK;
}

/**
 * @brief UDP API command listener task function.
 *
 * This task listens for UDP API commands from the external web server and processes them.
 * The task exits if the node is no longer root.
 */
static void mesh_udp_bridge_api_listener_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UDP API listener task started");

    /* Create UDP socket for listening */
    s_api_listener_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_api_listener_socket < 0) {
        ESP_LOGE(TAG, "Failed to create API listener socket: %d", errno);
        s_api_listener_task_handle = NULL;
        s_api_listener_running = false;
        vTaskDelete(NULL);
        return;
    }

    /* Set socket to non-blocking */
    int flags = fcntl(s_api_listener_socket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s_api_listener_socket, F_SETFL, flags | O_NONBLOCK);
    }

    /* Bind to INADDR_ANY on well-known port 8082 for API commands.
     * The external server sends API commands to rootNode.root_ip:8082.
     * This is separate from the registration port (8081) which is the server's listening port.
     */
    struct sockaddr_in listen_addr = {0};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(8082); /* Well-known port for API commands */

    if (bind(s_api_listener_socket, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind API listener socket: %d", errno);
        close(s_api_listener_socket);
        s_api_listener_socket = -1;
        s_api_listener_task_handle = NULL;
        s_api_listener_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP API listener bound to port 8082");

    /* Receive loop */
    uint8_t recv_buffer[MAX_API_PAYLOAD_SIZE + 10]; /* Extra space for packet header */
    while (1) {
        /* Check if task should exit */
        if (!s_api_listener_running) {
            break;
        }

        /* Check if still root node */
        if (!esp_mesh_is_root()) {
            ESP_LOGI(TAG, "API listener task exiting: not root node");
            break;
        }

        /* Receive API command packet */
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t received = recvfrom(s_api_listener_socket, recv_buffer, sizeof(recv_buffer), 0,
                                     (struct sockaddr *)&from_addr, &from_len);

        if (received > 0) {
            /* Minimum packet size: [CMD:1][LEN:2][SEQ:2][CHKSUM:2] = 7 bytes */
            if (received < 7) {
                ESP_LOGD(TAG, "API packet too short: %d bytes (min 7)", received);
                continue;
            }

            /* Parse packet: [CMD:1][LEN:2][SEQ:2][PAYLOAD:N][CHKSUM:2] */
            uint8_t commandId = recv_buffer[0];
            uint16_t payload_len = (recv_buffer[1] << 8) | recv_buffer[2];
            uint16_t seqNum = (recv_buffer[3] << 8) | recv_buffer[4];
            uint16_t checksum = (recv_buffer[received - 2] << 8) | recv_buffer[received - 1];

            /* Verify packet size */
            size_t expected_size = 1 + 2 + 2 + payload_len + 2; /* CMD + LEN + SEQ + PAYLOAD + CHKSUM */
            if (received != expected_size) {
                ESP_LOGD(TAG, "API packet size mismatch: expected %zu, got %d", expected_size, received);
                continue;
            }

            /* Verify checksum (optional, but recommended) */
            uint16_t calculated_checksum = calculate_udp_checksum(recv_buffer, received - 2);
            if (checksum != calculated_checksum) {
                ESP_LOGW(TAG, "API packet checksum mismatch: expected 0x%04X, got 0x%04X", checksum, calculated_checksum);
                /* Continue anyway (checksum verification is optional) */
            }

            /* Extract payload */
            const uint8_t *payload = NULL;
            size_t payload_size = 0;
            if (payload_len > 0 && received >= (5 + payload_len + 2)) {
                payload = &recv_buffer[5];
                payload_size = payload_len;
            }

            /* Process command and send response */
            esp_err_t err = process_api_command(commandId, seqNum, payload, payload_size, &from_addr);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to process API command 0x%02X: %s", commandId, esp_err_to_name(err));
            }
        } else if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGD(TAG, "API receive error: %d (non-critical)", errno);
            }
        }

        /* Small delay to prevent busy-waiting */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Clean up on exit */
    if (s_api_listener_socket >= 0) {
        close(s_api_listener_socket);
        s_api_listener_socket = -1;
    }

    s_api_listener_task_handle = NULL;
    s_api_listener_running = false;
    ESP_LOGI(TAG, "UDP API listener task stopped");
}

/**
 * @brief Start the UDP API command listener task.
 *
 * Starts a FreeRTOS task that listens for UDP API commands (0xE7-0xF8) from the external server.
 * The listener processes API commands and sends responses back to the server.
 * Only starts if the node is root. The listener is completely optional and does not affect
 * embedded web server operation.
 */
void mesh_udp_bridge_api_listener_start(void)
{
#ifdef ONLY_ONBOARD_HTTP
    ESP_LOGD(TAG, "ONLY_ONBOARD_HTTP enabled - API listener disabled");
    return;
#endif

    /* Check if task already running */
    if (s_api_listener_running && s_api_listener_task_handle != NULL) {
        ESP_LOGD(TAG, "API listener task already running");
        return;
    }

    /* Check if root node */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "Not root node, skipping API listener start");
        return;
    }

    /* Create FreeRTOS task */
    s_api_listener_running = true;
    BaseType_t task_err = xTaskCreate(mesh_udp_bridge_api_listener_task, "udp_api_listener",
                                      4096, NULL, 1, &s_api_listener_task_handle);
    if (task_err != pdPASS) {
        ESP_LOGW(TAG, "Failed to create API listener task");
        s_api_listener_task_handle = NULL;
        s_api_listener_running = false;
    } else {
        ESP_LOGI(TAG, "API listener task started");
    }
}

/**
 * @brief Stop the UDP API command listener task.
 *
 * Stops the API command listener task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_api_listener_stop(void)
{
    if (!s_api_listener_running || s_api_listener_task_handle == NULL) {
        /* Task not running, nothing to stop */
        return;
    }

    /* Stop task */
    s_api_listener_running = false;
    /* Task will clean itself up */
    ESP_LOGI(TAG, "Stopping API listener task");
}

/*******************************************************
 *                Retry Logic
 *******************************************************/

/**
 * @brief Background retry task for service discovery.
 *
 * Implements exponential backoff retry strategy:
 * - Initial delay: 5 seconds
 * - Maximum delay: 60 seconds
 * - Backoff multiplier: 2x
 *
 * The task stops if discovery succeeds or if the task is explicitly stopped.
 */
static void mesh_udp_bridge_retry_task(void *pvParameters)
{
    uint32_t delay_ms = 5000;  /* Initial delay: 5 seconds */
#if MDNS_AVAILABLE
    const uint32_t max_delay_ms = 60000;  /* Maximum delay: 60 seconds */
    const uint32_t backoff_multiplier = 2;  /* Backoff multiplier: 2x */
    char server_ip[16] = {0};
    uint16_t server_port = 0;
#endif

    ESP_LOGI(TAG, "Discovery retry task started");

    while (s_retry_task_running) {
        /* Wait for delay period */
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        /* Check if task should still run */
        if (!s_retry_task_running) {
            break;
        }

#if MDNS_AVAILABLE
        /* Ensure mDNS is initialized */
        if (!s_mdns_initialized) {
            esp_err_t err = mesh_udp_bridge_mdns_init();
            if (err != ESP_OK) {
                ESP_LOGD(TAG, "mDNS initialization failed in retry task, will retry");
                /* Increase delay and continue */
                delay_ms = (delay_ms * backoff_multiplier > max_delay_ms) ? max_delay_ms : delay_ms * backoff_multiplier;
                continue;
            }
        }

        /* Perform discovery with 20 second timeout */
        ESP_LOGI(TAG, "Retrying discovery (delay was %lu ms)", (unsigned long)delay_ms);
        esp_err_t err = mesh_udp_bridge_discover_server(20000, server_ip, &server_port);
        if (err == ESP_OK) {
            /* Discovery succeeded - cache the address and stop retrying */
            ESP_LOGI(TAG, "Discovery succeeded in retry task: %s:%d", server_ip, server_port);
            mesh_udp_bridge_cache_server(server_ip, server_port);

            /* Convert IP string to network byte order for registration */
            struct in_addr addr;
            if (inet_aton(server_ip, &addr) != 0) {
                uint8_t ip_bytes[4];
                memcpy(ip_bytes, &addr.s_addr, 4);
                mesh_udp_bridge_set_registration(true, ip_bytes, server_port);
            }

            /* Broadcast discovered IP to all child nodes (optimization) */
            mesh_udp_bridge_broadcast_server_ip(server_ip, server_port);

            /* Stop retrying */
            s_retry_task_running = false;
            s_retry_task_handle = NULL;
            vTaskDelete(NULL);
            return;
        } else {
            /* Discovery failed - increase delay for next retry */
            ESP_LOGI(TAG, "Discovery retry failed, will retry in %lu ms", (unsigned long)(delay_ms * backoff_multiplier));
            delay_ms = (delay_ms * backoff_multiplier > max_delay_ms) ? max_delay_ms : delay_ms * backoff_multiplier;
        }
#else
        /* ERROR: mDNS component not available - this should never happen since mDNS is required at build time. */
        /* The build should fail if mDNS is unavailable, so this code path indicates a configuration error. */
        ESP_LOGE(TAG, "ERROR: mDNS component not available - build configuration error (mDNS is required)");
        s_retry_task_running = false;
        s_retry_task_handle = NULL;
        vTaskDelete(NULL);
        return;
#endif
    }

    /* Task cleanup */
    s_retry_task_running = false;
    s_retry_task_handle = NULL;
    ESP_LOGI(TAG, "Discovery retry task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief Start background retry task for service discovery.
 *
 * Starts a FreeRTOS task that retries discovery with exponential backoff.
 * The task will stop automatically if discovery succeeds.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_start_retry_task(void)
{
    if (s_retry_task_running) {
        ESP_LOGD(TAG, "Retry task already running");
        return ESP_OK;
    }

    s_retry_task_running = true;
    BaseType_t err = xTaskCreate(mesh_udp_bridge_retry_task, "udp_bridge_retry", 4096, NULL, 1, &s_retry_task_handle);
    if (err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create retry task");
        s_retry_task_running = false;
        s_retry_task_handle = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Discovery retry task started");
    return ESP_OK;
}

/**
 * @brief Stop background retry task for service discovery.
 *
 * Stops the retry task if it is running.
 */
void mesh_udp_bridge_stop_retry_task(void)
{
    if (!s_retry_task_running || s_retry_task_handle == NULL) {
        return;
    }

    s_retry_task_running = false;
    /* Task will clean itself up */
    ESP_LOGI(TAG, "Stopping discovery retry task");
}

/*******************************************************
 *                Broadcast Functions
 *******************************************************/

/**
 * @brief Broadcast external web server IP and UDP port to all child nodes.
 *
 * This function broadcasts the discovered external web server IP address and UDP port
 * to all child nodes in the mesh network. Child nodes store this information in NVS
 * for use when they become root nodes (optimization to avoid mDNS discovery delay).
 *
 * This is a non-blocking, fire-and-forget operation. Broadcast failures are logged
 * but do not affect mesh operation or discovery.
 *
 * @param ip IP address string (e.g., "192.168.1.100")
 * @param port UDP port number
 */
void mesh_udp_bridge_broadcast_server_ip(const char *ip, uint16_t port)
{
    if (ip == NULL) {
        ESP_LOGW(TAG, "Cannot broadcast: IP address is NULL");
        return;
    }

    /* Only root node can broadcast */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "Not root node, skipping broadcast");
        return;
    }

    /* Check broadcast guard - only broadcast once per discovery */
    if (s_broadcast_sent) {
        ESP_LOGD(TAG, "Broadcast already sent, skipping duplicate");
        return;
    }

    /* Convert IP string to 4-byte array */
    struct in_addr addr;
    if (inet_aton(ip, &addr) == 0) {
        ESP_LOGW(TAG, "Failed to convert IP address: %s", ip);
        return;
    }

    /* Get routing table */
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Get root MAC address to filter it out from broadcast */
    mesh_addr_t root_addr;
    uint8_t mac[6];
    esp_err_t mac_err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (mac_err == ESP_OK) {
        memcpy(root_addr.addr, mac, 6);
    } else {
        /* Fallback: if we can't get root MAC, skip filtering (less safe but still works) */
        root_addr.addr[0] = 0;
    }

    /* Count child nodes (excluding root) */
    int child_node_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        /* Compare MAC addresses to exclude root */
        bool is_root = true;
        if (mac_err == ESP_OK) {
            for (int j = 0; j < 6; j++) {
                if (route_table[i].addr[j] != root_addr.addr[j]) {
                    is_root = false;
                    break;
                }
            }
        } else {
            is_root = false;  /* Can't determine, assume not root */
        }
        if (!is_root) {
            child_node_count++;
        }
    }

    if (child_node_count == 0) {
        ESP_LOGD(TAG, "No child nodes to broadcast to");
        return;
    }

    /* Get TX buffer and prepare payload */
    uint8_t *tx_buf = mesh_common_get_tx_buf();
    mesh_webserver_ip_broadcast_t *payload = (mesh_webserver_ip_broadcast_t *)(tx_buf + 1);  /* +1 for command ID */

    /* Set command ID */
    tx_buf[0] = MESH_CMD_WEBSERVER_IP_BROADCAST;

    /* Fill payload structure */
    /* Extract IP bytes in network byte order (big-endian) */
    /* addr.s_addr is already in network byte order, but on little-endian systems,
     * memcpy would copy bytes in wrong order, so we extract correctly */
    uint32_t ip_net = addr.s_addr;  /* Already in network byte order */
    payload->ip[0] = (ip_net >> 24) & 0xFF;  /* Most significant byte first (network byte order) */
    payload->ip[1] = (ip_net >> 16) & 0xFF;
    payload->ip[2] = (ip_net >> 8) & 0xFF;
    payload->ip[3] = ip_net & 0xFF;  /* Least significant byte last */
    payload->port = htons(port);  /* Port in network byte order */
    payload->timestamp = htonl((uint32_t)time(NULL));  /* Timestamp in network byte order */

    /* Create mesh data structure */
    mesh_data_t data;
    data.data = tx_buf;
    data.size = 1 + sizeof(mesh_webserver_ip_broadcast_t);  /* Command ID + payload */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Broadcast to all child nodes (excluding root) */
    int success_count = 0;
    int fail_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        /* Filter out root node */
        bool is_root = true;
        if (mac_err == ESP_OK) {
            for (int j = 0; j < 6; j++) {
                if (route_table[i].addr[j] != root_addr.addr[j]) {
                    is_root = false;
                    break;
                }
            }
        } else {
            is_root = false;  /* Can't determine, assume not root */
        }
        if (is_root) {
            continue;  /* Skip root node */
        }

        esp_err_t err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err == ESP_OK) {
            success_count++;
        } else {
            fail_count++;
            ESP_LOGD(TAG, "Broadcast send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    ESP_LOGI(TAG, "Web server IP broadcast - IP:%s, port:%d, sent to %d/%d child nodes (success:%d, failed:%d)",
             ip, port, success_count, child_node_count, success_count, fail_count);

    /* Mark broadcast as sent */
    s_broadcast_sent = true;
}

/*******************************************************
 *                Cached IP Functions
 *******************************************************/

/**
 * @brief Test UDP connection to server.
 *
 * Tests if a UDP connection can be established to the given IP and port.
 * This is a quick test (1-2 second timeout) to validate cached IP addresses.
 *
 * @param ip IP address string
 * @param port UDP port number
 * @return true if connection test succeeds, false otherwise
 */
bool mesh_udp_bridge_test_connection(const char *ip_or_hostname, uint16_t port)
{
    if (ip_or_hostname == NULL) {
        return false;
    }

    /* Resolve hostname to IP if needed */
    char resolved_ip[16] = {0};
    esp_err_t resolve_err = mesh_udp_bridge_resolve_hostname(ip_or_hostname, resolved_ip, sizeof(resolved_ip));
    if (resolve_err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to resolve hostname '%s': %s", ip_or_hostname, esp_err_to_name(resolve_err));
        return false;
    }

    const char *ip_to_use = resolved_ip;

    /* Create UDP socket */
    int test_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (test_socket < 0) {
        ESP_LOGD(TAG, "Failed to create test socket: %d", errno);
        return false;
    }

    /* Set socket timeout (5 seconds for connection test) */
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(test_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGD(TAG, "Failed to set socket timeout: %d", errno);
        close(test_socket);
        return false;
    }

    /* Set send timeout */
    if (setsockopt(test_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGD(TAG, "Failed to set send timeout: %d", errno);
        close(test_socket);
        return false;
    }

    /* Prepare server address */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_aton(ip_to_use, &server_addr.sin_addr) == 0) {
        ESP_LOGD(TAG, "Failed to convert IP address: %s", ip_to_use);
        close(test_socket);
        return false;
    }

    /* Attempt to send a test registration packet and wait for ACK */
    /* Build a minimal registration payload for testing */
    mesh_registration_payload_t test_payload;
    memset(&test_payload, 0, sizeof(test_payload));
    
    /* Get root node IP */
    esp_err_t err = mesh_udp_bridge_get_root_ip(test_payload.root_ip);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get root IP for test: %s", esp_err_to_name(err));
        close(test_socket);
        return false;
    }
    
    /* Get mesh ID */
    err = mesh_udp_bridge_get_mesh_id(test_payload.mesh_id);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to get mesh ID for test: %s", esp_err_to_name(err));
        close(test_socket);
        return false;
    }
    
    test_payload.node_count = mesh_udp_bridge_get_node_count();
    test_payload.firmware_version_len = 0;
    test_payload.timestamp = htonl((uint32_t)time(NULL));

    /* Send test registration packet */
    uint8_t test_buffer[256];
    size_t test_len = sizeof(test_payload);
    if (test_len > sizeof(test_buffer) - 3) {
        test_len = sizeof(test_buffer) - 3;
    }
    memcpy(test_buffer + 3, &test_payload, test_len);
    test_buffer[0] = UDP_CMD_REGISTRATION;
    test_buffer[1] = (test_len >> 8) & 0xFF;
    test_buffer[2] = test_len & 0xFF;

    ssize_t sent = sendto(test_socket, test_buffer, test_len + 3, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        ESP_LOGD(TAG, "Failed to send test packet to %s:%d: %d", ip_to_use, port, errno);
        close(test_socket);
        return false;
    }

    /* Wait for ACK with timeout */
    uint8_t ack_buffer[64];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(test_socket, ack_buffer, sizeof(ack_buffer), 0, (struct sockaddr *)&from_addr, &from_len);
    close(test_socket);

    if (received < 3) {
        ESP_LOGD(TAG, "No ACK received from %s:%d (timeout or error)", ip_to_use, port);
        return false;
    }

    /* Check if it's a registration ACK */
    if (ack_buffer[0] == UDP_CMD_REGISTRATION_ACK) {
        ESP_LOGI(TAG, "Connection test succeeded for %s:%d (hostname: %s)", ip_to_use, port, ip_or_hostname);
        return true;
    }

    ESP_LOGD(TAG, "Received unexpected response from %s:%d", ip_to_use, port);
    return false;
}

/**
 * @brief Check and use cached IP address if valid.
 *
 * Reads cached server IP and port from NVS, tests the connection, and if valid,
 * sets the server registration. This is an optimization to avoid mDNS discovery
 * delay when a valid cached IP is available.
 *
 * @return true if cached IP is valid and used, false otherwise
 */
bool mesh_udp_bridge_use_cached_ip(void)
{
    char server_ip[16] = {0};
    uint16_t server_port = 0;

    /* Read cached IP from NVS */
    esp_err_t err = mesh_udp_bridge_get_cached_server(server_ip, &server_port);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No cached server IP found in NVS");
        return false;
    }

    /* Optional: Check cache expiration (24 hours) */
    nvs_handle_t nvs_handle;
 nvs_open(NVS_NAMESPACE_UDP_BRIDGE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        uint32_t cached_timestamp = 0;
 nvs_get_u32(nvs_handle, "server_ip_timestamp", &cached_timestamp);
        if (err == ESP_OK) {
            /* Timestamp is already in host byte order (converted when stored) */
            time_t current_time = time(NULL);
            const time_t expiration_seconds = 24 * 60 * 60;  /* 24 hours */

            if (current_time - cached_timestamp > expiration_seconds) {
                ESP_LOGI(TAG, "Cached IP expired (age: %ld seconds)", (long)(current_time - cached_timestamp));
                nvs_close(nvs_handle);
                return false;
            }
        }
        nvs_close(nvs_handle);
    }

    /* Test UDP connection */
    if (!mesh_udp_bridge_test_connection(server_ip, server_port)) {
        ESP_LOGI(TAG, "Cached IP connection test failed: %s:%d", server_ip, server_port);
        return false;
    }

    /* Connection test succeeded - set server registration */
    struct in_addr addr;
    if (inet_aton(server_ip, &addr) != 0) {
        uint8_t ip_bytes[4];
        memcpy(ip_bytes, &addr.s_addr, 4);
        mesh_udp_bridge_set_registration(true, ip_bytes, server_port);
        ESP_LOGI(TAG, "Using cached server IP: %s:%d", server_ip, server_port);
        return true;
    }

    ESP_LOGW(TAG, "Failed to convert cached IP address: %s", server_ip);
    return false;
}
