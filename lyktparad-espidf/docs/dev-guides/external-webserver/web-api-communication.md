# Web API Communication - Development Guide

**Last Updated:** 2026-01-04

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [API Proxy Layer](#api-proxy-layer)
4. [Periodic State Updates](#periodic-state-updates)
5. [Protocol Details](#protocol-details)
6. [Request/Response Flow](#requestresponse-flow)
7. [Error Handling and Graceful Degradation](#error-handling-and-graceful-degradation)
8. [Implementation Details](#implementation-details)
9. [API Reference](#api-reference)
10. [Integration Points](#integration-points)

## Overview

### Purpose

The Web API Communication system enables bidirectional communication between the web UI and ESP32 root nodes through an optional external web server. The system consists of two complementary features:

1. **API Proxy Layer**: Translates HTTP requests from the web UI to UDP commands for the ESP32 root node, enabling the web UI to control and query the mesh network through the external server.
2. **Periodic State Updates**: Provides push-based mesh state updates from the ESP32 root node to the external server, enabling the web UI to receive real-time mesh network status without polling.

Both features are completely optional - ESP32 root nodes always run their embedded web server and can be accessed directly via their IP address. The external server provides a better user experience if available, but is never required for mesh functionality.

### Design Decisions

**Optional Infrastructure**: The entire API communication system is optional. ESP32 devices must continue operating normally even if the external server is unavailable or API communication fails. The embedded web server (`mesh_web_server_start()`) MUST ALWAYS run regardless of external server communication status.

**Bidirectional Communication**: The system supports both request/response (API proxy) and push-based (state updates) communication patterns, providing comprehensive mesh network control and monitoring capabilities.

**Web UI Unchanged**: The web UI continues to use standard HTTP requests. Only the communication path between the external server and ESP32 root node uses UDP protocol. This ensures the web UI remains simple and compatible.

**Fire-and-Forget State Updates**: State updates use fire-and-forget semantics (no ACK required) to minimize root node load. Only API commands require responses for reliability.

**Binary Protocol Efficiency**: UDP communication uses binary payload encoding for efficiency, reducing bandwidth usage compared to JSON over HTTP.

**Automatic Failure Handling**: API communication failures are handled gracefully without affecting mesh operation or the embedded web server. The system automatically retries critical operations and provides fallback mechanisms.

**Registration Dependency**: Both API proxy and state updates only operate if the root node has successfully registered with the external server. This ensures communication only happens when the server is ready.

## Architecture

### Overall Communication Flow

```
┌─────────────────────────────────────┐
│  Web UI (Browser)                    │
│  - Uses HTTP (unchanged)             │
│  - Sends HTTP requests               │
│  - Receives HTTP responses           │
│  - Polls /api/mesh/state             │
└──────────────┬──────────────────────┘
               │
               │ HTTP Request/Response
               │ GET/POST /api/*
               ▼
┌─────────────────────────────────────┐
│  External Web Server                  │
│  (lyktparad-server)                   │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  API Proxy Routes               │ │
│  │  - HTTP to UDP Translation      │ │
│  │  - UDP to HTTP Translation      │ │
│  │  - Request/Response Matching    │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  UDP Client                     │ │
│  │  - Send UDP Command             │ │
│  │  - Wait for Response            │ │
│  │  - Handle Timeout               │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│  ┌──────────────┴──────────────────┐ │
│  │  State Storage                  │ │
│  │  - Store State Updates          │ │
│  │  - Provide GET /api/mesh/state  │ │
│  └─────────────────────────────────┘ │
└─────────────────┬────────────────────┘
                  │
                  │ UDP Packets
                  │ API Commands: 0xE7-0xF8
                  │ State Updates: 0xE2
                  ▼
┌─────────────────────────────────────┐
│  ESP32 Root Node                     │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  Embedded Web Server            │ │
│  │  (MUST ALWAYS RUN)              │ │
│  │  - Port 80                      │ │
│  │  - Direct HTTP Access           │ │
│  └─────────────────────────────────┘ │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  UDP API Listener Task          │ │
│  │  - Listen on Port 8082          │ │
│  │  - Process API Commands         │ │
│  │  - Send Responses               │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  API Command Handlers           │ │
│  │  - 18 Endpoint Handlers         │ │
│  │  - Mesh Operations              │ │
│  │  - Sequence Control             │ │
│  │  - OTA Management               │ │
│  └─────────────────────────────────┘ │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  State Update Task              │ │
│  │  - Periodic (3s interval)       │ │
│  │  - Collect Mesh State           │ │
│  │  - Build Binary Payload         │ │
│  │  - Send UDP (fire-and-forget)   │ │
│  └──────────────┬──────────────────┘ │
└─────────────────┼────────────────────┘
                  │
                  │ UDP Packets
                  │ API Responses: 0xE7-0xF8
                  │ State Updates: 0xE2
                  ▼
┌─────────────────────────────────────┐
│  External Web Server                  │
│  - Process Responses                 │
│  - Store State Updates               │
│  - Return to Web UI                  │
└─────────────────────────────────────┘
```

### Communication Patterns

#### Pattern 1: Request/Response (API Proxy)

```
Web UI → HTTP Request → External Server → UDP Command (0xE7-0xF8) → ESP32 Root
                                                                          │
Web UI ← HTTP Response ← External Server ← UDP Response (0xE7-0xF8) ←────┘
```

- **Direction**: Bidirectional (Web UI → ESP32)
- **Protocol**: HTTP → UDP → HTTP
- **Reliability**: Request/response with sequence number matching
- **Use Cases**: Control operations (color change, sequence control, OTA), query operations (node list, status)

#### Pattern 2: Push Updates (State Updates)

```
ESP32 Root → UDP State Update (0xE2) → External Server → State Storage
                                                               │
Web UI ← HTTP GET /api/mesh/state ← External Server ←─────────┘
```

- **Direction**: Unidirectional (ESP32 → Web UI)
- **Protocol**: UDP (fire-and-forget) → HTTP GET
- **Reliability**: Fire-and-forget (packet loss acceptable)
- **Use Cases**: Real-time mesh status, node topology, sequence progress, OTA progress

## API Proxy Layer

### Overview

The API Proxy Layer translates HTTP requests from the web UI to UDP commands for the ESP32 root node. The web UI sends standard HTTP requests to the external server, which converts them to binary UDP packets, sends them to the registered root node, receives UDP responses, and converts them back to HTTP responses.

### Architecture

```
┌─────────────────────────────────────┐
│  Web UI (Browser)                    │
│  GET /api/nodes                      │
│  POST /api/color {r:255,g:0,b:0}    │
└──────────────┬──────────────────────┘
               │ HTTP Request
               ▼
┌─────────────────────────────────────┐
│  External Server: Proxy Route        │
│  /api/* → proxyHandler()            │
│                                       │
│  1. Parse HTTP Request               │
│  2. Map Endpoint → UDP Command ID    │
│  3. Convert JSON → Binary            │
│  4. Build UDP Packet                 │
└──────────────┬──────────────────────┘
               │ UDP Command (0xE7-0xF8)
               │ Port 8082
               ▼
┌─────────────────────────────────────┐
│  ESP32 Root: API Listener            │
│                                       │
│  1. Receive UDP Packet               │
│  2. Parse Command ID                 │
│  3. Route to Handler                 │
│  4. Process Request                  │
│  5. Build Response                   │
│  6. Send UDP Response                │
└──────────────┬──────────────────────┘
               │ UDP Response (0xE7-0xF8)
               │ Same Sequence Number
               ▼
┌─────────────────────────────────────┐
│  External Server: Response Handler   │
│                                       │
│  1. Receive UDP Response             │
│  2. Match by Sequence Number         │
│  3. Convert Binary → JSON            │
│  4. Build HTTP Response              │
└──────────────┬──────────────────────┘
               │ HTTP Response
               ▼
┌─────────────────────────────────────┐
│  Web UI (Browser)                    │
│  {nodes: [...]}                     │
│  200 OK                             │
└─────────────────────────────────────┘
```

### Endpoint Mapping

The API Proxy maps 18 HTTP endpoints to UDP command IDs:

| HTTP Endpoint | Method | UDP Command ID | Description |
|--------------|--------|----------------|-------------|
| `/api/nodes` | GET | `0xE7` | Get list of connected nodes |
| `/api/color` | GET | `0xE8` | Get current RGB color |
| `/api/color` | POST | `0xE9` | Set RGB color |
| `/api/sequence` | POST | `0xEA` | Upload sequence pattern |
| `/api/sequence/pointer` | GET | `0xEB` | Get sequence pointer |
| `/api/sequence/start` | POST | `0xEC` | Start sequence |
| `/api/sequence/stop` | POST | `0xED` | Stop sequence |
| `/api/sequence/reset` | POST | `0xEE` | Reset sequence |
| `/api/sequence/status` | GET | `0xEF` | Get sequence status |
| `/api/ota/download` | POST | `0xF0` | Download OTA firmware |
| `/api/ota/status` | GET | `0xF1` | Get OTA download status |
| `/api/ota/version` | GET | `0xF2` | Get firmware version |
| `/api/ota/cancel` | POST | `0xF3` | Cancel OTA download |
| `/api/ota/distribute` | POST | `0xF4` | Distribute OTA to mesh |
| `/api/ota/distribution/status` | GET | `0xF5` | Get distribution status |
| `/api/ota/distribution/progress` | GET | `0xF6` | Get distribution progress |
| `/api/ota/distribution/cancel` | POST | `0xF7` | Cancel distribution |
| `/api/ota/reboot` | POST | `0xF8` | Reboot root node |

### HTTP to UDP Translation

#### Request Translation Flow

1. **Parse HTTP Request**: Extract method, path, and body from Express request object
2. **Map Endpoint**: Look up UDP command ID from endpoint mapping table
3. **Convert JSON to Binary**: Transform JSON request body to binary payload format
4. **Generate Sequence Number**: Create unique sequence number for request/response matching
5. **Build UDP Packet**: Construct UDP packet with command ID, sequence number, and payload

#### UDP Packet Structure (API Commands)

```
[CMD:1][LEN:2][SEQ:2][PAYLOAD:N][CHKSUM:2]
```

- **CMD** (1 byte): UDP command ID (0xE7-0xF8)
- **LEN** (2 bytes): Payload length (network byte order, big-endian)
- **SEQ** (2 bytes): Sequence number (network byte order, big-endian)
- **PAYLOAD** (N bytes): Binary payload (endpoint-specific)
- **CHKSUM** (2 bytes): 16-bit checksum (network byte order)

#### Example: POST /api/color

**HTTP Request**:
```json
{
  "r": 255,
  "g": 0,
  "b": 0
}
```

**UDP Packet**:
```
[0xE9][0x00][0x03][0x00][0x01][0xFF][0x00][0x00][0xXX][0xXX]
  CMD    LEN MSB  LEN LSB  SEQ MSB  SEQ LSB    R    G    B    CHKSUM
```

### UDP to HTTP Translation

#### Response Translation Flow

1. **Receive UDP Response**: Wait for UDP response with matching sequence number
2. **Parse UDP Packet**: Extract command ID, sequence number, and payload
3. **Match Request**: Find corresponding HTTP request using sequence number
4. **Convert Binary to JSON**: Transform binary payload to JSON format
5. **Build HTTP Response**: Construct HTTP response with status code and JSON body

#### Example: Response to GET /api/nodes

**UDP Response Payload** (binary):
```
[node_count:1][nodes:N]
```

**HTTP Response**:
```json
{
  "nodes": [
    {
      "id": "aa:bb:cc:dd:ee:ff",
      "ip": "192.168.1.101",
      "layer": 1,
      "role": "child"
    }
  ]
}
```

### Request/Response Matching

The API Proxy uses sequence numbers to match requests and responses:

1. **Sequence Number Generation**: Incrementing counter (0x0001, 0x0002, ...)
2. **Request Storage**: Pending requests stored with sequence numbers
3. **Response Matching**: UDP responses matched to requests by sequence number
4. **Timeout Handling**: Pending requests cleaned up after timeout (8 seconds default)
5. **Retry Logic**: Critical commands (POST) retried up to 3 times on timeout

### Timeout and Retry Handling

- **Default Timeout**: 8 seconds for UDP response
- **Retry Strategy**:
  - GET requests: No retry (idempotent)
  - POST requests: Up to 3 retries with exponential backoff
- **Error Response**: HTTP 503 Service Unavailable on timeout or failure
- **Offline Detection**: Root node marked offline after multiple UDP failures

## Periodic State Updates

### Overview

Periodic State Updates provide push-based mesh state information from the ESP32 root node to the external server. The root node periodically collects mesh network state, encodes it as a binary payload, and sends it via UDP to the external server. The server stores the state and makes it available via HTTP GET endpoint for the web UI.

### Architecture

```
┌─────────────────────────────────────┐
│  ESP32 Root Node                     │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  State Update Task              │ │
│  │  (FreeRTOS Task)                │ │
│  │  Interval: 3 seconds            │ │
│  │  Priority: Low                  │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  State Collection               │ │
│  │  - Mesh Routing Table           │ │
│  │  - Sequence State               │ │
│  │  - OTA Status                   │ │
│  │  - Node Topology                │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  Binary Payload Encoding        │ │
│  │  - Fixed-width fields           │ │
│  │  - Network byte order           │ │
│  │  - Variable-length arrays       │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  UDP Send (Fire-and-Forget)     │ │
│  │  Command ID: 0xE2               │ │
│  │  No ACK required                │ │
│  └──────────────┬──────────────────┘ │
└─────────────────┼────────────────────┘
                  │
                  │ UDP State Update (0xE2)
                  │ Port 8081
                  ▼
┌─────────────────────────────────────┐
│  External Server                     │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  UDP Handler (0xE2)             │ │
│  │  - Parse Binary Payload         │ │
│  │  - Extract State Fields         │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  State Storage                  │ │
│  │  - In-Memory Map                │ │
│  │  - Timestamp Update             │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  HTTP GET /api/mesh/state       │ │
│  │  - Return Latest State (JSON)   │ │
│  │  - Staleness Detection          │ │
│  └──────────────┬──────────────────┘ │
└─────────────────┼────────────────────┘
                  │
                  │ HTTP Response (JSON)
                  ▼
┌─────────────────────────────────────┐
│  Web UI (Browser)                    │
│  - Polls /api/mesh/state             │
│  - Updates UI with Mesh State        │
└─────────────────────────────────────┘
```

### Update Frequency

- **Default Interval**: 3 seconds
- **Configurable**: Can be adjusted based on network conditions
- **Optimization**: Balance between UI responsiveness and resource usage
- **Adaptive**: Can reduce frequency during low activity (optional)

### State Data Collection

The state update collects the following information:

1. **Root Node Information**:
   - Root IP address (4 bytes)
   - Mesh ID (6 bytes, MAC address)
   - Timestamp (4 bytes, Unix timestamp)

2. **Mesh Network State**:
   - Mesh state (1 byte: 0=disconnected, 1=connected)
   - Node count (1 byte)

3. **Node List** (variable length):
   - For each node:
     - Node ID (6 bytes, MAC address)
     - IP address (4 bytes)
     - Layer (1 byte)
     - Parent ID (6 bytes, MAC address)
     - Role (1 byte: 0=root, 1=child, 2=leaf)
     - Status (1 byte: 0=disconnected, 1=connected)

4. **Sequence State**:
   - Sequence active flag (1 byte: 0=inactive, 1=active)
   - Sequence position (2 bytes, network byte order)
   - Sequence total length (2 bytes, network byte order)

5. **OTA State**:
   - OTA in progress flag (1 byte: 0=no, 1=yes)
   - OTA progress percentage (1 byte: 0-100)

### Binary Payload Structure

```
[ROOT_IP:4][MESH_ID:6][TIMESTAMP:4][MESH_STATE:1][NODE_COUNT:1]
[NODES:N*NODE_ENTRY][SEQ_ACTIVE:1][SEQ_POS:2][SEQ_TOTAL:2]
[OTA_IN_PROGRESS:1][OTA_PROGRESS:1]
```

**Node Entry Structure** (19 bytes per node):
```
[NODE_ID:6][IP:4][LAYER:1][PARENT_ID:6][ROLE:1][STATUS:1]
```

**Total Payload Size**:
- Minimum: 23 bytes (no nodes)
- Maximum: ~23 + (N * 19) bytes (N = node count)
- Typical: ~100-500 bytes for 5-25 nodes

### UDP Packet Structure (State Updates)

```
[CMD:1][LEN:2][PAYLOAD:N][CHKSUM:2]
```

- **CMD** (1 byte): `0xE2` (State Update command ID)
- **LEN** (2 bytes): Payload length (network byte order)
- **PAYLOAD** (N bytes): Binary state data (see above)
- **CHKSUM** (2 bytes): 16-bit checksum (network byte order)

**Note**: State update packets do NOT include sequence numbers (fire-and-forget).

### State Storage and Retrieval

#### Server-Side Storage

State updates are stored in an in-memory Map structure:
- **Key**: Mesh ID (as hex string)
- **Value**: State object with all fields
- **Timestamp**: Last update timestamp (for staleness detection)

#### HTTP GET Endpoint

**Endpoint**: `GET /api/mesh/state`

**Response Format**:
```json
{
  "root_ip": "192.168.1.100",
  "mesh_id": "aabbccddeeff",
  "timestamp": 1642684800,
  "timestamp_iso": "2022-01-20T10:00:00.000Z",
  "mesh_state": "connected",
  "node_count": 5,
  "nodes": [
    {
      "node_id": "aabbccddeeff",
      "ip": "192.168.1.101",
      "layer": 1,
      "parent_id": "aabbccddeeff",
      "role": "child",
      "status": "connected"
    }
  ],
  "sequence": {
    "active": true,
    "position": 42,
    "total": 100
  },
  "ota": {
    "in_progress": false,
    "progress": 0
  },
  "updated_at": "2022-01-20T10:00:03.000Z",
  "stale": false
}
```

**Staleness Detection**: State is considered stale if older than 10 seconds. Stale state includes a `stale: true` flag and warning message.

## Protocol Details

### Checksum Calculation

All UDP packets use a 16-bit checksum for integrity verification:

```c
uint16_t checksum = 0;
for (size_t i = 0; i < packet_size - 2; i++) {
    checksum = (checksum + packet[i]) & 0xFFFF;
}
```

The checksum is calculated over all bytes except the checksum field itself, and appended to the packet in network byte order (big-endian).

### Network Byte Order

All multi-byte values (lengths, sequence numbers, timestamps, etc.) use network byte order (big-endian):

- **2-byte values**: `(value >> 8) & 0xFF` (MSB), `value & 0xFF` (LSB)
- **4-byte values**: `(value >> 24) & 0xFF` (MSB), `(value >> 16) & 0xFF`, `(value >> 8) & 0xFF`, `value & 0xFF` (LSB)

### UDP Ports

- **API Commands**: Port `8082` (ESP32 root node listener)
- **State Updates**: Port `8081` (external server listener)
- **Registration/Heartbeat**: Port `8081` (external server listener)

### Command ID Ranges

- **0xE0**: Registration
- **0xE1**: Heartbeat
- **0xE2**: State Update
- **0xE3**: Registration ACK
- **0xE6**: Mesh Command Forward
- **0xE7-0xF8**: API Commands (18 endpoints)

## Request/Response Flow

### Complete API Request Flow

1. **Web UI** sends HTTP request: `POST /api/color {"r":255,"g":0,"b":0}`
2. **External Server** receives request in `proxyHandler()`
3. **External Server** looks up registered root node from registration storage
4. **External Server** calls `httpToUdpCommand()` to translate HTTP → UDP
5. **External Server** maps endpoint to command ID `0xE9`
6. **External Server** converts JSON `{"r":255,"g":0,"b":0}` to binary `[0xFF, 0x00, 0x00]`
7. **External Server** generates sequence number `0x0001`
8. **External Server** builds UDP packet: `[0xE9][0x00][0x03][0x00][0x01][0xFF][0x00][0x00][CHKSUM]`
9. **External Server** sends UDP packet to root node IP:port `192.168.1.100:8082`
10. **External Server** waits for UDP response (timeout: 8 seconds)
11. **ESP32 Root** receives UDP packet in `mesh_udp_bridge_api_listener_task()`
12. **ESP32 Root** parses packet: command ID `0xE9`, sequence `0x0001`, payload `[0xFF, 0x00, 0x00]`
13. **ESP32 Root** routes to `handle_api_color_post()` handler
14. **ESP32 Root** processes request: sets RGB color to (255, 0, 0)
15. **ESP32 Root** builds response payload: `[0x01]` (success)
16. **ESP32 Root** builds response packet: `[0xE9][0x00][0x01][0x00][0x01][0x01][CHKSUM]`
17. **ESP32 Root** sends UDP response to server IP:port (from request source address)
18. **External Server** receives UDP response with sequence `0x0001`
19. **External Server** matches response to pending request
20. **External Server** calls `udpToHttpResponse()` to translate UDP → HTTP
21. **External Server** converts binary `[0x01]` to JSON `{"success": true}`
22. **External Server** builds HTTP response: `200 OK` with JSON body
23. **Web UI** receives HTTP response and updates UI

### State Update Flow

1. **ESP32 Root** State Update Task wakes up (every 3 seconds)
2. **ESP32 Root** checks if still root and registered
3. **ESP32 Root** calls `mesh_udp_bridge_collect_state()` to collect mesh state
4. **ESP32 Root** collects routing table, sequence state, OTA state
5. **ESP32 Root** calls `mesh_udp_bridge_build_state_payload()` to encode binary payload
6. **ESP32 Root** builds UDP packet: `[0xE2][LEN][PAYLOAD][CHKSUM]`
7. **ESP32 Root** sends UDP packet to registered server IP:port `192.168.1.200:8081` (fire-and-forget)
8. **ESP32 Root** continues immediately (no wait for response)
9. **External Server** receives UDP packet in `handleStateUpdatePacket()`
10. **External Server** parses command ID `0xE2` and payload
11. **External Server** extracts all state fields from binary payload
12. **External Server** calls `storeMeshState()` to store in memory Map
13. **External Server** updates last state update timestamp
14. **Web UI** polls `GET /api/mesh/state` (periodic, e.g., every 2 seconds)
15. **External Server** retrieves latest state from storage
16. **External Server** checks staleness (state older than 10 seconds?)
17. **External Server** converts state to JSON format
18. **External Server** returns HTTP response with JSON state
19. **Web UI** receives state and updates UI (node list, topology, sequence progress, etc.)

## Error Handling and Graceful Degradation

### API Proxy Error Handling

#### No Root Node Registered

- **Scenario**: External server has no registered root node
- **HTTP Response**: `404 Not Found`
- **Body**: `{"error": "No root node registered", "message": "..."}`
- **Behavior**: Web UI can still access root node directly via IP

#### Root Node Offline

- **Scenario**: Root node is registered but marked offline (no recent heartbeat/state updates)
- **HTTP Response**: `503 Service Unavailable`
- **Body**: `{"error": "Root node unavailable", "suggestion": "Access root node directly via http://..."}`
- **Behavior**: Provides direct IP address for fallback access

#### UDP Timeout

- **Scenario**: UDP response not received within 8 seconds
- **HTTP Response**: `503 Service Unavailable`
- **Behavior**:
  - GET requests: Return error immediately (no retry)
  - POST requests: Retry up to 3 times with exponential backoff

#### UDP Send Failure

- **Scenario**: Failed to send UDP packet to root node
- **HTTP Response**: `503 Service Unavailable`
- **Behavior**: Root node marked offline after multiple failures

#### Malformed Response

- **Scenario**: UDP response packet is malformed or invalid
- **HTTP Response**: `502 Bad Gateway`
- **Behavior**: Request marked as failed, retry if applicable

#### Sequence Number Mismatch

- **Scenario**: UDP response sequence number doesn't match any pending request
- **Behavior**: Response discarded, log warning

### State Update Error Handling

#### State Collection Failure

- **Scenario**: Failed to collect mesh state (routing table API error, etc.)
- **Behavior**: Log warning, skip this update cycle, continue task
- **Impact**: No state update sent, but task continues (next cycle may succeed)

#### Payload Building Failure

- **Scenario**: Failed to build binary payload (buffer too small, encoding error)
- **Behavior**: Log warning, skip this update cycle, continue task
- **Impact**: No state update sent, but task continues

#### UDP Send Failure

- **Scenario**: Failed to send UDP packet (network error, packet loss)
- **Behavior**: Log at debug level (acceptable for fire-and-forget)
- **Impact**: No state update received by server, but task continues

#### Registration Lost

- **Scenario**: Root node registration lost or expired
- **Behavior**: State update task stops automatically
- **Impact**: No more state updates until re-registration

#### Not Root Node

- **Scenario**: Node is no longer root (role changed)
- **Behavior**: State update task stops automatically
- **Impact**: No more state updates until node becomes root again

### Server-Side Error Handling

#### Missing State

- **Scenario**: `GET /api/mesh/state` called but no state stored
- **HTTP Response**: `404 Not Found`
- **Body**: `{"error": "No mesh state available"}`

#### Stale State

- **Scenario**: State is older than 10 seconds
- **HTTP Response**: `200 OK` with `stale: true` flag
- **Body**: Includes `"warning": "State may be outdated"`

#### Malformed UDP Packet

- **Scenario**: State update packet is malformed or truncated
- **Behavior**: Log warning, discard packet, continue operation

### Graceful Degradation Principles

1. **Embedded Server Always Available**: Root node embedded web server continues operating regardless of external server status
2. **No Blocking Operations**: All UDP operations are non-blocking; failures don't delay mesh operation
3. **Fire-and-Forget Tolerance**: State update packet loss is acceptable; updates are periodic
4. **Automatic Recovery**: System automatically recovers when root node re-registers
5. **Fallback Access**: Web UI can always access root node directly via IP address

## Implementation Details

### ESP32 Side Implementation

#### API Listener Task

The API listener task runs as a FreeRTOS task and listens for UDP API commands:

```c
void mesh_udp_bridge_api_listener_start(void)
{
    // Create UDP socket
    // Bind to port 8082
    // Create FreeRTOS task
    // Start listening loop
}
```

**Task Function**: `mesh_udp_bridge_api_listener_task()`
- Receives UDP packets on port 8082
- Parses packet structure
- Routes commands to appropriate handlers
- Sends UDP responses back to sender

**Integration**: Task starts automatically when root node becomes root and external server is registered.

#### API Command Handlers

Each API endpoint has a dedicated handler function:

```c
static void handle_api_nodes(uint8_t *response, size_t *response_size, size_t max_size)
static void handle_api_color_get(uint8_t *response, size_t *response_size, size_t max_size)
static void handle_api_color_post(const uint8_t *payload, size_t payload_size,
                                   uint8_t *response, size_t *response_size, size_t max_size)
// ... 15 more handlers
```

**Handler Responsibilities**:
- Parse request payload (if applicable)
- Execute mesh operation (e.g., set color, start sequence)
- Build response payload (binary format)
- Return response via `process_api_command()`

#### State Collection Function

```c
esp_err_t mesh_udp_bridge_collect_state(mesh_state_data_t *state)
{
    // Get root IP and mesh ID
    // Get mesh routing table
    // Collect node list with topology
    // Get sequence state
    // Get OTA state
    // Allocate and populate state structure
}
```

**Integration**: Called by state update task before building payload.

#### State Update Task

```c
void mesh_udp_bridge_state_update_task_start(void)
{
    // Create FreeRTOS task
    // Start periodic update loop
}

static void mesh_udp_bridge_state_update_task(void *pvParameters)
{
    while (1) {
        // Check if still root and registered
        // Collect mesh state
        // Build binary payload
        // Send UDP state update
        // Sleep for 3 seconds
    }
}
```

**Integration**: Task starts automatically after successful registration, stops when registration lost or not root.

### Server-Side Implementation

#### Proxy Route Handler

```javascript
async function proxyHandler(req, res) {
    // Set CORS headers
    // Check if root node registered
    // Convert HTTP to UDP command
    // Send UDP command and wait for response
    // Convert UDP response to HTTP response
    // Return HTTP response
}
```

**Integration**: Registered as route handler for `/api/*` in Express app.

#### HTTP to UDP Translator

```javascript
function httpToUdpCommand(req) {
    // Parse HTTP request (method, path, body)
    // Map endpoint to UDP command ID
    // Convert JSON to binary payload
    // Generate sequence number
    // Build UDP packet
    // Return packet and sequence number
}
```

**Files**:
- `lyktparad-server/lib/http-udp-translator.js`: HTTP → UDP conversion
- `lyktparad-server/lib/udp-commands.js`: Command ID mapping

#### UDP to HTTP Translator

```javascript
function udpToHttpResponse(udpResponse) {
    // Parse UDP response packet
    // Extract command ID and payload
    // Match sequence number to pending request
    // Convert binary payload to JSON
    // Build HTTP response object
    // Return HTTP response
}
```

**Files**:
- `lyktparad-server/lib/udp-http-translator.js`: UDP → HTTP conversion

#### UDP Client

```javascript
async function sendUdpCommandAndWait(ip, port, packet, sequenceNumber, timeout) {
    // Send UDP packet
    // Wait for response with matching sequence number
    // Handle timeout
    // Return response packet
}
```

**Files**:
- `lyktparad-server/lib/udp-client.js`: UDP communication

#### State Update Handler

```javascript
function handleStateUpdatePacket(msg, rinfo) {
    // Parse UDP packet (command 0xE2)
    // Extract binary payload
    // Parse all state fields
    // Store state in memory Map
    // Update timestamp
}
```

**Integration**: Called from UDP server message handler when command ID is `0xE2`.

#### State Storage

```javascript
function storeMeshState(root_ip, mesh_id, timestamp, mesh_state, node_count,
                        nodes, sequence_state, ota_state) {
    // Convert mesh_id to hex string (key)
    // Create state object
    // Store in Map
    // Return state object
}

function getFirstMeshState() {
    // Get first state from Map (single mesh network)
    // Return state object or null
}
```

**Files**:
- `lyktparad-server/lib/state-storage.js`: State storage module

## API Reference

### ESP32 Side API

#### Function: `mesh_udp_bridge_api_listener_start()`

Start the UDP API listener task.

**Declaration**:
```c
void mesh_udp_bridge_api_listener_start(void);
```

**Description**: Creates and starts a FreeRTOS task that listens for UDP API commands on port 8082. The task processes incoming commands and sends responses back to the sender.

**Notes**:
- Only starts if node is root
- Task automatically stops if node is no longer root
- Completely optional; does not affect embedded web server

**Integration**: Called automatically when root node becomes root and external server is registered.

#### Function: `mesh_udp_bridge_api_listener_stop()`

Stop the UDP API listener task.

**Declaration**:
```c
void mesh_udp_bridge_api_listener_stop(void);
```

**Description**: Stops the UDP API listener task and cleans up resources.

**Integration**: Called automatically when node is no longer root or registration is lost.

#### Function: `mesh_udp_bridge_collect_state()`

Collect current mesh network state.

**Declaration**:
```c
esp_err_t mesh_udp_bridge_collect_state(mesh_state_data_t *state);
```

**Parameters**:
- `state`: Pointer to state data structure to populate

**Returns**: `ESP_OK` on success, error code on failure

**Description**: Collects complete mesh network state including routing table, sequence state, and OTA status. Allocates memory for node list (caller must free).

**Integration**: Called by state update task before sending state update.

#### Function: `mesh_udp_bridge_build_state_payload()`

Build binary payload from state data.

**Declaration**:
```c
esp_err_t mesh_udp_bridge_build_state_payload(const mesh_state_data_t *state,
                                               uint8_t *buffer, size_t buffer_size,
                                               size_t *payload_size);
```

**Parameters**:
- `state`: Pointer to state data structure
- `buffer`: Output buffer for binary payload
- `buffer_size`: Size of output buffer
- `payload_size`: Output parameter for actual payload size

**Returns**: `ESP_OK` on success, `ESP_ERR_INVALID_SIZE` if buffer too small

**Description**: Encodes state data structure into binary payload format for UDP transmission. Uses network byte order for multi-byte values.

**Integration**: Called by state update task before sending UDP packet.

#### Function: `mesh_udp_bridge_send_state_update()`

Send state update to external server.

**Declaration**:
```c
esp_err_t mesh_udp_bridge_send_state_update(const uint8_t *payload, size_t payload_size);
```

**Parameters**:
- `payload`: Binary payload buffer
- `payload_size`: Size of payload in bytes

**Returns**: `ESP_OK` on send attempt (even if send fails), `ESP_ERR_INVALID_STATE` if not registered or not root

**Description**: Sends state update UDP packet to registered external server. Fire-and-forget (no ACK required). Returns OK even if send fails (packet loss acceptable).

**Integration**: Called by state update task after building payload.

#### Function: `mesh_udp_bridge_state_update_task_start()`

Start the state update task.

**Declaration**:
```c
esp_err_t mesh_udp_bridge_state_update_task_start(void);
```

**Returns**: `ESP_OK` on success, error code on failure

**Description**: Creates and starts a FreeRTOS task that periodically collects and sends mesh state updates.

**Notes**:
- Only starts if node is root and registered
- Task runs at low priority
- Update interval: 3 seconds (default)

**Integration**: Called automatically after successful registration.

#### Function: `mesh_udp_bridge_state_update_task_stop()`

Stop the state update task.

**Declaration**:
```c
esp_err_t mesh_udp_bridge_state_update_task_stop(void);
```

**Description**: Stops the state update task and cleans up resources.

**Integration**: Called automatically when registration is lost or node is no longer root.

### Server-Side API

#### Function: `proxyHandler(req, res)`

Express route handler for API proxy.

**Declaration**:
```javascript
async function proxyHandler(req, res)
```

**Parameters**:
- `req`: Express request object
- `res`: Express response object

**Description**: Handles all `/api/*` requests, translates HTTP to UDP, sends to root node, and returns HTTP response.

**Integration**: Registered as `app.all('/api/*', proxyHandler)` in Express app.

#### Function: `httpToUdpCommand(req)`

Convert HTTP request to UDP command.

**Declaration**:
```javascript
function httpToUdpCommand(req)
```

**Parameters**:
- `req`: Express request object

**Returns**: Object with `packet` (Buffer) and `sequenceNumber` (number), or null if endpoint not mapped

**Description**: Parses HTTP request, maps endpoint to UDP command ID, converts JSON to binary, and builds UDP packet.

**Integration**: Called by `proxyHandler()` before sending UDP command.

#### Function: `udpToHttpResponse(udpResponse)`

Convert UDP response to HTTP response.

**Declaration**:
```javascript
function udpToHttpResponse(udpResponse)
```

**Parameters**:
- `udpResponse`: Parsed UDP response object

**Returns**: Object with `status` (number), `json` (object), or `text` (string)

**Description**: Parses UDP response packet, converts binary payload to JSON, and builds HTTP response object.

**Integration**: Called by `proxyHandler()` after receiving UDP response.

#### Function: `sendUdpCommandAndWait(ip, port, packet, sequenceNumber, timeout)`

Send UDP command and wait for response.

**Declaration**:
```javascript
async function sendUdpCommandAndWait(ip, port, packet, sequenceNumber, timeout)
```

**Parameters**:
- `ip`: Root node IP address (string)
- `port`: Root node UDP port (number, typically 8082)
- `packet`: UDP packet buffer
- `sequenceNumber`: Sequence number for matching
- `timeout`: Timeout in milliseconds (default: 8000)

**Returns**: Promise resolving to parsed UDP response object

**Description**: Sends UDP packet and waits for response with matching sequence number. Handles timeout and retry logic.

**Integration**: Called by `proxyHandler()` to communicate with root node.

#### Function: `handleStateUpdatePacket(msg, rinfo)`

Handle incoming state update UDP packet.

**Declaration**:
```javascript
function handleStateUpdatePacket(msg, rinfo)
```

**Parameters**:
- `msg`: UDP message buffer
- `rinfo`: Remote address info

**Description**: Parses state update packet (command 0xE2), extracts all state fields, and stores in state storage.

**Integration**: Called from UDP server message handler when command ID is `0xE2`.

#### Function: `storeMeshState(root_ip, mesh_id, timestamp, mesh_state, node_count, nodes, sequence_state, ota_state)`

Store mesh state in memory.

**Declaration**:
```javascript
function storeMeshState(root_ip, mesh_id, timestamp, mesh_state, node_count, nodes, sequence_state, ota_state)
```

**Parameters**:
- `root_ip`: IPv4 address buffer (4 bytes)
- `mesh_id`: Mesh ID buffer (6 bytes)
- `timestamp`: Unix timestamp (number)
- `mesh_state`: Mesh state (number: 0=disconnected, 1=connected)
- `node_count`: Number of nodes (number)
- `nodes`: Array of node entry objects
- `sequence_state`: Sequence state object
- `ota_state`: OTA state object

**Returns**: State object

**Description**: Stores or updates mesh state in memory Map, keyed by mesh ID.

**Integration**: Called by `handleStateUpdatePacket()` after parsing state.

#### Function: `getFirstMeshState()`

Get the first stored mesh state.

**Declaration**:
```javascript
function getFirstMeshState()
```

**Returns**: State object or null if none stored

**Description**: Retrieves the first mesh state from storage (for single mesh network).

**Integration**: Called by `GET /api/mesh/state` endpoint handler.

## Integration Points

### ESP32 Side Integration

#### Root Node Initialization

The API listener and state update tasks are started automatically when:
1. Node becomes root (`esp_mesh_is_root()` returns true)
2. External server is discovered and registered

**Integration Point**: `mesh_main.c` - Root node action handler after web server starts

#### Registration Success

After successful registration with external server:
1. `mesh_udp_bridge_api_listener_start()` is called
2. `mesh_udp_bridge_state_update_task_start()` is called

**Integration Point**: `mesh_udp_bridge.c` - Registration ACK handler

#### Registration Lost

When registration is lost or root node role changes:
1. `mesh_udp_bridge_api_listener_stop()` is called
2. `mesh_udp_bridge_state_update_task_stop()` is called

**Integration Point**: `mesh_udp_bridge.c` - Registration failure handler, root role change detection

#### API Command Processing

API commands are processed by calling existing mesh operation functions:
- Color commands: `mesh_root_handle_rgb_command()`
- Sequence commands: `mode_sequence_root_*()` functions
- OTA commands: `mesh_ota_*()` functions
- Node list: `esp_mesh_get_routing_table()`

**Integration Point**: `mesh_udp_bridge.c` - API command handler functions

### Server-Side Integration

#### Express Route Registration

API proxy route is registered before static file serving:

```javascript
// API proxy routes (before static file serving)
app.all('/api/*', proxyHandler);
```

**Integration Point**: `server.js` - Express app setup

#### UDP Server Message Handling

State update packets are handled in UDP server message handler:

```javascript
udpServer.on('message', (msg, rinfo) => {
    const commandId = msg[0];
    if (commandId === UDP_CMD_STATE_UPDATE) {
        handleStateUpdatePacket(msg, rinfo);
    }
    // ... other command handlers
});
```

**Integration Point**: `server.js` - UDP server setup

#### State Retrieval Endpoint

State retrieval endpoint is registered as a GET route:

```javascript
app.get('/api/mesh/state', (req, res) => {
    const state = getFirstMeshState();
    // ... return JSON response
});
```

**Integration Point**: `server.js` - Express route setup

#### Registration Storage Access

API proxy accesses registered root node information from registration storage:

```javascript
const { getFirstRegisteredRootNode } = require('./lib/registration');
const rootNode = getFirstRegisteredRootNode();
```

**Integration Point**: `routes/proxy.js` - Root node lookup

---

**See Also**:
- [Root Node Registration and Communication Lifecycle](root-node-registration-lifecycle.md) - Registration, heartbeat, and disconnection handling
- [External Web Discovery](external-web-discovery.md) - Server discovery via mDNS and UDP broadcast
- [Web to Root UDP Protocol](ext-web-udp-protocol.md) - Complete UDP protocol specification
