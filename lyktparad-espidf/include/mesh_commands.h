/* Mesh Commands Header
 *
 * This header defines all mesh protocol command constants used for communication
 * between nodes in the mesh network. Command definitions are protocol-level and
 * independent of hardware implementation.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_COMMANDS_H__
#define __MESH_COMMANDS_H__

#include <stdint.h>

/*******************************************************
 *                Mesh Command Definitions
 *******************************************************/

/* Mesh Command Definitions - Light and Sequence Commands */
#define  MESH_CMD_HEARTBEAT      (0x01)
#define  MESH_CMD_LIGHT_ON_OFF   (0x02)
#define  MESH_CMD_SET_RGB        (0x03)
#define  MESH_CMD_PLUGIN_DATA    (0x04)  /* Plugin data command: variable length (1 byte command + plugin-specific data) */
#define  MESH_CMD_PLUGIN_START   (0x05)  /* Start plugin playback */
#define  MESH_CMD_PLUGIN_PAUSE   (0x06)  /* Pause plugin playback */
#define  MESH_CMD_PLUGIN_RESET   (0x07)  /* Reset plugin state */
#define  MESH_CMD_PLUGIN_BEAT    (0x08)  /* Plugin beat synchronization (2 bytes: command + 1-byte pointer) */
#define  MESH_CMD_EFFECT         (0x09)  /* Effect command */

/*******************************************************
 *                Command ID Allocation
 *******************************************************/

/* Command ID allocation strategy:
 *
 * 0x01-0x0F: Core functionality commands (15 commands)
 *   - 0x01: MESH_CMD_HEARTBEAT
 *   - 0x02: MESH_CMD_LIGHT_ON_OFF
 *   - 0x03: MESH_CMD_SET_RGB
 *   - 0x04-0x08: Plugin commands (MESH_CMD_PLUGIN_DATA, START, PAUSE, RESET, BEAT)
 *   - 0x09: MESH_CMD_EFFECT
 *   - 0x0A-0x0F: Reserved for future core commands
 *
 * 0x10-0xEF: Plugin commands (224 plugins maximum)
 *   - Command IDs are automatically assigned during plugin registration
 *   - Plugins register themselves and receive command IDs sequentially starting from 0x10
 *   - Command IDs are assigned at initialization time, before mesh starts
 *   - See plugin_system.h for plugin registration API
 *
 * 0xF0-0xFF: Internal mesh use (16 commands)
 *   - Reserved for internal mesh operations (OTA, web server IP broadcast, etc.)
 */

/*******************************************************
 *                Mesh Command Payload Structures
 *******************************************************/

/**
 * @brief Payload structure for web server IP broadcast command.
 *
 * This structure is sent as the payload for MESH_CMD_WEBSERVER_IP_BROADCAST.
 * The structure is packed to ensure consistent byte alignment across platforms.
 *
 * Fields:
 * - ip[4]: IPv4 address in network byte order (4 bytes)
 * - port: UDP port number in network byte order (2 bytes)
 * - timestamp: Optional Unix timestamp for cache expiration (4 bytes, network byte order)
 *
 * Minimum size: 6 bytes (IP + port)
 * Optional size: 10 bytes (IP + port + timestamp)
 */
typedef struct {
    uint8_t ip[4];        /* IPv4 address in network byte order */
    uint16_t port;        /* UDP port number in network byte order */
    uint32_t timestamp;   /* Optional: Unix timestamp for expiration (network byte order) */
} __attribute__((packed)) mesh_webserver_ip_broadcast_t;

/* Mesh Command Definitions - Internal Mesh Use (0xF prefix) */
/* Commands with prefix 0xF are reserved for internal mesh use (OTA/update and internal mesh operations) */
#define  MESH_CMD_OTA_REQUEST    (0xF0)  /* Leaf node requests firmware update */
#define  MESH_CMD_OTA_START      (0xF1)  /* Root starts OTA distribution */
#define  MESH_CMD_OTA_BLOCK      (0xF2)  /* Firmware block data */
#define  MESH_CMD_OTA_ACK        (0xF3)  /* Block acknowledgment */
#define  MESH_CMD_OTA_STATUS     (0xF4)  /* Update status query */
#define  MESH_CMD_OTA_PREPARE_REBOOT (0xF5)  /* Prepare for coordinated reboot */
#define  MESH_CMD_OTA_REBOOT     (0xF6)  /* Execute coordinated reboot */
#define  MESH_CMD_WEBSERVER_IP_BROADCAST (0xF7)  /* Broadcast external web server IP and UDP port to child nodes */

#endif /* __MESH_COMMANDS_H__ */
