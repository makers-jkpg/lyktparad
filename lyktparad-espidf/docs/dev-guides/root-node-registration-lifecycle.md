# Root Node Registration and Communication Lifecycle - Development Guide

**Last Updated:** 2025-01-15

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Lifecycle Phases](#lifecycle-phases)
4. [IP Broadcast to Mesh Network](#ip-broadcast-to-mesh-network)
5. [Root Node Registration](#root-node-registration)
6. [Heartbeat and Keepalive](#heartbeat-and-keepalive)
7. [State Updates](#state-updates)
8. [Disconnection Detection and Recovery](#disconnection-detection-and-recovery)
9. [Error Handling and Graceful Degradation](#error-handling-and-graceful-degradation)
10. [Implementation Details](#implementation-details)
11. [API Reference](#api-reference)
12. [Integration Points](#integration-points)

## Overview

### Purpose

The Root Node Registration and Communication Lifecycle enables ESP32 mesh root nodes to optionally register with external web servers, maintain active connections through periodic heartbeats, share mesh state information, and handle disconnection scenarios gracefully. This lifecycle is completely optional - ESP32 root nodes always run their embedded web server and function independently, with external server communication serving as an enhancement only.

### Design Decisions

**Optional Infrastructure**: The entire registration and communication lifecycle is optional. ESP32 devices must continue operating normally even if registration fails, heartbeats stop, or the external server becomes unavailable. The embedded web server (`mesh_web_server_start()`) MUST ALWAYS run regardless of external server communication status.

**Embedded Server First**: The embedded web server always starts immediately on root node initialization. External server registration happens AFTER the embedded server is running, ensuring root nodes are always accessible locally even if external communication fails.

**Graceful Degradation**: All external server communication failures are handled gracefully without affecting mesh operation or the embedded web server. Errors are logged but do not cause system failures.

**Fire-and-Forget for Monitoring**: Heartbeat and state update messages use fire-and-forget semantics (no ACK required) to minimize root node load. Only registration requires acknowledgment for reliability.

**Automatic Recovery**: When root nodes re-register after disconnection (e.g., IP change, network restart), the system automatically recovers and resumes normal communication without manual intervention.

**Server-Side Detection**: Disconnection detection and recovery are primarily handled on the server side. ESP32 root nodes continue operating normally regardless of external server status.

### Lifecycle Overview

The complete lifecycle consists of several interconnected phases:

1. **Discovery**: Root node discovers external web server via mDNS or UDP broadcast (covered in [External Web Discovery Guide](external-web-discovery.md))
2. **IP Broadcast**: Discovered server IP is broadcast to all child nodes for future optimization
3. **Registration**: Root node registers with external server, providing identification and capabilities
4. **Heartbeat**: Periodic keepalive messages maintain registration and indicate active status
5. **State Updates**: Periodic mesh state information provides network topology and status
6. **Disconnection Handling**: Server-side detection and recovery for network interruptions
7. **Re-registration**: Automatic recovery when root node reconnects

## Architecture

### Overall Lifecycle Flow

```
┌─────────────────────────────────────────────────────────────────┐
│  ESP32 Root Node                                                 │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Embedded Web Server (MUST ALWAYS RUN)                   │  │
│  │  (mesh_web_server_start() on port 80)                    │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Discovery Success (mDNS or UDP Broadcast)               │  │
│  │  - Server IP: 192.168.1.100                              │  │
│  │  - UDP Port: 8081                                        │  │
│  └──────────────┬───────────────────────────────────────────┘  │
│                 │                                                │
│                 ▼                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Phase 1: IP Broadcast to Mesh                           │  │
│  │  - Broadcast to all child nodes                          │  │
│  │  - Store in child node NVS                               │  │
│  └──────────────┬───────────────────────────────────────────┘  │
│                 │                                                │
│                 ▼                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Phase 2: Registration                                   │  │
│  │  - Send registration packet (0xE0)                       │  │
│  │  - Wait for ACK (0xE3)                                   │  │
│  │  - Retry with exponential backoff                        │  │
│  └──────────────┬───────────────────────────────────────────┘  │
│                 │                                                │
│                 ▼                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Phase 3: Heartbeat Task                                 │  │
│  │  - Periodic keepalive (45s interval)                     │  │
│  │  - Fire-and-forget (no ACK)                              │  │
│  │  - Command ID: 0xE1                                      │  │
│  └──────────────┬───────────────────────────────────────────┘  │
│                 │                                                │
│                 ▼                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Phase 4: State Update Task                              │  │
│  │  - Periodic state information (60s interval)             │  │
│  │  - Fire-and-forget (no ACK)                              │  │
│  │  - Command ID: 0xE2                                      │  │
│  └──────────────┬───────────────────────────────────────────┘  │
│                 │                                                │
│                 ▼                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Phase 5: Disconnection Handling                         │  │
│  │  (Server-Side)                                           │  │
│  │  - Heartbeat timeout detection                           │  │
│  │  - UDP failure tracking                                  │  │
│  │  - Automatic cleanup                                     │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Communication Flow

```
┌─────────────────────┐                    ┌─────────────────────┐
│  ESP32 Root Node    │                    │  External Web       │
│                     │                    │  Server             │
│                     │                    │                     │
│  1. Discovery       │                    │  - mDNS Service     │
│     (mDNS/UDP)      │                    │  - UDP Broadcast    │
│                     │                    │                     │
│  2. IP Broadcast    │  MESH_CMD_WEBSERVER_IP_BROADCAST        │
│     (to child       │──────────────────────────────────────────┤
│      nodes)         │                    │                     │
│                     │                    │                     │
│  3. Registration    │  Registration (0xE0)                    │
│                     │──────────────────────────────────────────►│
│                     │  Registration ACK (0xE3)                │
│                     │◄─────────────────────────────────────────│
│                     │                    │                     │
│  4. Heartbeat       │  Heartbeat (0xE1)                       │
│     (periodic)      │──────────────────────────────────────────►│
│                     │  (fire-and-forget)                      │
│                     │                    │                     │
│  5. State Update    │  State Update (0xE2)                    │
│     (periodic)      │──────────────────────────────────────────►│
│                     │  (fire-and-forget)                      │
│                     │                    │                     │
│  6. Re-registration │  Registration (0xE0)                    │
│     (on recovery)   │──────────────────────────────────────────►│
│                     │  Registration ACK (0xE3)                │
│                     │◄─────────────────────────────────────────│
└─────────────────────┘                    └─────────────────────┘
```

## Lifecycle Phases

### Phase 1: Discovery

Discovery happens BEFORE registration and is covered in detail in the [External Web Discovery Guide](external-web-discovery.md). The discovery phase:

- Runs after embedded web server starts
- Uses mDNS (primary) or UDP broadcast (fallback)
- Caches discovered server address in NVS
- Stores server IP and UDP port for registration

**Key Points**:
- Discovery is non-blocking and runs in background tasks
- Embedded web server starts regardless of discovery status
- Discovery failures do not affect mesh operation
- First successful discovery method wins (parallel execution)

### Phase 2: IP Broadcast to Mesh Network

After successful discovery, the root node broadcasts the external server IP and UDP port to all child nodes in the mesh network. This optimization allows child nodes to cache the server address, so if they later become root nodes, they can use the cached address immediately without waiting for discovery.

**Purpose**: Speed up root node transitions by pre-caching external server address in all nodes.

**When**: Immediately after successful discovery (mDNS or UDP broadcast).

**How**: Broadcast mesh command `MESH_CMD_WEBSERVER_IP_BROADCAST` (0x09) to all child nodes.

**Storage**: Child nodes store received IP and port in NVS namespace "udp_bridge" with keys "server_ip" and "server_port".

**Fallback**: If cached IP is invalid or connection test fails, new root node falls back to normal discovery.

### Phase 3: Registration

Registration establishes the initial connection between root node and external web server. The root node identifies itself and provides information about its capabilities.

**Purpose**: Register root node identity and capabilities with external server.

**When**:
- After successful discovery and embedded web server startup
- When node becomes root (if external server discovered)
- After IP address change or network reconnection

**How**: Send registration packet (UDP command 0xE0) with root node information, wait for ACK (0xE3).

**Retry Logic**: Up to 3 attempts with exponential backoff (1s, 2s, 4s delays).

**Success**: After successful ACK, start heartbeat and state update tasks.

### Phase 4: Heartbeat and Keepalive

Heartbeat maintains the registration by periodically indicating the root node is still active. This allows the external server to detect when root nodes go offline.

**Purpose**: Maintain registration and detect offline root nodes.

**When**: After successful registration, continues until node loses root role or registration fails.

**How**: Periodic FreeRTOS task sends heartbeat packets (command 0xE1) every 45 seconds.

**Fire-and-Forget**: No ACK required, packet loss is acceptable.

**Automatic Stop**: Task exits if node is no longer root or registration is lost.

### Phase 5: State Updates

State updates periodically share comprehensive mesh network information with the external server, including node topology, routing tables, and network status.

**Purpose**: Provide external server with current mesh network state for monitoring and management.

**When**: After successful registration, continues until node loses root role or registration fails.

**How**: Periodic FreeRTOS task collects mesh state and sends state update packets (command 0xE2) every 60 seconds.

**Fire-and-Forget**: No ACK required, packet loss is acceptable.

**Automatic Stop**: Task exits if node is no longer root or registration is lost.

### Phase 6: Disconnection Detection and Recovery

Disconnection detection runs on the server side to detect when root nodes go offline. The ESP32 root node continues operating normally regardless of external server status.

**Purpose**: Detect offline root nodes and clean up stale registrations on the server.

**When**: Continuously monitors all registrations for heartbeat timeout or UDP communication failures.

**How**: Periodic monitoring task checks last heartbeat/state update timestamps and UDP failure counts.

**Recovery**: When root node re-registers, system automatically recovers and resumes normal communication.

## IP Broadcast to Mesh Network

### Overview

IP broadcast is an optimization that allows child nodes to cache the external web server address, enabling faster root node transitions. When a child node becomes root, it can immediately use the cached address instead of waiting for discovery.

**Command ID**: `MESH_CMD_WEBSERVER_IP_BROADCAST` (0x09)

**Direction**: Root node → All child nodes

**Frequency**: Once per successful discovery (no periodic broadcast)

### Payload Structure

```c
typedef struct {
    uint8_t ip[4];        // IPv4 address (network byte order)
    uint16_t port;        // UDP port number (network byte order)
    uint32_t timestamp;   // Optional: Unix timestamp for expiration (network byte order)
} __attribute__((packed)) mesh_webserver_ip_broadcast_t;
```

**Size**:
- Minimum: 6 bytes (IP + port)
- With timestamp: 10 bytes (IP + port + timestamp)

### Implementation - ESP32 Side

#### Broadcast Function

```c
void mesh_udp_bridge_broadcast_server_ip(const char *ip, uint16_t port)
```

**Location**: `src/mesh_udp_bridge.c`

**Process**:
1. Validate IP address and port
2. Convert IP string to 4-byte array (network byte order)
3. Convert port to network byte order (`htons()`)
4. Get routing table using `esp_mesh_get_routing_table()`
5. Loop through all child nodes
6. For each child node:
   - Prepare `mesh_data_t` structure
   - Set command ID as first byte: `MESH_CMD_WEBSERVER_IP_BROADCAST`
   - Copy payload structure after command ID
   - Call `esp_mesh_send()` with child node address
   - Handle send errors (log but continue)

**Error Handling**: Broadcast failures are logged but do not affect discovery or registration. Broadcast is optimization only.

**Integration**: Called immediately after successful discovery in `mesh_root_ip_callback()`.

#### Reception Handler - Child Nodes

**Location**: `src/mesh_child.c` in `esp_mesh_p2p_rx_main()`

**Process**:
1. Receive mesh command `MESH_CMD_WEBSERVER_IP_BROADCAST`
2. Check data size (minimum 7 bytes: command + 6 bytes payload)
3. Extract payload after command ID
4. Parse IP address from 4-byte array to string format
5. Parse port from network byte order (`ntohs()`)
6. Validate IP address and port range
7. Store in NVS namespace "udp_bridge":
   - Key "server_ip": IP address string
   - Key "server_port": Port number (uint16_t)
   - Key "server_ip_timestamp": Timestamp (if present, uint32_t)
8. Log reception

**Error Handling**: Parsing errors are logged but do not affect mesh operation. Invalid data is discarded.

### Cached IP Usage

#### Check Cached IP Function

```c
bool mesh_udp_bridge_use_cached_ip(void)
```

**Location**: `src/mesh_udp_bridge.c`

**Process**:
1. Open NVS namespace "udp_bridge"
2. Read "server_ip" and "server_port" keys
3. Check cache expiration (if timestamp present)
4. Test UDP connection to cached IP and port
5. If connection test succeeds, set server address for registration
6. Return true if cached IP is valid and used, false otherwise

**Integration**: Called before mDNS discovery in discovery flow. If cached IP is valid, discovery can be skipped.

#### Connection Test Function

```c
bool mesh_udp_bridge_test_connection(const char *ip, uint16_t port)
```

**Location**: `src/mesh_udp_bridge.c`

**Process**:
1. Create UDP socket
2. Set socket timeout (1-2 seconds)
3. Attempt to send test packet to IP and port
4. Check if send succeeds
5. Close socket
6. Return true if connection test succeeds, false otherwise

**Purpose**: Validate cached IP address is still reachable before using it.

## Root Node Registration

### Overview

Registration establishes the initial connection between ESP32 root node and external web server. The registration includes root node identification (IP address, mesh ID), capabilities (node count, firmware version), and timestamp information.

**UDP Command**: 0xE0 (Registration Request)

**ACK Command**: 0xE3 (Registration ACK)

**ACK Required**: Yes (with retry logic)

**When**:
- After successful discovery and embedded web server startup
- When node becomes root (if external server discovered)
- After network reconnection or IP change

### Registration Payload Structure

```c
typedef struct {
    uint8_t root_ip[4];              // IPv4 address (network byte order)
    uint8_t mesh_id[6];              // Mesh ID (6 bytes)
    uint8_t node_count;              // Number of connected nodes (uint8_t, max 255)
    uint8_t firmware_version_len;    // Length of version string
    char firmware_version[32];       // Version string (null-terminated, max 31 chars)
    uint32_t timestamp;              // Unix timestamp (network byte order)
} __attribute__((packed)) mesh_registration_payload_t;
```

**Size**: Variable (minimum 16 bytes, maximum 47 bytes depending on version string length)

### Registration Flow

#### Step 1: Check Prerequisites

1. Check if external server is discovered (or use cached address)
2. Check if node is root (`esp_mesh_is_root()`)
3. Check if already registered (optional, to avoid duplicate registration)

#### Step 2: Gather Registration Data

1. **Root IP Address**: Use `esp_netif_get_ip_info()` to get current IP address
2. **Mesh ID**: Read from `MESH_CONFIG_MESH_ID` in mesh config
3. **Node Count**: Use `esp_mesh_get_routing_table_size() - 1` or `mesh_get_node_count()`
4. **Firmware Version**: Use `mesh_version_get_string()` to get version string
5. **Timestamp**: Use `time(NULL)` or `esp_timer_get_time() / 1000000` for Unix timestamp

#### Step 3: Build Registration Payload

1. Validate all gathered data:
   - IP address must not be 0.0.0.0
   - Mesh ID must not be all zeros
   - Firmware version must not be empty
   - Version length must fit in payload (max 31 chars)
2. Convert multi-byte values to network byte order:
   - IP address (already in network byte order from `inet_aton()`)
   - Timestamp: `htonl()`
3. Copy all data into payload structure

#### Step 4: Send Registration Packet

1. Construct UDP packet:
   - Command ID: 0xE0
   - Length: payload size (2 bytes, network byte order)
   - Payload: registration payload structure
   - Checksum: 16-bit sum of all bytes (optional)
2. Send packet to discovered server IP and UDP port
3. Log registration attempt

#### Step 5: Wait for ACK

1. Set up UDP receive with 5-second timeout
2. Wait for ACK packet (command 0xE3)
3. Parse ACK status byte:
   - Status 0: Success
   - Status 1: Failure
4. Handle ACK timeout or parsing errors

#### Step 6: Retry Logic

If ACK timeout or failure:
1. Retry up to 3 times
2. Use exponential backoff: 1s, 2s, 4s delays between retries
3. Log each retry attempt
4. Give up after 3 attempts (registration failure is not fatal)

#### Step 7: On Success

After successful ACK:
1. Mark registration as complete
2. Start heartbeat task (`mesh_udp_bridge_start_heartbeat()`)
3. Start state update task (`mesh_udp_bridge_start_state_updates()`)
4. Log registration success

### Implementation - ESP32 Side

#### Registration Function

```c
esp_err_t mesh_udp_bridge_register(void)
```

**Location**: `src/mesh_udp_bridge.c`

**Parameters**: None (all data gathered internally)

**Returns**: `ESP_OK` on success, error code on failure

**Process**:
1. Check if external server discovered (or use cached address)
2. Check if root node
3. Build registration payload
4. Send registration packet with retry logic
5. Wait for ACK with timeout
6. Start heartbeat and state update tasks on success

**Error Handling**:
- External server not discovered: Return `ESP_ERR_NOT_FOUND` (not an error condition)
- Not root node: Return `ESP_ERR_INVALID_STATE` (not an error condition)
- Payload construction failure: Return error code, don't retry
- UDP send failure: Retry with backoff
- ACK timeout: Retry with backoff
- Registration failure: Log warning, continue normal operation

**Integration Points**:
- Called after successful discovery in `mesh_root_ip_callback()`
- Called on root role change in `mesh_common_event_handler()`
- Called via FreeRTOS task for non-blocking execution

#### Registration ACK Handler

```c
esp_err_t mesh_udp_bridge_wait_for_registration_ack(uint32_t timeout_ms, bool *success)
```

**Location**: `src/mesh_udp_bridge.c`

**Parameters**:
- `timeout_ms`: Timeout in milliseconds
- `success`: Output pointer for success status

**Returns**: `ESP_OK` if ACK received, `ESP_ERR_TIMEOUT` if timeout, error code on failure

**Process**:
1. Set up UDP socket receive with timeout
2. Wait for incoming UDP packet
3. Check packet command ID (should be 0xE3)
4. Extract status byte (0=success, 1=failure)
5. Set `*success` based on status byte
6. Return appropriate error code

### Implementation - Server Side

#### Registration Handler

**Location**: `lyktparad-server/lib/registration.js`

**Process**:
1. Receive UDP registration packet (command 0xE0)
2. Parse registration payload:
   - Extract root IP (4 bytes, network byte order)
   - Extract mesh ID (6 bytes)
   - Extract node count (1 byte)
   - Extract firmware version length (1 byte)
   - Extract firmware version string (N bytes)
   - Extract timestamp (4 bytes, network byte order)
3. Validate payload:
   - Check IP address is valid
   - Check mesh ID is valid
   - Check version string is not empty
4. Check if registration already exists (by mesh ID)
5. If exists:
   - Update existing registration (IP change detection)
   - Clear failure counters
   - Mark as online
6. If new:
   - Create new registration
7. Store registration with timestamp
8. Send ACK packet (command 0xE3) with status 0 (success)

**IP Change Detection**: If mesh ID already registered but IP address differs, update IP address and mark as re-registration.

**Registration Storage**: Stored in memory Map (can be extended to database) keyed by mesh ID (hex string).

### Registration State Management

#### Registration Status Tracking

**Static Variables**:
- `s_registration_complete`: Boolean flag indicating successful registration
- `s_server_registered`: Boolean flag indicating server address is known
- `s_server_addr`: Server address structure (IP and port)

#### Registration Status Functions

```c
bool mesh_udp_bridge_is_registered(void)
```

**Returns**: `true` if registration complete, `false` otherwise

**Usage**: Check registration status before starting heartbeat/state updates or before sending API commands.

```c
void mesh_udp_bridge_set_registration(bool registered, uint8_t *ip, uint16_t port)
```

**Purpose**: Set server registration status and address (called after discovery or from cached IP).

### Integration Points

#### After Discovery

**Location**: `src/mesh_root.c` in `mesh_root_ip_callback()`

**Process**:
1. Discovery succeeds (mDNS or UDP broadcast)
2. Cache discovered server address
3. Call `mesh_udp_bridge_set_registration(true, ip, port)`
4. Broadcast server IP to child nodes
5. Call `mesh_udp_bridge_register()` in non-blocking task

#### On Root Role Change

**Location**: `src/mesh_common.c` in `mesh_common_event_handler()`

**Process**:
1. Node becomes root (`MESH_EVENT_LAYER_CHANGE`)
2. Check if external server discovered
3. If discovered, call `mesh_udp_bridge_register()` in non-blocking task
4. If already registered, start heartbeat and state update tasks

#### Non-Blocking Execution

Registration is always executed in a FreeRTOS task to avoid blocking mesh operations or embedded web server:

```c
static void registration_task(void *pvParameters)
{
    esp_err_t err = mesh_udp_bridge_register();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Registration failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}
```

## Heartbeat and Keepalive

### Overview

Heartbeat is a periodic keepalive mechanism that maintains registration and indicates root node is still active. Heartbeat uses fire-and-forget semantics (no ACK required) to minimize root node load.

**UDP Command**: 0xE1 (Heartbeat)

**ACK Required**: No (fire-and-forget)

**Interval**: 45 seconds (configurable, range: 30-60 seconds)

**When**: After successful registration, continues until node loses root role or registration fails

### Heartbeat Payload Structure

```c
typedef struct {
    uint32_t timestamp;    // Unix timestamp (network byte order)
    uint8_t node_count;    // Number of connected nodes (optional)
} __attribute__((packed)) mesh_heartbeat_payload_t;
```

**Size**: 5 bytes (4 bytes timestamp + 1 byte node count)

### Heartbeat Flow

#### Step 1: Check Prerequisites

1. Check if registered (`mesh_udp_bridge_is_registered()`)
2. Check if root node (`esp_mesh_is_root()`)
3. If either check fails, return early (not an error)

#### Step 2: Build Heartbeat Payload

1. Get current timestamp:
   - Use `time(NULL)` for Unix timestamp
   - Fallback to `esp_timer_get_time() / 1000000` if `time()` fails
2. Convert timestamp to network byte order (`htonl()`)
3. Get node count:
   - Use `mesh_get_node_count()` or `esp_mesh_get_routing_table_size() - 1`
   - Ensure count fits in uint8_t (max 255)
4. Fill payload structure

#### Step 3: Send Heartbeat Packet

1. Construct UDP packet:
   - Command ID: 0xE1
   - Length: payload size (2 bytes, network byte order) = 5
   - Payload: heartbeat payload structure
2. Send packet to registered server IP and UDP port
3. Log send attempt (debug level)

#### Step 4: Error Handling

- UDP send failures: Log at debug level, continue (packet loss acceptable)
- No retry: Fire-and-forget, packet loss is acceptable
- Return `ESP_OK` even if send fails (heartbeat is optional)

### Implementation - ESP32 Side

#### Heartbeat Send Function

```c
esp_err_t mesh_udp_bridge_send_heartbeat(void)
```

**Location**: `src/mesh_udp_bridge.c`

**Parameters**: None

**Returns**: `ESP_OK` on send attempt (even if send fails), `ESP_ERR_INVALID_STATE` if not registered or not root

**Process**:
1. Check registration and root status
2. Build heartbeat payload
3. Send UDP packet (fire-and-forget)
4. Log send attempt (debug level)
5. Return `ESP_OK` regardless of send result

#### Heartbeat Task Function

```c
static void mesh_udp_bridge_heartbeat_task(void *pvParameters)
```

**Location**: `src/mesh_udp_bridge.c`

**Process**:
1. Loop continuously:
   - Check if still root and registered
   - If not, exit task
   - Send heartbeat packet
   - Sleep for interval (45 seconds)
   - Repeat
2. On exit:
   - Clear task handle
   - Set running flag to false
   - Delete task

**Task Configuration**:
- Stack size: 2048 bytes
- Priority: 1 (low priority, don't interfere with mesh)
- Name: "udp_heartbeat"

#### Heartbeat Start/Stop Functions

```c
void mesh_udp_bridge_start_heartbeat(void)
```

**Process**:
1. Check if task already running (return early if yes)
2. Check if registered (return early if not)
3. Check if root node (return early if not)
4. Create FreeRTOS task
5. Store task handle
6. Set running flag
7. Log task start

```c
void mesh_udp_bridge_stop_heartbeat(void)
```

**Process**:
1. Check if task is running
2. Delete task using `vTaskDelete()`
3. Clear task handle
4. Set running flag to false
5. Log task stop

**Integration**:
- Started automatically after successful registration
- Stopped automatically when node loses root role
- Stopped automatically when registration is lost

### Implementation - Server Side

#### Heartbeat Handler

**Location**: `lyktparad-server/lib/udp-commands.js`

**Process**:
1. Receive UDP heartbeat packet (command 0xE1)
2. Parse heartbeat payload:
   - Extract timestamp (4 bytes, network byte order)
   - Extract node count (1 byte)
3. Find registration by source IP address (or mesh ID if available)
4. Update last heartbeat timestamp:
   - Call `updateLastHeartbeat(meshIdHex, timestamp)`
   - Reset UDP failure count
   - Mark registration as online
5. Log heartbeat reception (debug level)

**No ACK Sent**: Heartbeat is fire-and-forget, server does not send response.

**Timestamp Update**: Server updates `last_heartbeat` field in registration with received timestamp (or current time if timestamp invalid).

**Failure Count Reset**: Successful heartbeat reception resets UDP failure count to 0, clearing any offline status.

## State Updates

### Overview

State updates periodically share comprehensive mesh network information with the external server, including node topology, routing tables, and network status. State updates use fire-and-forget semantics (no ACK required) to minimize root node load.

**UDP Command**: 0xE2 (State Update)

**ACK Required**: No (fire-and-forget)

**Interval**: 60 seconds (configurable)

**When**: After successful registration, continues until node loses root role or registration fails

### State Update Payload Structure

The state update payload is variable-length binary format:

```
[Root IP: 4 bytes] [Mesh ID: 6 bytes] [Timestamp: 4 bytes] [Node Count: 1 byte]
[Node Entries: N * (MAC: 6 bytes, IP: 4 bytes, Layer: 1 byte, Parent: 6 bytes)]
[Active Sequences: 1 byte] [Sequence Entries: M * (ID: 2 bytes, Status: 2 bytes)]
[OTA Status: 1 byte] [Current Mode: 1 byte]
```

**Size**: Variable (depends on number of nodes and sequences, typical: 100-500 bytes)

### State Collection

#### State Collection Function

```c
esp_err_t mesh_udp_bridge_collect_state(mesh_state_data_t *state)
```

**Location**: `src/mesh_udp_bridge.c`

**Process**:
1. Get root IP address
2. Get mesh ID
3. Get current timestamp
4. Get routing table using `esp_mesh_get_routing_table()`
5. For each node in routing table:
   - Extract MAC address
   - Extract IP address
   - Extract layer
   - Extract parent MAC address
   - Create node entry
6. Get active sequence information
7. Get OTA status
8. Get current mode
9. Fill state structure

**Memory Management**: Node entries are dynamically allocated. Caller must free node list after use.

### State Update Flow

#### Step 1: Check Prerequisites

1. Check if registered (`mesh_udp_bridge_is_registered()`)
2. Check if root node (`esp_mesh_is_root()`)
3. If either check fails, return early (not an error)

#### Step 2: Collect Mesh State

1. Call `mesh_udp_bridge_collect_state()` to gather state information
2. Handle collection errors (log and continue)
3. Allocate buffer for state payload

#### Step 3: Build State Payload

1. Pack state data into binary payload:
   - Root IP (4 bytes, network byte order)
   - Mesh ID (6 bytes)
   - Timestamp (4 bytes, network byte order)
   - Node count (1 byte)
   - Node entries (variable length)
   - Active sequences (1 byte)
   - Sequence entries (variable length)
   - OTA status (1 byte)
   - Current mode (1 byte)
2. Calculate payload size

#### Step 4: Send State Update Packet

1. Construct UDP packet:
   - Command ID: 0xE2
   - Length: payload size (2 bytes, network byte order)
   - Payload: state payload buffer
2. Send packet to registered server IP and UDP port
3. Log send attempt (debug level)

#### Step 5: Error Handling

- State collection failures: Log warning, continue (skip this update)
- Payload construction failures: Log warning, free buffers, continue
- UDP send failures: Log at debug level, continue (packet loss acceptable)
- No retry: Fire-and-forget, packet loss is acceptable
- Return `ESP_OK` even if send fails (state updates are optional)

### Implementation - ESP32 Side

#### State Update Send Function

```c
esp_err_t mesh_udp_bridge_send_state_update(uint8_t *payload, size_t payload_size)
```

**Location**: `src/mesh_udp_bridge.c`

**Parameters**:
- `payload`: Binary state payload buffer
- `payload_size`: Payload size in bytes

**Returns**: `ESP_OK` on send attempt (even if send fails), `ESP_ERR_INVALID_STATE` if not registered or not root

**Process**:
1. Check registration and root status
2. Construct UDP packet
3. Send packet (fire-and-forget)
4. Log send attempt (debug level)
5. Return `ESP_OK` regardless of send result

#### State Update Task Function

```c
static void mesh_udp_bridge_state_update_task(void *pvParameters)
```

**Location**: `src/mesh_udp_bridge.c`

**Process**:
1. Loop continuously:
   - Check if still root and registered
   - If not, exit task
   - Collect mesh state
   - Build state payload
   - Send state update packet
   - Free buffers
   - Sleep for interval (60 seconds)
   - Repeat
2. On exit:
   - Clear task handle
   - Set running flag to false
   - Delete task

**Task Configuration**:
- Stack size: 4096 bytes (larger than heartbeat due to state collection)
- Priority: 1 (low priority, don't interfere with mesh)
- Name: "udp_state_update"

#### State Update Start/Stop Functions

```c
void mesh_udp_bridge_start_state_updates(void)
void mesh_udp_bridge_stop_state_updates(void)
```

**Similar to heartbeat start/stop functions**, with same integration points.

### Implementation - Server Side

#### State Update Handler

**Location**: `lyktparad-server/lib/udp-commands.js`

**Process**:
1. Receive UDP state update packet (command 0xE2)
2. Parse state update payload:
   - Extract root IP, mesh ID, timestamp
   - Extract node count and node entries
   - Extract sequence information
   - Extract OTA status and current mode
3. Find registration by source IP address (or mesh ID)
4. Update last state update timestamp:
   - Call `updateLastStateUpdate(meshIdHex, timestamp)`
   - Reset UDP failure count
   - Mark registration as online
5. Store state information for HTTP GET endpoint
6. Log state update reception (debug level)

**State Storage**: State information is stored in memory (can be extended to database) for HTTP GET `/api/state` endpoint.

**No ACK Sent**: State updates are fire-and-forget, server does not send response.

## Disconnection Detection and Recovery

### Overview

Disconnection detection runs on the server side to detect when root nodes go offline. The ESP32 root node continues operating normally regardless of external server status. Disconnection detection enables the server to clean up stale registrations and inform users when root nodes are unavailable.

**ESP32 Side**: No disconnection handling - root node continues operating normally.

**Server Side**: Detects disconnection via heartbeat timeout or UDP communication failures.

**Recovery**: Automatic when root node re-registers.

### Disconnection Detection Methods

#### Method 1: Heartbeat Timeout

**How**: Monitor `last_heartbeat` timestamp for each registration.

**Timeout**: 3 minutes (configurable, range: 2-5 minutes)

**Detection**: If time since last heartbeat exceeds timeout, mark registration as offline.

**Fallback**: If no heartbeat ever received, use `last_state_update` timestamp instead.

#### Method 2: UDP Communication Failure

**How**: Track UDP send failures in API proxy.

**Threshold**: 3 consecutive failures (configurable)

**Detection**: If failure count exceeds threshold, mark registration as offline.

**Reset**: Failure count reset on successful heartbeat, state update, or re-registration.

#### Method 3: State Update Timeout

**How**: Monitor `last_state_update` timestamp (secondary check).

**Usage**: Used as fallback if no heartbeat received (both update registration activity).

### Implementation - Server Side

#### Heartbeat Timeout Detection

**Location**: `lyktparad-server/lib/disconnection-detection.js`

**Function**: `isHeartbeatTimeout(registration)`

**Process**:
1. Check if registration has `last_heartbeat` or `last_state_update`
2. If neither exists:
   - Check if registration time exceeds timeout
   - Return timeout if registered more than timeout ago
3. Use `last_heartbeat` if available, otherwise `last_state_update`
4. Calculate time since last activity
5. Return true if time exceeds timeout, false otherwise

**Configuration**: Timeout configurable via environment variable `HEARTBEAT_TIMEOUT_MS` (default: 180000 ms = 3 minutes).

#### UDP Failure Tracking

**Location**: `lyktparad-server/lib/disconnection-detection.js`

**Function**: `isUdpFailureThresholdExceeded(registration)`

**Process**:
1. Get `udp_failure_count` from registration
2. Compare with threshold (default: 3)
3. Return true if threshold exceeded, false otherwise

**Increment**: Failure count incremented in API proxy on UDP send failures.

**Reset**: Failure count reset on successful heartbeat, state update, or re-registration.

#### Monitoring Function

**Location**: `lyktparad-server/lib/disconnection-detection.js`

**Function**: `monitorHeartbeatTimeout()`

**Process**:
1. Get all registrations
2. For each registration:
   - Check if heartbeat timeout exceeded
   - Check if UDP failure threshold exceeded
   - If either condition true and not already marked offline:
     - Mark registration as offline
     - Log disconnection event
     - Add to timed out list
3. Return list of newly timed out registrations

**Periodic Execution**: Called every 30 seconds (configurable) by monitoring interval.

#### Cleanup Function

**Location**: `lyktparad-server/lib/disconnection-detection.js`

**Function**: `cleanupStaleRegistrations(forceCleanup = false)`

**Process**:
1. Get all registrations
2. For each registration:
   - If marked offline and time since last activity > 2x timeout:
     - Remove registration
     - Log cleanup event
     - Add to cleaned up list
   - If no activity ever and registration time > 2x timeout:
     - Remove registration
     - Log cleanup event
     - Add to cleaned up list
3. Return list of cleaned up registration mesh IDs

**Cleanup Timeout**: 2x heartbeat timeout (default: 6 minutes) to allow for network interruptions.

**Periodic Execution**: Called every 30 seconds (configurable) by monitoring interval.

#### Monitoring Start/Stop

**Location**: `lyktparad-server/lib/disconnection-detection.js`

**Functions**: `startMonitoring()`, `stopMonitoring()`

**Process**:
1. Create setInterval for periodic monitoring
2. Interval calls:
   - `monitorHeartbeatTimeout()`
   - `cleanupStaleRegistrations()`
3. Interval: 30 seconds (configurable via `CLEANUP_CHECK_INTERVAL_MS`)

**Integration**: Started automatically when server starts, stopped on server shutdown.

### Error Handling - Server Side

#### HTTP Error Responses

When root node is offline or not registered, API proxy returns appropriate HTTP error codes:

**503 Service Unavailable**: Root node is offline (heartbeat timeout or UDP failures)

**404 Not Found**: No registration exists for mesh network

**Error Response Format**:
```json
{
  "error": "Root node unavailable",
  "message": "The root node is currently offline or unreachable",
  "code": 503,
  "suggestion": "You can access the root node directly via its IP address"
}
```

#### Connection Status API

**Endpoint**: `GET /api/connection/status`

**Response Format**:
```json
{
  "connected": false,
  "last_seen": "2025-01-15T10:30:00Z",
  "root_node_ip": "192.168.1.100",
  "mesh_id": "aa:bb:cc:dd:ee:ff",
  "status": "offline"
}
```

**Usage**: Web UI polls this endpoint to display connection status to users.

### Recovery and Re-registration

#### Automatic Recovery

When root node re-registers (e.g., after network restart, IP change, or temporary disconnection):

1. Registration handler receives new registration packet
2. Check if mesh ID already registered
3. If exists:
   - Update existing registration (IP change detection)
   - Clear offline status
   - Reset UDP failure count
   - Update timestamp
   - Log re-registration event
4. Send ACK packet (status 0)
5. Server resumes normal communication with root node

**No Manual Intervention**: Recovery is automatic - no user action required.

**IP Change Handling**: If root node IP changes but mesh ID is same, update IP address and mark as re-registration.

#### Re-registration Triggers

Root nodes re-register in these scenarios:

1. **Network Reconnection**: After network disconnect/reconnect
2. **IP Address Change**: After DHCP IP renewal or network reconfiguration
3. **Root Role Change**: When node becomes root again after losing root role
4. **Manual Restart**: After ESP32 restart or firmware update

**Process**: Same as initial registration - send registration packet, wait for ACK, start heartbeat/state updates.

## Error Handling and Graceful Degradation

### ESP32 Side Error Handling

#### Registration Errors

- **External Server Not Discovered**: Return `ESP_ERR_NOT_FOUND` (normal case, not an error)
- **Not Root Node**: Return `ESP_ERR_INVALID_STATE` (normal case, not an error)
- **Payload Construction Failure**: Log error, return error code, don't retry
- **UDP Send Failure**: Retry with exponential backoff (up to 3 attempts)
- **ACK Timeout**: Retry with exponential backoff (up to 3 attempts)
- **Registration Failure**: Log warning, continue normal operation (embedded web server unaffected)

#### Heartbeat Errors

- **Not Registered**: Return `ESP_ERR_INVALID_STATE` (normal case, not an error)
- **Not Root Node**: Return `ESP_ERR_INVALID_STATE` (normal case, not an error)
- **UDP Send Failure**: Log at debug level, continue (packet loss acceptable)
- **No Retry**: Fire-and-forget, packet loss is acceptable

#### State Update Errors

- **Not Registered**: Return `ESP_ERR_INVALID_STATE` (normal case, not an error)
- **Not Root Node**: Return `ESP_ERR_INVALID_STATE` (normal case, not an error)
- **State Collection Failure**: Log warning, skip this update, continue
- **UDP Send Failure**: Log at debug level, continue (packet loss acceptable)
- **No Retry**: Fire-and-forget, packet loss is acceptable

#### IP Broadcast Errors

- **Broadcast Failure**: Log warning, continue (optimization only)
- **Child Node Reception Failure**: Log at child node, discard invalid data
- **NVS Storage Failure**: Log error, continue (cache is optional)

### Server Side Error Handling

#### Registration Errors

- **Invalid Payload**: Send ACK with status 1 (failure), log error
- **Parsing Errors**: Send ACK with status 1 (failure), log error
- **Storage Errors**: Send ACK with status 1 (failure), log error

#### Disconnection Errors

- **Heartbeat Timeout**: Mark as offline, log warning, return 503 on API requests
- **UDP Communication Failure**: Increment failure count, mark offline if threshold exceeded
- **Cleanup Errors**: Log error, continue (don't affect other registrations)

### Graceful Degradation Principles

1. **Embedded Server Always Works**: Embedded web server always starts and continues operating regardless of external server communication status.

2. **Non-Blocking**: All external server communication is non-blocking and runs in background tasks.

3. **Failures Don't Cascade**: External server communication failures do not affect mesh operation, embedded web server, or other system components.

4. **Fire-and-Forget for Monitoring**: Heartbeat and state updates use fire-and-forget semantics - packet loss is acceptable.

5. **Retry with Limits**: Registration retries are limited (3 attempts) to avoid indefinite blocking.

6. **Optional Features**: All external server features are optional enhancements - system functions without them.

## Implementation Details

### UDP Socket Management

#### Socket Initialization

```c
static esp_err_t init_udp_socket(void)
```

**Location**: `src/mesh_udp_bridge.c`

**Process**:
1. Check if socket already initialized (reuse if yes)
2. Create UDP socket using `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)`
3. Set socket options:
   - `SO_REUSEADDR`: Allow address reuse
   - Receive timeout: 5 seconds (for ACK wait)
4. Store socket file descriptor in static variable
5. Return `ESP_OK` on success

**Socket Reuse**: Socket is created once and reused for all UDP communication (registration, heartbeat, state updates).

#### Socket Cleanup

Socket is closed when UDP bridge module is deinitialized (optional cleanup on system shutdown).

### Task Management

#### Task Lifecycle

1. **Creation**: Tasks created on demand when needed (after registration, on role change)
2. **Execution**: Tasks run continuously until exit condition
3. **Exit Conditions**:
   - Node loses root role
   - Registration is lost
   - Manual stop requested
4. **Cleanup**: Tasks delete themselves using `vTaskDelete(NULL)`

#### Task Synchronization

Tasks check registration and root status in their main loops. No external synchronization needed - tasks exit automatically when conditions are no longer met.

### Memory Management

#### Dynamic Allocation

- **Registration Payload**: Allocated on stack (small, fixed size)
- **Heartbeat Payload**: Allocated on stack (small, fixed size)
- **State Payload**: Allocated dynamically (variable size, depends on node count)
- **UDP Packets**: Allocated dynamically for each send

#### Memory Cleanup

- **State Payload**: Freed after send (in state update task)
- **UDP Packets**: Freed after send (in all send functions)
- **Node Lists**: Freed after state collection (in state update task)

### Network Byte Order

All multi-byte values use network byte order (big-endian) for consistency:

- **IP Addresses**: Already in network byte order from `inet_aton()`
- **Port Numbers**: Convert using `htons()` / `ntohs()`
- **Timestamps**: Convert using `htonl()` / `ntohl()`
- **Payload Length**: Convert using `htons()` / `ntohs()`

## API Reference

### ESP32 Side Functions

#### Registration Functions

```c
esp_err_t mesh_udp_bridge_register(void)
```

Register root node with external web server.

**Returns**: `ESP_OK` on success, error code on failure

**Note**: Called automatically after discovery or on role change.

```c
bool mesh_udp_bridge_is_registered(void)
```

Check if root node is registered with external server.

**Returns**: `true` if registered, `false` otherwise

#### Heartbeat Functions

```c
esp_err_t mesh_udp_bridge_send_heartbeat(void)
```

Send a single heartbeat packet (used by heartbeat task).

**Returns**: `ESP_OK` on send attempt, `ESP_ERR_INVALID_STATE` if not registered or not root

```c
void mesh_udp_bridge_start_heartbeat(void)
```

Start periodic heartbeat task (called automatically after registration).

```c
void mesh_udp_bridge_stop_heartbeat(void)
```

Stop periodic heartbeat task (called automatically on role loss).

#### State Update Functions

```c
esp_err_t mesh_udp_bridge_send_state_update(uint8_t *payload, size_t payload_size)
```

Send state update packet (used by state update task).

**Returns**: `ESP_OK` on send attempt, `ESP_ERR_INVALID_STATE` if not registered or not root

```c
void mesh_udp_bridge_start_state_updates(void)
```

Start periodic state update task (called automatically after registration).

```c
void mesh_udp_bridge_stop_state_updates(void)
```

Stop periodic state update task (called automatically on role loss).

#### IP Broadcast Functions

```c
void mesh_udp_bridge_broadcast_server_ip(const char *ip, uint16_t port)
```

Broadcast external server IP and port to all child nodes.

**Parameters**:
- `ip`: Server IP address string (e.g., "192.168.1.100")
- `port`: Server UDP port number

**Note**: Called automatically after successful discovery.

```c
bool mesh_udp_bridge_use_cached_ip(void)
```

Check if cached server IP is valid and use it for registration.

**Returns**: `true` if cached IP is valid and used, `false` otherwise

**Note**: Called automatically before discovery as optimization.

### Server Side Functions

#### Registration Functions

```javascript
function registerRootNode(root_ip, mesh_id, node_count, firmware_version, timestamp, udp_port)
```

Register or update root node registration.

**Returns**: Registration object

```javascript
function getRegisteredRootNode(meshIdHex)
```

Get registered root node by mesh ID.

**Returns**: Registration object or `null` if not found

```javascript
function updateLastHeartbeat(meshIdHex, timestamp)
```

Update last heartbeat timestamp for registration.

**Returns**: `true` if updated, `false` if not found

```javascript
function updateLastStateUpdate(meshIdHex, timestamp)
```

Update last state update timestamp for registration.

**Returns**: `true` if updated, `false` if not found

#### Disconnection Detection Functions

```javascript
function monitorHeartbeatTimeout()
```

Monitor all registrations for heartbeat timeout.

**Returns**: Array of registrations that have timed out

```javascript
function cleanupStaleRegistrations(forceCleanup)
```

Clean up stale registrations (exceeded timeout).

**Returns**: Array of cleaned up registration mesh IDs

```javascript
function startMonitoring()
```

Start periodic monitoring of heartbeat timeouts and cleanup.

```javascript
function stopMonitoring()
```

Stop periodic monitoring.

## Integration Points

### ESP32 Side Integration

#### After Discovery

**File**: `src/mesh_root.c`

**Function**: `mesh_root_ip_callback()`

**Process**:
1. Discovery succeeds
2. Cache server address
3. Set registration status
4. Broadcast server IP to child nodes
5. Register with external server (non-blocking task)

#### On Root Role Change

**File**: `src/mesh_common.c`

**Function**: `mesh_common_event_handler()`

**Process**:
1. Node becomes root
2. If external server discovered, register (non-blocking task)
3. If already registered, start heartbeat and state update tasks
4. Node loses root role: stop heartbeat and state update tasks

#### After Registration Success

**File**: `src/mesh_udp_bridge.c`

**Function**: `mesh_udp_bridge_register()`

**Process**:
1. Registration ACK received successfully
2. Mark registration as complete
3. Start heartbeat task
4. Start state update task

### Server Side Integration

#### UDP Command Handler

**File**: `lyktparad-server/lib/udp-commands.js`

**Process**:
1. Receive UDP packet
2. Parse command ID
3. Route to appropriate handler:
   - 0xE0: Registration handler
   - 0xE1: Heartbeat handler
   - 0xE2: State update handler
4. Update registration storage
5. Send ACK (if required)

#### Disconnection Monitoring

**File**: `lyktparad-server/server.js`

**Process**:
1. Server startup: call `startMonitoring()`
2. Periodic monitoring: detect timeouts and clean up stale registrations
3. Server shutdown: call `stopMonitoring()`

#### API Proxy Integration

**File**: `lyktparad-server/lib/http-udp-translator.js`

**Process**:
1. Receive HTTP API request
2. Check if root node is registered and online
3. If offline: return 503 Service Unavailable
4. If not registered: return 404 Not Found
5. If online: forward request via UDP, wait for response

---

## Summary

The Root Node Registration and Communication Lifecycle provides a complete optional communication system between ESP32 mesh root nodes and external web servers. The lifecycle consists of:

1. **IP Broadcast**: Optimization to pre-cache server address in child nodes
2. **Registration**: Initial connection establishment with retry logic
3. **Heartbeat**: Periodic keepalive to maintain registration
4. **State Updates**: Periodic mesh state information sharing
5. **Disconnection Detection**: Server-side monitoring for offline detection
6. **Recovery**: Automatic re-registration and recovery

All phases are optional and use graceful degradation - ESP32 root nodes always run their embedded web server and function independently, with external server communication serving as an enhancement only.
