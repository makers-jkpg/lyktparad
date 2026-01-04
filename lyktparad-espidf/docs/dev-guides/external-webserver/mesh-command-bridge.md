# Mesh Command Bridge - Development Guide

**Last Updated:** 2025-01-15

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Design Decisions](#design-decisions)
4. [Wrapper Function](#wrapper-function)
5. [UDP Forwarding Function](#udp-forwarding-function)
6. [Protocol Format](#protocol-format)
7. [Integration Points](#integration-points)
8. [Command Types Forwarded](#command-types-forwarded)
9. [Error Handling](#error-handling)
10. [Non-Blocking Behavior](#non-blocking-behavior)
11. [Implementation Details](#implementation-details)
12. [API Reference](#api-reference)
13. [Server-Side Handling](#server-side-handling)

## Overview

### Purpose

The Mesh Command Bridge enables transparent forwarding of all mesh commands from the ESP32 root node to an optional external web server via UDP. This allows the external server to monitor all mesh traffic and provides a foundation for future verification features to ensure correct messages are sent to the mesh network. The bridge acts as a transparent forwarder - all mesh commands sent via `esp_mesh_send()` are also forwarded to the external server (if registered) without modifying the original mesh command flow.

### Key Characteristics

**Transparent Forwarding**: The bridge intercepts mesh commands without modifying their structure or delivery. Original mesh operations continue unchanged.

**Completely Optional**: Bridge forwarding is completely optional and must not block or interfere with mesh operations. If the external server is unavailable or forwarding fails, mesh operations continue normally.

**Non-Blocking**: UDP forwarding is non-blocking and fire-and-forget. Mesh send operations return immediately without waiting for UDP forwarding to complete.

**Monitoring Only**: Forwarded commands are for monitoring purposes only. Packet loss is acceptable, and no acknowledgments are required.

**Root Node Only**: Only the root node forwards commands to the external server. Child nodes continue using standard mesh send operations.

## Architecture

### High-Level Flow

```
┌─────────────────────────────────────┐
│  Mesh Command Sender                │
│  (mesh_root.c, mode_sequence_root.c,│
│   mesh_ota.c)                       │
│                                     │
│  mesh_send_with_bridge() call       │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  mesh_send_with_bridge()            │
│  (Wrapper Function in mesh_common.c)│
│                                     │
│  1. Call esp_mesh_send()            │
│     (original mesh operation)       │
│  2. Check if root node              │
│  3. Check if server registered      │
│  4. Extract command ID & payload    │
│  5. Forward via UDP (non-blocking)  │
│  6. Return mesh send result         │
└──────────────┬──────────────────────┘
               │
               ├──────────────────────┐
               │                      │
               ▼                      ▼
┌─────────────────────┐   ┌─────────────────────┐
│  ESP-MESH Network    │   │  UDP Bridge         │
│  (Original Flow)     │   │  (Optional Forward) │
│                      │   │                     │
│  Child Nodes         │   │  External Server    │
└─────────────────────┘   └─────────────────────┘
```

### Module Structure

**Wrapper Function** (`mesh_common.c`):
- `mesh_send_with_bridge()` - Wraps `esp_mesh_send()` and adds optional UDP forwarding

**UDP Bridge Module** (`mesh_udp_bridge.c`):
- `mesh_udp_bridge_forward_mesh_command_async()` - Forwards mesh commands to external server via UDP

**Integration Points**:
- `mesh_root.c` - Heartbeat and RGB command sending
- `mode_sequence_root.c` - Sequence, beat, and control command sending
- `mesh_ota.c` - All OTA command sending

### Command Flow

1. **Command Origin**: Mesh commands originate from various modules (mesh_root.c, mode_sequence_root.c, mesh_ota.c)

2. **Wrapper Call**: All mesh sends go through `mesh_send_with_bridge()` wrapper function

3. **Mesh Send**: Wrapper calls original `esp_mesh_send()` first (mesh operation)

4. **Root Check**: Wrapper checks if current node is root node (only root forwards)

5. **Registration Check**: Wrapper checks if external server is registered

6. **Command Extraction**: Wrapper extracts command ID and payload from mesh data

7. **UDP Forward**: If registered, wrapper calls UDP forwarding function (non-blocking)

8. **Return Result**: Wrapper returns result from `esp_mesh_send()` (ignoring UDP forward result)

## Design Decisions

**Transparent Wrapper**: The wrapper function (`mesh_send_with_bridge()`) has identical signature to `esp_mesh_send()`, allowing drop-in replacement. All existing `esp_mesh_send()` calls are replaced with `mesh_send_with_bridge()`.

**Mesh First**: The wrapper always calls `esp_mesh_send()` first before any UDP forwarding. This ensures mesh operations are never delayed by UDP operations.

**Return Mesh Result**: The wrapper returns the result from `esp_mesh_send()` only, ignoring UDP forward results. This preserves original error handling behavior.

**Root Node Only**: Only root nodes forward commands to external server. Child nodes use the wrapper but don't forward (check performed in wrapper).

**Non-Blocking UDP**: UDP forwarding uses non-blocking socket operations and fire-and-forget semantics. Memory is allocated for packet construction and freed immediately after sending.

**No Retries**: UDP forward failures are logged at debug level but not retried. Packet loss is acceptable for monitoring purposes.

**Conditional Forwarding**: Forwarding only occurs if:
1. Current node is root node
2. External server is registered
3. Data is valid (not NULL, size > 0)

**Memory Safety**: UDP packet buffers are allocated on heap and freed immediately after sending. No persistent buffers or memory leaks.

## Wrapper Function

### Function Signature

```c
esp_err_t mesh_send_with_bridge(const mesh_addr_t *to,
                                 const mesh_data_t *data,
                                 int flag,
                                 const mesh_opt_t opt[],
                                 int opt_count);
```

**Location**: `src/mesh_common.c` (lines 948-988)

**Parameters**:
- `to`: Destination mesh address (NULL for broadcast)
- `data`: Mesh data to send (contains command ID and payload)
- `flag`: Mesh data flag (e.g., `MESH_DATA_P2P`)
- `opt`: Optional mesh options (can be NULL)
- `opt_count`: Number of optional mesh options

**Return Value**: Result from `esp_mesh_send()` call (ESP_OK on success, error code on failure)

### Implementation Logic

```c
esp_err_t mesh_send_with_bridge(const mesh_addr_t *to, const mesh_data_t *data,
                                 int flag, const mesh_opt_t opt[], int opt_count)
{
    /* Step 1: Call original esp_mesh_send() first (mesh operation) */
    esp_err_t mesh_result = esp_mesh_send(to, data, flag, opt, opt_count);

    /* Step 2: Only forward if this is the root node */
    if (!esp_mesh_is_root()) {
        return mesh_result;  /* Not root - don't forward */
    }

    /* Step 3: Validate data */
    if (data == NULL || data->data == NULL || data->size == 0) {
        return mesh_result;  /* Invalid data - don't forward */
    }

    /* Step 4: Extract command ID (first byte) */
    uint8_t mesh_cmd = data->data[0];

    /* Step 5: Extract payload (data after command ID) */
    const void *mesh_payload = NULL;
    size_t mesh_payload_len = 0;
    if (data->size > 1) {
        mesh_payload = &data->data[1];
        mesh_payload_len = data->size - 1;
    }

    /* Step 6: Forward command via UDP (non-blocking, fire-and-forget) */
    mesh_udp_bridge_forward_mesh_command_async(mesh_cmd, mesh_payload, mesh_payload_len);

    /* Step 7: Return mesh send result (ignore UDP forward result) */
    return mesh_result;
}
```

### Key Behaviors

1. **Mesh Operation First**: `esp_mesh_send()` is called first, ensuring mesh operations are never delayed

2. **Root Node Check**: Only root nodes forward commands. Child nodes return immediately after mesh send

3. **Data Validation**: Checks for NULL pointers and empty data before forwarding

4. **Command Extraction**: Extracts command ID (first byte) and payload (remaining bytes) from mesh data

5. **Non-Blocking Forward**: UDP forward is asynchronous and doesn't block mesh send return

6. **Return Mesh Result**: Returns result from `esp_mesh_send()` only, preserving original behavior

## UDP Forwarding Function

### Function Signature

```c
void mesh_udp_bridge_forward_mesh_command_async(uint8_t mesh_cmd,
                                                 const void *mesh_payload,
                                                 size_t mesh_payload_len);
```

**Location**: `src/mesh_udp_bridge.c` (lines 1123-1205)

**Parameters**:
- `mesh_cmd`: Mesh command ID (first byte of mesh data)
- `mesh_payload`: Mesh command payload (data after command ID)
- `mesh_payload_len`: Length of mesh payload in bytes

**Return Value**: None (void function)

### Implementation Logic

1. **Registration Check**: Returns early if external server is not registered (normal case, no logging)

2. **UDP Socket Initialization**: Initializes UDP socket if needed

3. **Packet Size Calculation**: Calculates UDP payload size and total packet size

4. **MTU Limit Check**: Checks if packet exceeds UDP MTU limit (1472 bytes), logs warning and returns if too large

5. **Buffer Allocation**: Allocates heap memory for UDP payload and complete packet

6. **Payload Construction**: Packs mesh command ID, payload length, payload data, and timestamp

7. **Packet Construction**: Packs UDP packet with command ID (0xE6), length, payload, and checksum

8. **Checksum Calculation**: Calculates 16-bit checksum of all bytes except checksum field

9. **Non-Blocking Send**: Sends UDP packet using `sendto()` (non-blocking, fire-and-forget)

10. **Error Logging**: Logs send failures at debug level (packet loss acceptable)

11. **Memory Cleanup**: Frees allocated buffers immediately after sending

### Key Behaviors

1. **Early Return on No Registration**: If server not registered, function returns immediately without logging (normal case)

2. **Dynamic Memory Allocation**: Allocates memory for packet construction, frees immediately after sending

3. **MTU Limit Enforcement**: Rejects packets larger than 1472 bytes to avoid fragmentation

4. **Network Byte Order**: All multi-byte values use network byte order (big-endian)

5. **Checksum Validation**: Calculates 16-bit sum checksum for packet integrity

6. **Fire-and-Forget**: No acknowledgments or retries. Packet loss is acceptable for monitoring

7. **Error Tolerance**: Send failures are logged at debug level but don't affect mesh operation

## Protocol Format

### UDP Packet Structure

All mesh command forward packets follow the standard UDP protocol format:

```
[Command ID: 1 byte] [Payload Length: 2 bytes] [Payload: N bytes] [Checksum: 2 bytes]
```

**Command ID**: `0xE6` (UDP_CMD_MESH_COMMAND_FORWARD)

**Payload Length**: Length of payload in network byte order (big-endian)

**Payload**: Mesh command data (see payload format below)

**Checksum**: 16-bit sum of all bytes (command ID + payload length + payload), network byte order

### Payload Format

The UDP payload contains the mesh command information:

```
[mesh_cmd_id: 1 byte] [mesh_payload_len: 2 bytes] [mesh_payload: N bytes] [timestamp: 4 bytes]
```

**Field Descriptions**:

- **mesh_cmd_id (1 byte)**: Mesh command ID (e.g., MESH_CMD_HEARTBEAT, MESH_CMD_SET_RGB)
- **mesh_payload_len (2 bytes)**: Length of mesh payload in network byte order
- **mesh_payload (N bytes)**: Actual mesh command payload (variable length, 0-1465 bytes)
- **timestamp (4 bytes)**: Unix timestamp in network byte order (for ordering/sequencing)

### Packet Size Limits

**Minimum Packet Size**: 14 bytes
- Command ID: 1 byte
- Payload Length: 2 bytes
- Payload: 7 bytes (mesh_cmd: 1 + payload_len: 2 + timestamp: 4)
- Checksum: 2 bytes

**Maximum Packet Size**: 1472 bytes (UDP MTU limit)
- Command ID: 1 byte
- Payload Length: 2 bytes
- Payload: 1465 bytes (mesh_cmd: 1 + payload_len: 2 + mesh_payload: 1458 + timestamp: 4)
- Checksum: 2 bytes

**Recommended Mesh Payload Limit**: 1458 bytes (to leave room for headers and avoid fragmentation)

### Checksum Algorithm

The checksum is a simple 16-bit sum of all bytes in the packet except the checksum field itself:

```c
uint16_t checksum = 0;
for (size_t i = 0; i < packet_size - 2; i++) {
    checksum = (checksum + packet[i]) & 0xFFFF;
}
```

The checksum is stored in network byte order (big-endian) at the end of the packet.

## Integration Points

### mesh_root.c

**Heartbeat Command** (line ~147):
- Function: `heartbeat_timer_cb()`
- Command: `MESH_CMD_HEARTBEAT` (0x01)
- Payload: None (1 byte command only)
- Frequency: Every 500ms (configurable)

**RGB Command** (line ~249):
- Function: `mesh_send_rgb()`
- Command: `MESH_CMD_SET_RGB` (0x03)
- Payload: 3 bytes (R, G, B values)
- Trigger: HTTP POST to `/api/color` endpoint

### mode_sequence_root.c

**Sequence Command** (line ~319):
- Function: `mode_sequence_root_broadcast_sequence()`
- Command: `MESH_CMD_SEQUENCE` (0x04)
- Payload: Variable length (rhythm: 1 + num_rows: 1 + color_data: N)
- Trigger: HTTP POST to `/api/sequence` endpoint

**Sequence Beat Command** (line ~378):
- Function: `mode_sequence_root_broadcast_beat()`
- Command: `MESH_CMD_SEQUENCE_BEAT` (0x08)
- Payload: 1 byte (pointer position)
- Frequency: At row boundaries during playback

**Sequence Control Commands** (line ~433):
- Function: `mode_sequence_root_broadcast_control()`
- Commands: `MESH_CMD_SEQUENCE_START` (0x05), `MESH_CMD_SEQUENCE_STOP` (0x06), `MESH_CMD_SEQUENCE_RESET` (0x07)
- Payload: None (1 byte command only)
- Trigger: HTTP POST to `/api/sequence/start`, `/api/sequence/stop`, `/api/sequence/reset`

### mesh_ota.c

**OTA Commands** (multiple locations):
- Function: Various OTA functions
- Commands: Multiple OTA command types (0xF0-0xF6)
- Payload: Variable length (command-specific)
- Trigger: HTTP POST to various OTA endpoints

**Key Integration Points**:
- Line ~905: `send_ota_start()` - OTA_START command
- Line ~943: `send_ota_block_to_node()` - OTA_BLOCK command
- Line ~1589: OTA_ACK command
- Line ~1850: OTA_STATUS command
- Line ~1983: OTA_PREPARE_REBOOT command
- Line ~2096: OTA_REBOOT command
- Line ~2247: OTA distribution commands

### Replacement Pattern

All `esp_mesh_send()` calls were replaced with `mesh_send_with_bridge()` calls:

**Before**:
```c
esp_err_t err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
```

**After**:
```c
esp_err_t err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
```

**No Other Changes**: Only the function name changed. All parameters, error handling, and return value usage remain identical.

## Command Types Forwarded

### Light Control Commands

**MESH_CMD_HEARTBEAT (0x01)**:
- **Payload**: None
- **Size**: 1 byte (command only)
- **Frequency**: Every 500ms
- **Purpose**: Periodic keepalive to detect node presence

**MESH_CMD_SET_RGB (0x03)**:
- **Payload**: 3 bytes (R, G, B values, 0-255 each)
- **Size**: 4 bytes (command + payload)
- **Purpose**: Set RGB color for all nodes

### Sequence Commands

**MESH_CMD_SEQUENCE (0x04)**:
- **Payload**: Variable (rhythm: 1 byte + num_rows: 1 byte + color_data: N bytes)
- **Size**: 3 + (num_rows * 24) bytes
- **Purpose**: Broadcast sequence pattern to all nodes

**MESH_CMD_SEQUENCE_START (0x05)**:
- **Payload**: None
- **Size**: 1 byte (command only)
- **Purpose**: Start sequence playback

**MESH_CMD_SEQUENCE_STOP (0x06)**:
- **Payload**: None
- **Size**: 1 byte (command only)
- **Purpose**: Stop sequence playback

**MESH_CMD_SEQUENCE_RESET (0x07)**:
- **Payload**: None
- **Size**: 1 byte (command only)
- **Purpose**: Reset sequence pointer to 0

**MESH_CMD_SEQUENCE_BEAT (0x08)**:
- **Payload**: 1 byte (pointer position, 0-255)
- **Size**: 2 bytes (command + payload)
- **Purpose**: Tempo synchronization beat at row boundaries

### OTA Commands

**MESH_CMD_OTA_START (0xF1)**:
- **Payload**: Variable (total_blocks: 2 + firmware_size: 4 + version: N)
- **Purpose**: Start OTA distribution

**MESH_CMD_OTA_BLOCK (0xF2)**:
- **Payload**: Variable (block_number: 2 + block_data: N)
- **Purpose**: Send firmware block data

**MESH_CMD_OTA_ACK (0xF3)**:
- **Payload**: Variable (block_number: 2 + status: 1)
- **Purpose**: Acknowledge block reception

**MESH_CMD_OTA_STATUS (0xF4)**:
- **Payload**: Variable (status information)
- **Purpose**: Query OTA status

**MESH_CMD_OTA_PREPARE_REBOOT (0xF5)**:
- **Payload**: Variable
- **Purpose**: Prepare for coordinated reboot

**MESH_CMD_OTA_REBOOT (0xF6)**:
- **Payload**: Variable
- **Purpose**: Execute coordinated reboot

## Error Handling

### Wrapper Function Error Handling

**NULL Data Pointer**:
- Check: `data == NULL || data->data == NULL`
- Action: Log warning, return mesh result without forwarding
- Impact: Mesh operation continues normally

**Empty Data**:
- Check: `data->size == 0`
- Action: Log warning, return mesh result without forwarding
- Impact: Mesh operation continues normally

**Non-Root Node**:
- Check: `!esp_mesh_is_root()`
- Action: Return mesh result without forwarding (normal case)
- Impact: Child nodes don't forward commands (as designed)

**Mesh Send Failure**:
- Behavior: Return error from `esp_mesh_send()` (preserves original behavior)
- Action: UDP forwarding still attempted if registered (fire-and-forget)
- Impact: Mesh error codes returned to caller

### UDP Forwarding Error Handling

**Server Not Registered**:
- Check: `!mesh_udp_bridge_is_registered()`
- Action: Return early (no logging, normal case)
- Impact: No forwarding attempted, mesh operation unaffected

**UDP Socket Initialization Failure**:
- Check: `init_udp_socket() != ESP_OK`
- Action: Log at debug level, return without forwarding
- Impact: Mesh operation continues normally

**Packet Too Large**:
- Check: `packet_size > 1472`
- Action: Log warning, return without forwarding
- Impact: Mesh operation continues normally (packet too large for UDP)

**Memory Allocation Failure**:
- Check: `malloc() == NULL`
- Action: Log at debug level, return without forwarding
- Impact: Mesh operation continues normally (UDP forward skipped)

**UDP Send Failure**:
- Check: `sendto() < 0`
- Action: Log at debug level (packet loss acceptable)
- Impact: Mesh operation continues normally (fire-and-forget)

### Error Logging Levels

**Warning Level** (`ESP_LOGW`):
- NULL data pointer
- Empty data
- Packet too large

**Debug Level** (`ESP_LOGD`):
- UDP socket initialization failure
- Memory allocation failure
- UDP send failure (packet loss acceptable)

**No Logging** (Normal Cases):
- Server not registered
- Non-root node
- Successful forwarding (unless debug logging enabled)

## Non-Blocking Behavior

### Execution Flow

The wrapper function ensures non-blocking behavior:

1. **Mesh Send First**: `esp_mesh_send()` is called immediately, returning mesh result

2. **UDP Forward Async**: UDP forwarding happens after mesh send returns

3. **No Waiting**: Wrapper doesn't wait for UDP send to complete

4. **Fire-and-Forget**: UDP packets are sent without acknowledgment

### Performance Impact

**Mesh Send Latency**: Zero additional latency (mesh send happens first)

**Memory Usage**: Temporary buffers allocated for UDP packet construction, freed immediately after sending

**CPU Usage**: Minimal (packet construction and socket send are lightweight)

**Network Bandwidth**: Additional UDP traffic proportional to mesh command frequency (monitoring only)

### Memory Management

**Dynamic Allocation**: UDP packet buffers allocated on heap for each forward operation

**Immediate Free**: Buffers freed immediately after `sendto()` call (even if send fails)

**No Leaks**: No persistent buffers or memory leaks (allocation matched with free)

**Stack Usage**: Minimal stack usage (only local variables, no large buffers)

## Implementation Details

### Command Extraction

**Command ID Extraction**:
```c
uint8_t mesh_cmd = data->data[0];  /* First byte is command ID */
```

**Payload Extraction**:
```c
const void *mesh_payload = NULL;
size_t mesh_payload_len = 0;
if (data->size > 1) {
    mesh_payload = &data->data[1];        /* Data after command ID */
    mesh_payload_len = data->size - 1;    /* Length minus command ID */
}
```

**Edge Cases**:
- Empty payload (`data->size == 1`): `mesh_payload_len = 0`, `mesh_payload = NULL`
- No command ID (`data->size == 0`): Caught by validation check, no forwarding

### Network Byte Order

All multi-byte values use network byte order (big-endian):

**Payload Length**:
```c
udp_payload[1] = (mesh_payload_len >> 8) & 0xFF;  /* MSB */
udp_payload[2] = mesh_payload_len & 0xFF;         /* LSB */
```

**UDP Packet Length**:
```c
packet[1] = (udp_payload_size >> 8) & 0xFF;  /* MSB */
packet[2] = udp_payload_size & 0xFF;         /* LSB */
```

**Timestamp**:
```c
uint32_t timestamp = mesh_udp_bridge_get_timestamp();
memcpy(&udp_payload[3 + mesh_payload_len], &timestamp, 4);  /* Already in network byte order */
```

**Checksum**:
```c
packet[packet_size - 2] = (checksum >> 8) & 0xFF;  /* MSB */
packet[packet_size - 1] = checksum & 0xFF;         /* LSB */
```

### Timestamp Generation

**Function**: `mesh_udp_bridge_get_timestamp()`

**Purpose**: Provides Unix timestamp for packet ordering/sequencing

**Format**: 32-bit unsigned integer in network byte order

**Usage**: Included in UDP payload for server-side sequencing and ordering

### UDP Socket Management

**Socket Initialization**: `init_udp_socket()` ensures UDP socket is initialized before sending

**Socket Reuse**: Socket is reused for all forward operations (not created per packet)

**Error Handling**: Socket initialization failures are logged and don't affect mesh operation

## API Reference

### mesh_send_with_bridge()

**Header**: `include/mesh_common.h`

**Function**:
```c
esp_err_t mesh_send_with_bridge(const mesh_addr_t *to,
                                 const mesh_data_t *data,
                                 int flag,
                                 const mesh_opt_t opt[],
                                 int opt_count);
```

**Description**: Wrapper function that sends mesh data and optionally forwards it to external web server via UDP.

**Parameters**:
- `to`: Destination mesh address (NULL for broadcast)
- `data`: Mesh data to send (contains command ID and payload)
- `flag`: Mesh data flag (e.g., `MESH_DATA_P2P`)
- `opt`: Optional mesh options (can be NULL)
- `opt_count`: Number of optional mesh options

**Return Value**: Result from `esp_mesh_send()` call (ESP_OK on success, error code on failure)

**Side Effects**: May forward mesh command to external server if registered (non-blocking)

### mesh_udp_bridge_forward_mesh_command_async()

**Header**: `include/mesh_udp_bridge.h`

**Function**:
```c
void mesh_udp_bridge_forward_mesh_command_async(uint8_t mesh_cmd,
                                                 const void *mesh_payload,
                                                 size_t mesh_payload_len);
```

**Description**: Forwards a mesh command to external web server via UDP (non-blocking, fire-and-forget).

**Parameters**:
- `mesh_cmd`: Mesh command ID (first byte of mesh data)
- `mesh_payload`: Mesh command payload (data after command ID)
- `mesh_payload_len`: Length of mesh payload in bytes

**Return Value**: None (void function)

**Side Effects**: Sends UDP packet to external server if registered (non-blocking)

### mesh_udp_bridge_is_registered()

**Header**: `include/mesh_udp_bridge.h`

**Function**:
```c
bool mesh_udp_bridge_is_registered(void);
```

**Description**: Checks if external web server is registered.

**Return Value**: `true` if registered, `false` otherwise

**Usage**: Used by wrapper function to determine if forwarding should occur

## Server-Side Handling

### Current Status

**Implementation**: Server-side handling for mesh command forward packets (command ID 0xE6) is not yet implemented.

**Future Implementation**: Server-side handler should:

1. **Parse UDP Packets**: Receive and parse mesh command forward packets (command ID 0xE6)

2. **Extract Command Information**: Extract mesh command ID, payload length, payload data, and timestamp

3. **Store/Log Commands**: Store or log forwarded commands for monitoring and verification

4. **Optional Verification**: Verify that correct commands were sent to mesh (future feature)

### Expected Server Implementation

**Packet Handler**:
```javascript
function handleMeshCommandForwardPacket(msg, rinfo) {
    // Parse packet: [CMD:0xE6][LEN:2][PAYLOAD:N][CHKSUM:2]
    const commandId = msg[0];
    if (commandId !== UDP_CMD_MESH_COMMAND_FORWARD) {
        return; // Not a mesh command forward packet
    }

    // Extract payload: [mesh_cmd:1][mesh_payload_len:2][mesh_payload:N][timestamp:4]
    const payload = msg.slice(3, 3 + payloadLen);
    const mesh_cmd = payload[0];
    const mesh_payload_len = (payload[1] << 8) | payload[2];
    const mesh_payload = payload.slice(3, 3 + mesh_payload_len);
    const timestamp = payload.readUInt32BE(3 + mesh_payload_len);

    // Store/log command for monitoring
    // Future: Verify correct commands were sent to mesh
}
```

**Integration**: Add handler to UDP server message handler in `server.js`

**Monitoring**: Store forwarded commands in state storage or log for analysis

**Verification**: Compare forwarded commands with expected commands (future feature)

### UDP Command ID

**Command ID**: `0xE6` (UDP_CMD_MESH_COMMAND_FORWARD)

**Direction**: Root → Server (ESP32 root node to external web server)

**ACK Required**: No (fire-and-forget)

**Purpose**: Forward mesh commands for monitoring and verification

---

## Summary

The Mesh Command Bridge provides transparent forwarding of all mesh commands from the ESP32 root node to an optional external web server. The bridge is completely optional, non-blocking, and fire-and-forget, ensuring that mesh operations are never delayed or affected by forwarding failures. All mesh send operations go through the `mesh_send_with_bridge()` wrapper function, which forwards commands via UDP after successfully sending to the mesh network. The forwarding is transparent and doesn't modify original mesh command flow or behavior.
