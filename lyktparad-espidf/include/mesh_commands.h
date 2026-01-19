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

/* Plugin Command Byte Constants (used in plugin protocol after plugin ID) */
#define  PLUGIN_CMD_START        (0x01)  /* Start plugin playback */
#define  PLUGIN_CMD_PAUSE        (0x02)  /* Pause plugin playback */
#define  PLUGIN_CMD_RESET        (0x03)  /* Reset plugin state */
#define  PLUGIN_CMD_DATA         (0x04)  /* General-purpose plugin data command: variable length, for plugin-specific needs beyond START/PAUSE/RESET/STOP */
/**
 * @brief PLUGIN_CMD_DATA Protocol Specification
 *
 * PLUGIN_CMD_DATA is a general-purpose command for plugin-specific needs that cannot be
 * satisfied by the standard commands (START, PAUSE, RESET, STOP). Plugins can define their
 * own protocol within PLUGIN_CMD_DATA to send arbitrary data or sub-commands.
 *
 * Protocol Format:
 *   [PLUGIN_CMD_DATA:1] [<plugin_specific_sub_command_id>:1-n] [<optional_data>:n]
 *
 *   - PLUGIN_CMD_DATA (1 byte): Command byte (0x04)
 *   - <plugin_specific_sub_command_id> (1-n bytes): Plugin-defined sub-command identifier
 *     Plugins can use any format they choose (e.g., single byte ID, multi-byte ID, etc.)
 *   - <optional_data> (n bytes): Plugin-specific data payload
 *
 * Note: The length prefix format (2-byte network byte order) shown in the Plugin Protocol
 * Format section is plugin-specific (used by sequence plugin) and is optional. Plugins can
 * define their own protocol structure within PLUGIN_CMD_DATA.
 *
 * Maximum Payload Size: 512 bytes recommended (not enforced by system)
 *   - This is the recommended maximum for PLUGIN_CMD_DATA payload (data after PLUGIN_ID and CMD)
 *   - This leaves room for PLUGIN_ID (1 byte) + CMD (1 byte) within the 1024 byte total
 *     mesh command size limit (512 + 1 + 1 = 514 bytes < 1024 bytes)
 *   - Plugins should validate their own data size limits
 */
#define  PLUGIN_CMD_STOP         (0x05)  /* Stop plugin (deactivate and reset state) */

/*******************************************************
 *                Command ID Allocation
 *******************************************************/

/* Command ID allocation strategy:
 *
 * 0x00-0x0A: Reserved (core commands and reserved range)
 *   - 0x01: MESH_CMD_HEARTBEAT (format: [CMD:1] [POINTER:1] [COUNTER:1] [IP0:1] [IP1:1] [IP2:1] [IP3:1], 7 bytes total)
 *     IP address is in network byte order. If root node has no IP address (not connected), sends 0.0.0.0 (4 zero bytes).
 *   - 0x02: MESH_CMD_LIGHT_ON_OFF
 *   - 0x03: MESH_CMD_SET_RGB
 *   - 0x04-0x09: Legacy plugin commands (DEPRECATED - use plugin protocol)
 *   - 0x0A: Reserved for future core commands
 *
 * 0x0B-0xEE: Plugin IDs (228 plugins maximum)
 *   - Plugin IDs are automatically assigned during plugin registration
 *   - Plugins register themselves and receive IDs sequentially starting from 0x0B
 *   - Plugin IDs are assigned at initialization time, before mesh starts
 *   - Registration order is deterministic (fixed in plugins.h), ensuring consistency across nodes
 *   - See plugin_system.h for plugin registration API
 *
 * Plugin Protocol Format:
 *   [PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N]
 *   - PLUGIN_ID: Plugin identifier (0x0B-0xEE)
 *   - CMD: Command byte (PLUGIN_CMD_START=0x01, PAUSE=0x02, RESET=0x03, DATA=0x04, STOP=0x05)
 *   - LENGTH: Optional 2-byte length prefix for variable-length data (network byte order, only for DATA commands)
 *     Note: Length prefix is plugin-specific and optional. Some plugins (e.g., sequence plugin) use it,
 *     while others may define their own protocol with plugin-specific sub-command IDs.
 *   - DATA: Optional command-specific data
 *     For PLUGIN_CMD_DATA: Contains plugin-specific sub-command ID and data payload. Plugins can define
 *     their own protocol structure (e.g., sub-command ID format, data layout).
 *   - Total size: Maximum 1024 bytes (including all fields)
 *   - Fixed-size commands: START, PAUSE, RESET, STOP (2 bytes: PLUGIN_ID + CMD)
 *   - Variable-size commands: DATA (minimum 2 bytes: PLUGIN_ID + CMD, plus plugin-specific data)
 *     Example with length prefix (sequence plugin): 4 bytes header (PLUGIN_ID + CMD + LENGTH) + data
 *     Example with sub-command ID: 3+ bytes (PLUGIN_ID + CMD + sub-command ID) + optional data
 *   - Note: Sequence synchronization is handled via MESH_CMD_HEARTBEAT, not via plugin BEAT commands
 *
 * 0xEF-0xFF: Reserved (internal mesh use)
 *   - Reserved for internal mesh operations (OTA, web server IP broadcast, etc.)
 *   - 0xF7: MESH_CMD_WEBSERVER_IP_BROADCAST
 *   - 0xF8: MESH_CMD_WEBSERVER_DISCOVERY_FAILED
 *   - 0xF9: MESH_CMD_QUERY_MESH_STATE
 *   - 0xFA: MESH_CMD_MESH_STATE_RESPONSE
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

/**
 * @brief Payload structure for web server discovery failure broadcast command.
 *
 * This structure is sent as the payload for MESH_CMD_WEBSERVER_DISCOVERY_FAILED.
 * The structure is packed to ensure consistent byte alignment across platforms.
 *
 * Fields:
 * - timestamp: Unix timestamp when discovery failed (4 bytes, network byte order)
 *   Used for expiration logic (e.g., 5-10 minutes) to allow recovery if network conditions change.
 *
 * Size: 4 bytes (timestamp)
 */
typedef struct {
    uint32_t timestamp;   /* Unix timestamp when discovery failed (network byte order) */
} __attribute__((packed)) mesh_webserver_discovery_failed_t;

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
#define  MESH_CMD_WEBSERVER_DISCOVERY_FAILED (0xF8)  /* Broadcast external web server discovery failure state to all mesh nodes */
#define  MESH_CMD_QUERY_MESH_STATE (0xF9)  /* Root queries child nodes for plugin state and heartbeat counter */
#define  MESH_CMD_MESH_STATE_RESPONSE (0xFA)  /* Child responds with active plugin name and current heartbeat counter */

/*******************************************************
 *                Mesh State Query Command Formats
 *******************************************************/

/**
 * @brief MESH_CMD_QUERY_MESH_STATE payload format
 *
 * Command: MESH_CMD_QUERY_MESH_STATE (0xF9)
 * Payload: None (command byte only, 1 byte)
 *
 * Sent by root node to query all child nodes for their current mesh state:
 * - Active plugin name
 * - Current local heartbeat counter value
 *
 * Each child node responds only once per query (one-time response flag).
 */

/**
 * @brief MESH_CMD_MESH_STATE_RESPONSE payload format
 *
 * Command: MESH_CMD_MESH_STATE_RESPONSE (0xFA)
 * Payload: [PLUGIN_NAME_LEN:1] [PLUGIN_NAME:N] [COUNTER:1]
 *   - PLUGIN_NAME_LEN: Length of plugin name string (1 byte, 0-255)
 *   - PLUGIN_NAME: Null-terminated plugin name string (N bytes, max length to prevent overflow)
 *   - COUNTER: Current local heartbeat counter value (1 byte, 0-255, wraps)
 *
 * Total size: 2 + PLUGIN_NAME_LEN bytes (minimum 2 bytes if plugin name is empty)
 *
 * Sent by child nodes in response to MESH_CMD_QUERY_MESH_STATE.
 * Contains the active plugin name (or empty string if no plugin active) and
 * the current local heartbeat counter value.
 */

#endif /* __MESH_COMMANDS_H__ */
