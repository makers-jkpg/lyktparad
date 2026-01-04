# Web to Root UDP Protocol - Development Guide

**Last Updated:** 2026-01-04

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Protocol Packet Structure](#protocol-packet-structure)
4. [Command ID Definitions](#command-id-definitions)
5. [Payload Formats](#payload-formats)
6. [API Command Payloads](#api-command-payloads)
7. [Reliability Mechanisms](#reliability-mechanisms)
8. [Checksum Algorithm](#checksum-algorithm)
9. [Sequence Numbers](#sequence-numbers)
10. [Ports and Addressing](#ports-and-addressing)
11. [Error Handling](#error-handling)

## Overview

### Purpose

The Web to Root UDP Protocol enables communication between an external web server and ESP32 mesh root nodes. This protocol is binary-based, efficient, and designed to minimize root node load while providing reliable communication for optional external web server features.

### Design Decisions

**Binary protocol**: All packet formats are binary to minimize parsing overhead on the ESP32 root node and reduce bandwidth usage.

**Network byte order**: All multi-byte values use big-endian (network byte order) for consistency across different architectures.

**Fixed-width fields**: Where possible, fields use fixed widths for efficient parsing and minimal memory allocation.

**Optional infrastructure**: The protocol is designed for optional external web server features. Root nodes must continue operating normally even if external server communication fails.

**Fire-and-forget for monitoring**: Heartbeat and state update messages use fire-and-forget semantics to minimize root node load. Only registration requires ACK.

## Architecture

### Communication Flow

```
┌─────────────────────┐                    ┌─────────────────────┐
│  External Web       │                    │  ESP32 Root Node    │
│  Server             │                    │                     │
│                     │                    │                     │
│  HTTP Server        │                    │  Embedded HTTP      │
│  (Port 8080)        │                    │  Server (Port 80)   │
│                     │                    │                     │
│  UDP Server         │◄───────────────────┤  UDP Client         │
│  (Port 8081)        │  Registration      │                     │
│                     │  Heartbeat         │                     │
│                     │  State Updates     │                     │
│                     │                    │                     │
│  UDP Server         │◄───────────────────┤  UDP Client         │
│  (Port 8081)        │  API Commands      │                     │
│                     │  (via Proxy)       │  UDP Server         │
│                     │                    │  (Port 8082)        │
└─────────────────────┘                    └─────────────────────┘
```

### Protocol Layers

1. **Transport Layer**: UDP (connectionless, fire-and-forget where appropriate)
2. **Packet Layer**: Binary packet format with command ID, length, payload, and checksum
3. **Payload Layer**: Command-specific binary payload formats

## Protocol Packet Structure

### Basic Packet Format

All UDP packets follow this structure:

```
[Command ID: 1 byte] [Payload Length: 2 bytes] [Payload: N bytes] [Checksum: 2 bytes]
```

### Field Descriptions

- **Command ID (1 byte)**: Identifies the command type (0xE0-0xF8 reserved for web-to-root protocol)
- **Payload Length (2 bytes)**: Length of payload in network byte order (big-endian)
- **Payload (N bytes)**: Command-specific binary data (variable length, up to MTU limit)
- **Checksum (2 bytes)**: 16-bit sum of all bytes (command ID + payload length + payload), network byte order

### Minimum Packet Size

Minimum packet size is 5 bytes:
- Command ID: 1 byte
- Payload Length: 2 bytes (can be 0 for no-payload commands)
- Checksum: 2 bytes

### Maximum Packet Size

Maximum packet size is limited by UDP MTU:
- **Ethernet MTU**: 1500 bytes
- **IP Header**: 20 bytes
- **UDP Header**: 8 bytes
- **UDP Payload Limit**: 1472 bytes
- **Recommended Limit**: 1472 bytes to avoid fragmentation

## Command ID Definitions

### Core Protocol Commands (0xE0-0xE6)

| Command ID | Name | Direction | ACK Required | Description |
|------------|------|-----------|--------------|-------------|
| 0xE0 | Registration | Root → Server | Yes (0xE3) | Root node registers with external server |
| 0xE1 | Heartbeat | Root → Server | No | Periodic keepalive message |
| 0xE2 | State Update | Root → Server | No | Periodic mesh state information |
| 0xE3 | Registration ACK | Server → Root | No | Acknowledgment for registration |
| 0xE4 | Heartbeat ACK | Reserved | No | Reserved for future use |
| 0xE5 | State Update ACK | Reserved | No | Reserved for future use |
| 0xE6 | Mesh Command Forward | Root → Server | No | Forwarded mesh command (monitoring) |

### API Commands (0xE7-0xEF)

| Command ID | HTTP Endpoint | Method | Direction | ACK Required | Description |
|------------|---------------|--------|-----------|--------------|-------------|
| 0xE7 | `/api/nodes` | GET | Server → Root | Yes (response) | Get mesh node count |
| 0xE8 | `/api/color` | GET | Server → Root | Yes (response) | Get current RGB color |
| 0xE9 | `/api/color` | POST | Server → Root | Yes (response) | Set RGB color |
| 0xEA | `/api/sequence` | POST | Server → Root | Yes (response) | Upload sequence pattern |
| 0xEB | `/api/sequence/pointer` | GET | Server → Root | Yes (response) | Get sequence playback pointer |
| 0xEC | `/api/sequence/start` | POST | Server → Root | Yes (response) | Start sequence playback |
| 0xED | `/api/sequence/stop` | POST | Server → Root | Yes (response) | Stop sequence playback |
| 0xEE | `/api/sequence/reset` | POST | Server → Root | Yes (response) | Reset sequence pointer |
| 0xEF | `/api/sequence/status` | GET | Server → Root | Yes (response) | Get sequence playback status |

### OTA Commands (0xF0-0xF8)

| Command ID | HTTP Endpoint | Method | Direction | ACK Required | Description |
|------------|---------------|--------|-----------|--------------|-------------|
| 0xF0 | `/api/ota/download` | POST | Server → Root | Yes (response) | Start OTA download |
| 0xF1 | `/api/ota/status` | GET | Server → Root | Yes (response) | Get OTA download status |
| 0xF2 | `/api/ota/version` | GET | Server → Root | Yes (response) | Get firmware version |
| 0xF3 | `/api/ota/cancel` | POST | Server → Root | Yes (response) | Cancel OTA download |
| 0xF4 | `/api/ota/distribute` | POST | Server → Root | Yes (response) | Distribute firmware to mesh |
| 0xF5 | `/api/ota/distribution/status` | GET | Server → Root | Yes (response) | Get distribution status |
| 0xF6 | `/api/ota/distribution/progress` | GET | Server → Root | Yes (response) | Get distribution progress |
| 0xF7 | `/api/ota/distribution/cancel` | POST | Server → Root | Yes (response) | Cancel distribution |
| 0xF8 | `/api/ota/reboot` | POST | Server → Root | Yes (response) | Reboot root node |

### Command Direction

- **Root → Server**: Commands sent from ESP32 root node to external web server
- **Server → Root**: Commands sent from external web server to ESP32 root node

## Payload Formats

### Registration (0xE0)

**Direction**: Root → Server
**ACK Required**: Yes (0xE3)
**Payload Format**:

```
[root_ip: 4 bytes] [mesh_id: 6 bytes] [node_count: 1 byte] [firmware_version_len: 1 byte] [firmware_version: N bytes, null-terminated] [timestamp: 4 bytes]
```

**Field Descriptions**:
- `root_ip`: IPv4 address in network byte order (big-endian)
- `mesh_id`: 6-byte mesh ID (same as ESP32 MAC address format)
- `node_count`: Number of connected nodes (uint8_t, 0-255)
- `firmware_version_len`: Length of firmware version string (uint8_t, 0-255)
- `firmware_version`: Null-terminated string (UTF-8, up to 255 bytes including null terminator)
- `timestamp`: Unix timestamp (uint32_t, network byte order)

**Minimum Payload Size**: 16 bytes (4 + 6 + 1 + 1 + 1 + 4, with 1-byte version string)
**Maximum Payload Size**: 271 bytes (4 + 6 + 1 + 1 + 255 + 4)

**Example**:
```c
// Root IP: 192.168.1.100
// Mesh ID: {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}
// Node count: 5
// Firmware version: "1.2.3"
// Timestamp: 1737129600 (Unix timestamp)

uint8_t payload[] = {
    // root_ip: 192.168.1.100 (0xC0A80164)
    0xC0, 0xA8, 0x01, 0x64,
    // mesh_id
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
    // node_count: 5
    0x05,
    // firmware_version_len: 5
    0x05,
    // firmware_version: "1.2.3\0"
    0x31, 0x2E, 0x32, 0x2E, 0x33, 0x00,
    // timestamp: 1737129600 (0x677C1600)
    0x67, 0x7C, 0x16, 0x00
};
```

### Registration ACK (0xE3)

**Direction**: Server → Root
**ACK Required**: No
**Payload Format**:

```
[status: 1 byte]
```

**Field Descriptions**:
- `status`: Registration status (0 = success, 1 = failure)

**Payload Size**: 1 byte

**Example**:
```c
// Success
uint8_t payload[] = { 0x00 };

// Failure
uint8_t payload[] = { 0x01 };
```

### Heartbeat (0xE1)

**Direction**: Root → Server
**ACK Required**: No (fire-and-forget)
**Payload Format**:

```
[timestamp: 4 bytes] [node_count: 1 byte, optional]
```

**Field Descriptions**:
- `timestamp`: Unix timestamp (uint32_t, network byte order)
- `node_count`: Optional node count (uint8_t, 0-255). If present, total payload is 5 bytes.

**Payload Size**: 4 bytes (timestamp only) or 5 bytes (timestamp + node_count)

**Example**:
```c
// With node count
uint8_t payload[] = {
    // timestamp: 1737129600 (0x677C1600)
    0x67, 0x7C, 0x16, 0x00,
    // node_count: 5
    0x05
};
```

### State Update (0xE2)

**Direction**: Root → Server
**ACK Required**: No (fire-and-forget)
**Payload Format**:

```
[root_ip: 4 bytes] [mesh_id: 6 bytes] [timestamp: 4 bytes] [mesh_state: 1 byte] [node_count: 1 byte]
[nodes: N * node_entry] [sequence_active: 1 byte] [sequence_position: 2 bytes] [sequence_total: 2 bytes]
[ota_in_progress: 1 byte] [ota_progress: 1 byte]

node_entry: [node_id: 6 bytes] [ip: 4 bytes] [layer: 1 byte] [parent_id: 6 bytes] [role: 1 byte] [status: 1 byte]
```

**Field Descriptions**:
- `root_ip`: IPv4 address in network byte order (big-endian)
- `mesh_id`: 6-byte mesh ID
- `timestamp`: Unix timestamp (uint32_t, network byte order)
- `mesh_state`: Mesh state (1 = connected, 0 = disconnected)
- `node_count`: Number of nodes in node list (uint8_t, 0-255)
- `nodes`: Variable-length array of node entries (19 bytes per node)
  - `node_id`: 6-byte node MAC address
  - `ip`: IPv4 address in network byte order
  - `layer`: Layer number (uint8_t, 0-6)
  - `parent_id`: 6-byte parent node MAC address
  - `role`: Node role (uint8_t, see below)
  - `status`: Node status (uint8_t, see below)
- `sequence_active`: Sequence playback active (1 = active, 0 = inactive)
- `sequence_position`: Current sequence position (uint16_t, network byte order)
- `sequence_total`: Total sequence length (uint16_t, network byte order)
- `ota_in_progress`: OTA download in progress (1 = active, 0 = inactive)
- `ota_progress`: OTA progress (uint8_t, 0-100)

**Role Values**:
- 0x00: MESH_IDLE (not joined mesh)
- 0x01: MESH_ROOT (root node)
- 0x02: MESH_NODE (intermediate node)
- 0x03: MESH_LEAF (leaf node)

**Status Values**:
- 0x00: Disconnected
- 0x01: Connected

**Minimum Payload Size**: 23 bytes (4 + 6 + 4 + 1 + 1 + 0 + 1 + 2 + 2 + 1 + 1, with 0 nodes)
**Maximum Payload Size**: 4853 bytes (4 + 6 + 4 + 1 + 1 + (255 * 19) + 1 + 2 + 2 + 1 + 1, with 255 nodes)

**Example** (with 1 node):
```c
uint8_t payload[] = {
    // root_ip: 192.168.1.100
    0xC0, 0xA8, 0x01, 0x64,
    // mesh_id
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
    // timestamp: 1737129600
    0x67, 0x7C, 0x16, 0x00,
    // mesh_state: connected
    0x01,
    // node_count: 1
    0x01,
    // node_entry 0
    //   node_id: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    //   ip: 192.168.1.101
    0xC0, 0xA8, 0x01, 0x65,
    //   layer: 1
    0x01,
    //   parent_id: {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC} (root)
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
    //   role: MESH_NODE
    0x02,
    //   status: Connected
    0x01,
    // sequence_active: inactive
    0x00,
    // sequence_position: 0
    0x00, 0x00,
    // sequence_total: 256
    0x01, 0x00,
    // ota_in_progress: inactive
    0x00,
    // ota_progress: 0
    0x00
};
```

### Mesh Command Forward (0xE6)

**Direction**: Root → Server
**ACK Required**: No (fire-and-forget)
**Payload Format**:

```
[mesh_cmd_id: 1 byte] [mesh_payload_len: 2 bytes] [mesh_payload: N bytes] [timestamp: 4 bytes, optional]
```

**Field Descriptions**:
- `mesh_cmd_id`: Original mesh command ID (from `mesh_commands.h`)
- `mesh_payload_len`: Length of original mesh payload (uint16_t, network byte order)
- `mesh_payload`: Original mesh command payload (variable length)
- `timestamp`: Optional timestamp (uint32_t, network byte order). If present, add 4 bytes to payload.

**Payload Size**: Variable (3 + N bytes, or 7 + N bytes with timestamp)

**Example**:
```c
// Heartbeat command (mesh_cmd_id = 0x01) with counter 12345 and timestamp
uint8_t payload[] = {
    // mesh_cmd_id: 0x01 (HEARTBEAT)
    0x01,
    // mesh_payload_len: 4 bytes
    0x00, 0x04,
    // mesh_payload: counter (12345 = 0x00003039)
    0x00, 0x30, 0x39,
    // timestamp: 1737129600 (optional)
    0x67, 0x7C, 0x16, 0x00
};
```

## API Command Payloads

API commands (0xE7-0xF8) use sequence numbers in the packet format. See [Sequence Numbers](#sequence-numbers) for details.

### Packet Format for API Commands

```
[Command ID: 1 byte] [Payload Length: 2 bytes] [Sequence Number: 2 bytes] [Payload: N bytes] [Checksum: 2 bytes]
```

The sequence number is inserted between payload length and payload for API commands only.

### API Command Request Payloads

#### GET /api/nodes (0xE7)

**Payload**: Empty (0 bytes)

#### GET /api/color (0xE8)

**Payload**: Empty (0 bytes)

#### POST /api/color (0xE9)

**Payload Format**:

```
[r: 1 byte] [g: 1 byte] [b: 1 byte]
```

**Field Descriptions**:
- `r`: Red component (0-255)
- `g`: Green component (0-255)
- `b`: Blue component (0-255)

**Payload Size**: 3 bytes

#### POST /api/sequence (0xEA)

**Payload Format**: Binary data (rhythm + length + color data)

See [Sequence Mode Developer Guide](mode-sequence.md) for detailed sequence format.

**Payload Size**: Variable (depends on sequence size)

#### GET /api/sequence/pointer (0xEB)

**Payload**: Empty (0 bytes)

#### POST /api/sequence/start (0xEC)

**Payload**: Empty (0 bytes)

#### POST /api/sequence/stop (0xED)

**Payload**: Empty (0 bytes)

#### POST /api/sequence/reset (0xEE)

**Payload**: Empty (0 bytes)

#### GET /api/sequence/status (0xEF)

**Payload**: Empty (0 bytes)

#### POST /api/ota/download (0xF0)

**Payload Format**:

```
[url_len: 1 byte] [url: N bytes, null-terminated]
```

**Field Descriptions**:
- `url_len`: Length of URL string (uint8_t, 0-255)
- `url`: URL string (UTF-8, null-terminated, up to 255 bytes including null)

**Payload Size**: 1 + N bytes (where N = url_len)

#### GET /api/ota/status (0xF1)

**Payload**: Empty (0 bytes)

#### GET /api/ota/version (0xF2)

**Payload**: Empty (0 bytes)

#### POST /api/ota/cancel (0xF3)

**Payload**: Empty (0 bytes)

#### POST /api/ota/distribute (0xF4)

**Payload**: Empty (0 bytes)

#### GET /api/ota/distribution/status (0xF5)

**Payload**: Empty (0 bytes)

#### GET /api/ota/distribution/progress (0xF6)

**Payload**: Empty (0 bytes)

#### POST /api/ota/distribution/cancel (0xF7)

**Payload**: Empty (0 bytes)

#### POST /api/ota/reboot (0xF8)

**Payload Format**:

```
[timeout: 2 bytes] [delay: 2 bytes]
```

**Field Descriptions**:
- `timeout`: Reboot timeout in seconds (uint16_t, network byte order, default: 10)
- `delay`: Reboot delay in milliseconds (uint16_t, network byte order, default: 1000)

**Payload Size**: 4 bytes

### API Command Response Payloads

#### GET /api/nodes (0xE7)

**Response Payload Format**:

```
[node_count: 4 bytes, network byte order] OR [node_count: 1 byte]
```

**Field Descriptions**:
- `node_count`: Number of nodes (uint32_t or uint8_t, depending on implementation)

**Payload Size**: 4 bytes or 1 byte

#### GET /api/color (0xE8)

**Response Payload Format**:

```
[r: 1 byte] [g: 1 byte] [b: 1 byte] [is_set: 1 byte]
```

**Field Descriptions**:
- `r`: Red component (0-255)
- `g`: Green component (0-255)
- `b`: Blue component (0-255)
- `is_set`: Color has been set (0 = false, 1 = true)

**Payload Size**: 4 bytes

#### GET /api/sequence/pointer (0xEB)

**Response Payload Format**:

```
[pointer: 2 bytes, network byte order] OR [pointer: 1 byte]
```

**Field Descriptions**:
- `pointer`: Current sequence playback pointer (uint16_t or uint8_t, depending on implementation)

**Payload Size**: 2 bytes or 1 byte

#### GET /api/sequence/status (0xEF)

**Response Payload Format**:

```
[active: 1 byte]
```

**Field Descriptions**:
- `active`: Sequence playback active (0 = false, 1 = true)

**Payload Size**: 1 byte

#### GET /api/ota/status (0xF1)

**Response Payload Format**:

```
[downloading: 1 byte] [progress: 4 bytes, float] OR [progress: 1 byte, 0-100]
```

**Field Descriptions**:
- `downloading`: OTA download in progress (0 = false, 1 = true)
- `progress`: Download progress (float 0.0-1.0 or uint8_t 0-100, depending on implementation)

**Payload Size**: 5 bytes (float) or 2 bytes (uint8_t)

#### GET /api/ota/version (0xF2)

**Response Payload Format**:

```
[version_len: 1 byte] [version: N bytes, null-terminated]
```

**Field Descriptions**:
- `version_len`: Length of version string (uint8_t, 0-255)
- `version`: Version string (UTF-8, null-terminated, up to 255 bytes including null)

**Payload Size**: 1 + N bytes (where N = version_len)

#### GET /api/ota/distribution/status (0xF5)

**Response Payload Format**:

```
[distributing: 1 byte]
```

**Field Descriptions**:
- `distributing`: Distribution in progress (0 = false, 1 = true)

**Payload Size**: 1 byte

#### GET /api/ota/distribution/progress (0xF6)

**Response Payload Format**:

```
[progress: 4 bytes, float] OR [progress: 1 byte, 0-100]
```

**Field Descriptions**:
- `progress`: Distribution progress (float 0.0-1.0 or uint8_t 0-100, depending on implementation)

**Payload Size**: 4 bytes (float) or 1 byte (uint8_t)

## Reliability Mechanisms

### ACK Requirements

- **Registration (0xE0)**: Requires ACK (0xE3). Root node waits for ACK with 5-second timeout and retries up to 3 times with exponential backoff.
- **Heartbeat (0xE1)**: Fire-and-forget. No ACK required. Packet loss is acceptable.
- **State Update (0xE2)**: Fire-and-forget. No ACK required. Packet loss is acceptable.
- **Mesh Command Forward (0xE6)**: Fire-and-forget. No ACK required. Packet loss is acceptable.
- **API Commands (0xE7-0xF8)**: Require response (request-response pattern). Server waits for response with 8-second timeout.

### Retry Logic

**Registration Retry**:
- Maximum attempts: 3
- Exponential backoff: 1s, 2s, 4s
- Timeout per attempt: 5 seconds
- Total maximum time: ~12 seconds (1 + 2 + 4 + 3*5 = 22 seconds worst case)

**API Command Retry**:
- Maximum attempts: 2 (for POST operations only)
- Exponential backoff: 2s, 4s
- Timeout per attempt: 8 seconds
- GET operations: No retry (idempotent)

### Timeout Values

- **Registration ACK timeout**: 5 seconds
- **API command response timeout**: 8 seconds
- **UDP send timeout**: 1 second
- **Heartbeat interval**: 45 seconds (default, configurable 30-60 seconds)
- **State update interval**: 3 seconds (default, configurable 2-5 seconds)

### Sequence Numbers

Sequence numbers are used for API commands (0xE7-0xF8) to match requests with responses. See [Sequence Numbers](#sequence-numbers) for details.

## Checksum Algorithm

### Checksum Calculation

The checksum is a simple 16-bit sum of all bytes in the packet (excluding the checksum field itself).

**Algorithm**:

```c
uint16_t calculate_checksum(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    // Return 16-bit sum (wraps if sum > 65535)
    return (uint16_t)sum;
}
```

**Checksum Field**: The checksum is stored in the last 2 bytes of the packet in network byte order (big-endian).

### Checksum Verification

**On Receive**:

1. Extract checksum from packet (last 2 bytes)
2. Calculate checksum of packet data (excluding checksum field)
3. Compare calculated checksum with received checksum
4. If mismatch: Log warning and optionally discard packet (checksum verification is optional - implementation may continue on mismatch)

**Note**: Checksum verification is optional. Some implementations continue processing even if checksum mismatch is detected (for debugging or backward compatibility).

## Sequence Numbers

### Purpose

Sequence numbers are used for API commands (0xE7-0xF8) to match requests with responses. They enable the server to track multiple concurrent API requests.

### Sequence Number Format

Sequence numbers are 16-bit values (uint16_t) in network byte order.

**Range**: 0x0000 to 0xFFFF (0 to 65535)
**Wrap**: Sequence numbers wrap at 65535 (0xFFFF → 0x0000)

### Sequence Number Generation

**Server Side** (when sending API command to root):
- Generate sequence number (incrementing counter, wraps at 65535)
- Include sequence number in packet (between payload length and payload)
- Store pending request with sequence number
- Match response using sequence number

**Root Side** (when responding to API command):
- Extract sequence number from request packet
- Include same sequence number in response packet
- Server matches response to request using sequence number

### Packet Format with Sequence Number

For API commands only:

```
[Command ID: 1 byte] [Payload Length: 2 bytes] [Sequence Number: 2 bytes] [Payload: N bytes] [Checksum: 2 bytes]
```

**Note**: Sequence numbers are NOT included in payload length calculation.

**Example**:
```c
// GET /api/nodes request
uint8_t packet[] = {
    // Command ID: 0xE7
    0xE7,
    // Payload Length: 0 bytes
    0x00, 0x00,
    // Sequence Number: 12345 (0x3039)
    0x30, 0x39,
    // Checksum: 0x1234 (example)
    0x12, 0x34
};
```

### Sequence Number Matching

**Server Side**:
- Store pending requests in a Map: `sequence_number → { timeout, callback }`
- When response received, look up sequence number in Map
- Call callback with response data
- Remove entry from Map

**Timeout Handling**:
- Pending requests expire after 8 seconds
- Periodic cleanup removes expired entries (every 60 seconds)
- Maximum pending requests: Limited by available memory

## Ports and Addressing

### Port Assignments

- **HTTP Server**: Port 8080 (default, configurable via `PORT` environment variable)
- **UDP Registration/Heartbeat/State**: Port 8081 (default, configurable via `UDP_PORT` environment variable)
- **ESP32 API Listener**: Port 8082 (well-known port, hardcoded)
- **UDP Broadcast Listener**: Port 5353 (default, configurable via `BROADCAST_PORT` environment variable)

### Addressing

**Server → Root (API Commands)**:
- **Destination IP**: Root node IP address (from registration)
- **Destination Port**: 8082 (ESP32 API listener)
- **Source Port**: 8081 (server UDP port)

**Root → Server (Registration/Heartbeat/State)**:
- **Destination IP**: Server IP address (from mDNS discovery or cache)
- **Destination Port**: 8081 (server UDP port)
- **Source Port**: Ephemeral (assigned by OS)

**Server → Broadcast (Discovery)**:
- **Destination IP**: 255.255.255.255 (broadcast address)
- **Destination Port**: 5353 (broadcast listener port)
- **Source Port**: Ephemeral (assigned by OS)

## Error Handling

### Packet Validation

**Minimum Size Check**:
- All packets must be at least 5 bytes (Command ID + Payload Length + Checksum)
- Packets smaller than 5 bytes are discarded

**Payload Length Check**:
- Payload length must match actual payload size
- Packets with mismatched payload length are discarded

**Checksum Check**:
- Checksum verification is optional
- Mismatched checksum logs warning but may continue processing (implementation-dependent)

### Error Responses

**API Command Errors**:
- Error responses include error indicator in payload
- Error indicators: 0 = success, 1 = failure
- HTTP status codes mapped from error indicators (403, 400, 409, 500)

**Registration Errors**:
- Registration ACK includes status byte (0 = success, 1 = failure)
- Root node retries on failure (up to 3 attempts)

### Timeout Handling

**Registration Timeout**:
- If ACK not received within 5 seconds, retry
- After 3 failed attempts, give up (root continues operating normally)

**API Command Timeout**:
- If response not received within 8 seconds, return 503 error
- Retry for POST operations (up to 2 attempts)
- GET operations: No retry (idempotent)

### Graceful Degradation

**Root Node Behavior**:
- If external server unavailable, root continues operating normally
- Embedded web server always runs regardless of external server status
- Discovery and registration failures do not affect mesh operation

**Server Behavior**:
- If root node unavailable (503), suggest direct access to root IP
- If root node not registered (404), suggest waiting for registration
- Server continues operating even if no root nodes are registered

---

**Note**: This protocol is designed for optional external web server features. Root nodes must continue operating normally even if external server communication fails. The embedded web server on the root node provides full functionality and is always available.
