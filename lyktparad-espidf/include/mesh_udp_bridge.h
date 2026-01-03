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

#endif /* __MESH_UDP_BRIDGE_H__ */
