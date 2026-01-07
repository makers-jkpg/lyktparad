/* Mesh UDP Bridge Module Header
 *
 * This module provides UDP communication bridge between the ESP32 root node
 * and an optional external web server. The bridge forwards mesh commands
 * to the external server for monitoring purposes and handles root node
 * registration.
 *
 * Copyright (c) 2025 the_louie
 */

#ifndef __MESH_UDP_BRIDGE_H__
#define __MESH_UDP_BRIDGE_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*******************************************************
 *                UDP Command IDs
 *******************************************************/

/* UDP Command ID for Registration */
#define UDP_CMD_REGISTRATION  0xE0

/* UDP Command ID for Registration ACK */
#define UDP_CMD_REGISTRATION_ACK  0xE3

/* UDP Command ID for Heartbeat */
#define UDP_CMD_HEARTBEAT  0xE1

/* UDP Command ID for Mesh Command Forward */
#define UDP_CMD_MESH_COMMAND_FORWARD  0xE6

/* UDP Command ID for State Update */
#define UDP_CMD_STATE_UPDATE  0xE2

/* UDP Command IDs for API Commands (0xE7-0xF8) */
#define UDP_CMD_API_NODES  0xE7
#define UDP_CMD_API_COLOR_GET  0xE8
#define UDP_CMD_API_COLOR_POST  0xE9
#define UDP_CMD_API_SEQUENCE_POST  0xEA
#define UDP_CMD_API_SEQUENCE_POINTER  0xEB
#define UDP_CMD_API_SEQUENCE_START  0xEC
#define UDP_CMD_API_SEQUENCE_STOP  0xED
#define UDP_CMD_API_SEQUENCE_RESET  0xEE
#define UDP_CMD_API_SEQUENCE_STATUS  0xEF
#define UDP_CMD_API_OTA_DOWNLOAD  0xF0
#define UDP_CMD_API_OTA_STATUS  0xF1
#define UDP_CMD_API_OTA_VERSION  0xF2
#define UDP_CMD_API_OTA_CANCEL  0xF3
#define UDP_CMD_API_OTA_DISTRIBUTE  0xF4
#define UDP_CMD_API_OTA_DISTRIBUTION_STATUS  0xF5
#define UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS  0xF6
#define UDP_CMD_API_OTA_DISTRIBUTION_CANCEL  0xF7
#define UDP_CMD_API_OTA_REBOOT  0xF8
#define UDP_CMD_API_PLUGIN_ACTIVATE    0xFA
#define UDP_CMD_API_PLUGIN_DEACTIVATE  0xFB
#define UDP_CMD_API_PLUGIN_ACTIVE       0xFC
#define UDP_CMD_API_PLUGINS_LIST        0xFD
#define UDP_CMD_API_PLUGIN_STOP         0xF9
#define UDP_CMD_API_PLUGIN_PAUSE        0xFE
#define UDP_CMD_API_PLUGIN_RESET        0xFF

/*******************************************************
 *                State Update Data Structures
 *******************************************************/

/**
 * @brief Node entry structure for state updates.
 *
 * Represents a single node in the mesh network.
 */
typedef struct {
    uint8_t node_id[6];      /* Node MAC address (6 bytes) */
    uint8_t ip[4];          /* Node IP address (network byte order) */
    uint8_t layer;           /* Mesh layer */
    uint8_t parent_id[6];    /* Parent node MAC address (6 bytes) */
    uint8_t role;            /* Node role (0=root, 1=child, 2=leaf) */
    uint8_t status;          /* Node status (0=disconnected, 1=connected) */
} __attribute__((packed)) mesh_node_entry_t;

/**
 * @brief Mesh state data structure.
 *
 * Contains complete mesh network state information.
 */
typedef struct {
    uint8_t root_ip[4];              /* Root node IP address (network byte order) */
    uint8_t mesh_id[6];               /* Mesh ID (MAC address) */
    uint32_t timestamp;               /* Unix timestamp (network byte order) */
    uint8_t mesh_state;               /* Mesh state (0=disconnected, 1=connected) */
    uint8_t node_count;               /* Number of connected nodes */
    mesh_node_entry_t *nodes;         /* Array of node entries (dynamically allocated) */
    uint8_t sequence_active;          /* Sequence active flag (0=inactive, 1=active) */
    uint16_t sequence_position;     /* Current sequence position (network byte order) */
    uint16_t sequence_total;          /* Total sequence length (network byte order) */
    uint8_t ota_in_progress;          /* OTA distribution in progress (0=no, 1=yes) */
    uint8_t ota_progress;             /* OTA progress percentage (0-100) */
} mesh_state_data_t;

/*******************************************************
 *                Registration Payload Structure
 *******************************************************/

/**
 * @brief Payload structure for root node registration.
 *
 * This structure is sent as the payload for UDP_CMD_REGISTRATION.
 * The structure is packed to ensure consistent byte alignment across platforms.
 */
typedef struct {
    uint8_t root_ip[4];           /* IPv4 address (network byte order) */
    uint8_t mesh_id[6];           /* Mesh ID */
    uint8_t node_count;           /* Number of connected nodes */
    uint8_t firmware_version_len; /* Length of version string */
    char firmware_version[32];     /* Version string (null-terminated, max 31 chars) */
    uint32_t timestamp;           /* Unix timestamp (network byte order) */
} __attribute__((packed)) mesh_registration_payload_t;

/*******************************************************
 *                Heartbeat Payload Structure
 *******************************************************/

/**
 * @brief Payload structure for heartbeat/keepalive messages.
 *
 * This structure is sent as the payload for UDP_CMD_HEARTBEAT.
 * The structure is packed to ensure consistent byte alignment across platforms.
 */
typedef struct {
    uint32_t timestamp;    /* Unix timestamp (network byte order) */
    uint8_t node_count;    /* Number of connected nodes (optional) */
} __attribute__((packed)) mesh_heartbeat_payload_t;
/*******************************************************
 *                Bridge Functions
 *******************************************************/

/**
 * @brief Check if external server is registered.
 *
 * Returns true if an external server has successfully registered with this root node.
 *
 * @return true if external server is registered, false otherwise
 */
bool mesh_udp_bridge_is_registered(void);

/**
 * @brief Check if external server was discovered.
 *
 * Returns true if an external server was discovered (via mDNS or cached).
 * This is used to determine if registration should be attempted.
 *
 * @return true if external server was discovered, false otherwise
 */
bool mesh_udp_bridge_is_server_discovered(void);

/**
 * @brief Set external server registration state.
 *
 * Called by discovery module when external server is discovered or disconnected.
 * This function is for use by the discovery module (extweb_05).
 *
 * @param registered True if server is registered, false if disconnected
 * @param server_ip Server IP address (network byte order), NULL if disconnected
 * @param server_port Server UDP port
 */
void mesh_udp_bridge_set_registration(bool registered, const uint8_t *server_ip, uint16_t server_port);

/**
 * @brief Register root node with external web server.
 *
 * This function registers the root node with an external web server (if discovered).
 * Registration is completely optional and non-blocking. The embedded web server
 * continues to operate regardless of registration status.
 *
 * Registration includes:
 * - Root node IP address
 * - Mesh ID
 * - Node count
 * - Firmware version
 * - Timestamp
 *
 * The function will:
 * - Check if external server was discovered (return early if not)
 * - Gather all registration data
 * - Send UDP registration packet
 * - Wait for ACK with timeout (5 seconds)
 * - Retry up to 3 times with exponential backoff (1s, 2s, 4s)
 * - Give up after retries (don't retry indefinitely)
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if server not discovered (not an error),
 *         other error codes on failure
 */
esp_err_t mesh_udp_bridge_register(void);

/**
 * @brief Get discovered server IP and port.
 *
 * Retrieves the IP address and UDP port of the discovered external web server.
 *
 * @param server_ip Output buffer for server IP (must be at least 16 bytes)
 * @param server_port Output pointer for UDP port
 * @return ESP_OK if server discovered, ESP_ERR_NOT_FOUND if not discovered, error code on failure
 */
esp_err_t mesh_udp_bridge_get_cached_server(char *server_ip, uint16_t *server_port);

/*******************************************************
 *                Heartbeat Functions
 *******************************************************/

/**
 * @brief Send a single heartbeat UDP packet.
 *
 * This function sends a heartbeat packet to the external web server (if registered).
 * Heartbeat is fire-and-forget (no ACK required) and completely optional.
 *
 * @return ESP_OK on send attempt (even if send fails), ESP_ERR_INVALID_STATE if not registered or not root
 */
esp_err_t mesh_udp_bridge_send_heartbeat(void);

/**
 * @brief Start the heartbeat task.
 *
 * Starts a periodic FreeRTOS task that sends heartbeat packets at regular intervals.
 * Only starts if the node is root and registered with an external server.
 * Heartbeat is completely optional and does not affect embedded web server operation.
 */
void mesh_udp_bridge_start_heartbeat(void);

/**
 * @brief Stop the heartbeat task.
 *
 * Stops the periodic heartbeat task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_stop_heartbeat(void);

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
                                                 size_t mesh_payload_len);

/*******************************************************
 *                State Update Functions
 *******************************************************/

/**
 * @brief Collect mesh state information.
 *
 * Collects complete mesh network state including nodes, sequence state, and OTA state.
 * This function allocates memory for the node list which must be freed by the caller.
 *
 * @param state Output structure to fill with state data
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_collect_state(mesh_state_data_t *state);

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
                                               size_t *payload_size);

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
esp_err_t mesh_udp_bridge_send_state_update(const uint8_t *payload, size_t payload_size);

/**
 * @brief Start the state update task.
 *
 * Starts a periodic FreeRTOS task that sends state updates at regular intervals.
 * Only starts if the node is root and registered with an external server.
 * State updates are completely optional and do not affect embedded web server operation.
 */
void mesh_udp_bridge_start_state_updates(void);

/**
 * @brief Stop the state update task.
 *
 * Stops the periodic state update task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_stop_state_updates(void);

/*******************************************************
 *                UDP Broadcast Listener Functions
 *******************************************************/

/**
 * @brief Start the UDP broadcast listener task.
 *
 * Starts a FreeRTOS task that listens for UDP broadcast packets on port 5353.
 * The listener is completely optional and does not affect embedded web server operation.
 * Broadcast discovery runs in parallel with mDNS discovery (if available).
 */
void mesh_udp_bridge_broadcast_listener_start(void);

/**
 * @brief Stop the UDP broadcast listener task.
 *
 * Stops the broadcast listener task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_broadcast_listener_stop(void);

/*******************************************************
 *                UDP API Command Listener Functions
 *******************************************************/

/**
 * @brief Start the UDP API command listener task.
 *
 * Starts a FreeRTOS task that listens for UDP API commands (0xE7-0xF8) from the external server.
 * The listener processes API commands and sends responses back to the server.
 * Only starts if the node is root. The listener is completely optional and does not affect
 * embedded web server operation.
 */
void mesh_udp_bridge_api_listener_start(void);

/**
 * @brief Stop the UDP API command listener task.
 *
 * Stops the API command listener task and cleans up resources.
 * This function is safe to call even if the task is not running.
 */
void mesh_udp_bridge_api_listener_stop(void);

/*******************************************************
 *                mDNS Discovery Functions
 *******************************************************/

/**
 * @brief Initialize mDNS component for service discovery.
 *
 * Initializes the ESP-IDF mDNS component and sets the hostname.
 * This function is idempotent - calling it multiple times is safe.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_mdns_init(void);

/**
 * @brief Discover external web server via mDNS.
 *
 * Queries for _lyktparad-web._tcp service and extracts server IP and UDP port.
 * The UDP port is extracted from TXT records if available, otherwise uses HTTP port.
 *
 * @param timeout_ms Query timeout in milliseconds (10000-30000)
 * @param server_ip Output buffer for server IP (must be at least 16 bytes)
 * @param server_port Output pointer for UDP port
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_discover_server(uint32_t timeout_ms, char *server_ip, uint16_t *server_port);

/**
 * @brief Cache discovered server address in NVS.
 *
 * Stores the server IP address and UDP port in NVS for use on subsequent boots.
 *
 * @param server_ip Server IP address string (e.g., "192.168.1.100")
 * @param server_port Server UDP port
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_cache_server(const char *server_ip, uint16_t server_port);

/**
 * @brief Start background retry task for service discovery.
 *
 * Starts a FreeRTOS task that retries discovery with exponential backoff.
 * The task will stop automatically if discovery succeeds.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_start_retry_task(void);

/**
 * @brief Stop background retry task for service discovery.
 *
 * Stops the retry task if it is running.
 */
void mesh_udp_bridge_stop_retry_task(void);

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
void mesh_udp_bridge_broadcast_server_ip(const char *ip, uint16_t port);

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
esp_err_t mesh_udp_bridge_resolve_hostname(const char *hostname, char *ip_out, size_t ip_len);

/**
 * @brief Test UDP connection to server.
 *
 * Tests if a UDP connection can be established to the given IP/hostname and port.
 * This function resolves hostnames and sends a test registration packet, waiting
 * for an ACK with a 5 second timeout.
 *
 * @param ip_or_hostname IP address or hostname string
 * @param port UDP port number
 * @return true if connection test succeeds, false otherwise
 */
bool mesh_udp_bridge_test_connection(const char *ip_or_hostname, uint16_t port);

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
esp_err_t mesh_udp_bridge_store_manual_config(const char *ip_or_hostname, uint16_t port, const char *resolved_ip);

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
esp_err_t mesh_udp_bridge_get_manual_config(char *ip_or_hostname, size_t hostname_len, uint16_t *port, char *resolved_ip, size_t resolved_len);

/**
 * @brief Clear manual server configuration from NVS.
 *
 * Erases all manual server configuration keys from NVS.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_clear_manual_config(void);

/**
 * @brief Set manual server IP and register with external server.
 *
 * Resolves hostname if needed, stores configuration in NVS, and sets registration state.
 *
 * @param ip_or_hostname Server IP address or hostname string
 * @param port Server UDP port
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_set_manual_server_ip(const char *ip_or_hostname, uint16_t port);

/**
 * @brief Clear manual server IP configuration.
 *
 * Clears manual configuration from NVS and clears registration state if it was using manual IP.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_udp_bridge_clear_manual_server_ip(void);

/**
 * @brief Check and use cached IP address if valid.
 *
 * Reads cached server IP and port from NVS, tests the connection, and if valid,
 * sets the server registration. This is an optimization to avoid mDNS discovery
 * delay when a valid cached IP is available.
 *
 * @return true if cached IP is valid and used, false otherwise
 */
bool mesh_udp_bridge_use_cached_ip(void);

#endif /* __MESH_UDP_BRIDGE_H__ */
