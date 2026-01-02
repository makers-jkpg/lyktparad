# Mesh Firmware Distribution Protocol Implementation

**Last Updated:** 2025-01-15

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Protocol Specification](#protocol-specification)
4. [API Reference](#api-reference)
5. [Integration Guide](#integration-guide)
6. [Testing Guide](#testing-guide)
7. [Error Handling](#error-handling)

## Overview

### Purpose

The Mesh Firmware Distribution Protocol enables the root node to distribute downloaded firmware to all leaf nodes in the mesh network. The implementation uses a block-based distribution model with acknowledgment tracking, retry logic, and progress monitoring.

### Design Decisions

**Block-based distribution**: Firmware is fragmented into 1KB blocks to fit within ESP-MESH message size limits and enable efficient retry of failed blocks.

**Acknowledgment tracking**: Each node acknowledges receipt of each block, allowing the root to track which nodes have which blocks and retry failed blocks.

**Sequential block sending**: Blocks are sent sequentially to avoid overwhelming the mesh network and to enable accurate progress tracking.

**Retry logic**: Failed blocks are retried up to 3 times per node before marking the node as failed for that block.

**Golden Image pattern**: Root downloads firmware once and distributes it to all nodes, minimizing bandwidth usage.

**Downgrade prevention**: Before distribution starts, the firmware version is verified to ensure it's not older than the current version. See [OTA Downgrade Prevention](ota-downgrade-prevention.md) for details.

> **For Users**: See [User Guide](../../user-guides/ota-updates.md) for usage instructions.

## Architecture

### Data Flow

```
Root: Firmware Downloaded
    ↓
Distribution Initiated (via API or OTA_REQUEST)
    ↓
Get Routing Table → Filter Root Node → Get Target Nodes
    ↓
Fragment Firmware → Calculate Blocks → Allocate Tracking Structures
    ↓
Send OTA_START to All Nodes
    ↓
For Each Block (0 to total_blocks-1):
    ├─→ Send Block to All Nodes That Need It
    ├─→ Wait for ACKs (5 second timeout)
    ├─→ Track Received ACKs
    ├─→ Retry Failed Blocks (max 3 retries)
    └─→ Update Progress
    ↓
Distribution Complete
```

### Command Protocol

The protocol uses mesh commands defined in `include/mesh_commands.h`:

- **MESH_CMD_OTA_REQUEST** (0xF0): Leaf node requests firmware update
- **MESH_CMD_OTA_START** (0xF1): Root starts distribution with metadata
- **MESH_CMD_OTA_BLOCK** (0xF2): Firmware block data with header
- **MESH_CMD_OTA_ACK** (0xF3): Block acknowledgment from leaf node
- **MESH_CMD_OTA_STATUS** (0xF4): Query distribution status
- **MESH_CMD_OTA_PREPARE_REBOOT** (0xF5): Prepare for coordinated reboot
- **MESH_CMD_OTA_REBOOT** (0xF6): Execute coordinated reboot

**Note**: Commands with prefix 0xF are reserved for OTA/update functionality.

### Block Structure

Each OTA_BLOCK message contains:

- **Header** (10 bytes):
  - Command byte (1): `MESH_CMD_OTA_BLOCK`
  - Block number (2): Block index (0-based, big-endian)
  - Total blocks (2): Total number of blocks (big-endian)
  - Block size (2): Size of this block in bytes (big-endian)
  - Checksum (4): CRC32 checksum of block data (big-endian)
- **Data** (up to 1KB): Firmware block payload
- **Total message size**: ~1KB + 10 bytes header

### State Management

Distribution state is managed in `src/mesh_ota.c`:

- **Distribution active flag**: Tracks if distribution is in progress
- **Per-node block bitmap**: Tracks which blocks each node has received
- **Retry count array**: Tracks retry count per node per block
- **Progress tracking**: Calculates overall progress based on blocks received
- **Event group**: Used for ACK synchronization between distribution task and message handler

## Protocol Specification

### OTA_START Message

Sent from root to all nodes to initiate distribution.

**Structure** (`mesh_ota_start_t`):
```c
typedef struct {
    uint8_t cmd;              // MESH_CMD_OTA_START (0xF1)
    uint16_t total_blocks;    // Total number of blocks (big-endian)
    uint32_t firmware_size;   // Total firmware size in bytes (big-endian)
    char version[16];         // Firmware version string (null-terminated)
} mesh_ota_start_t;
```

**Size**: 23 bytes

**Behavior**:
- Root sends OTA_START to all nodes in routing table
- Nodes receive OTA_START and prepare for block reception
- Nodes should clear any previous OTA state

### OTA_BLOCK Message

Sent from root to nodes to transmit firmware blocks.

**Header Structure** (`mesh_ota_block_header_t`):
```c
typedef struct {
    uint8_t cmd;              // MESH_CMD_OTA_BLOCK (0xF2)
    uint16_t block_number;    // Block number (0-based, big-endian)
    uint16_t total_blocks;    // Total number of blocks (big-endian)
    uint16_t block_size;      // Size of this block in bytes (big-endian)
    uint32_t checksum;        // CRC32 checksum (big-endian)
} mesh_ota_block_header_t;
```

**Message Structure**:
- Header (10 bytes) + Block data (up to 1KB)

**Behavior**:
- Root sends block to nodes that haven't acknowledged it yet
- Nodes receive block, verify checksum, write to OTA partition, send ACK
- Root waits for ACK with 5-second timeout
- Root retries failed blocks up to 3 times per node

### OTA_ACK Message

Sent from leaf nodes to root to acknowledge block receipt.

**Structure** (`mesh_ota_ack_t`):
```c
typedef struct {
    uint8_t cmd;              // MESH_CMD_OTA_ACK (0xF3)
    uint16_t block_number;    // Block number being acknowledged (big-endian)
    uint8_t status;           // Status: 0=OK, 1=ERROR
} mesh_ota_ack_t;
```

**Size**: 4 bytes

**Behavior**:
- Node sends ACK after successfully receiving and verifying a block
- Root updates per-node block tracking on ACK receipt
- Root uses ACK to determine which blocks to retry

### OTA_REQUEST Message

Sent from leaf nodes to root to request firmware update.

**Structure**: Single byte command (`MESH_CMD_OTA_REQUEST`)

**Size**: 1 byte

**Behavior**:
- Leaf node sends OTA_REQUEST when it wants to receive an update
- Root checks if firmware is available and starts distribution if not already in progress
- If distribution is already in progress, request is ignored

### OTA_STATUS Message

Query distribution status (currently only logged, not fully implemented).

**Structure** (`mesh_ota_status_t`):
```c
typedef struct {
    uint8_t cmd;              // MESH_CMD_OTA_STATUS (0xF4)
    uint8_t request_type;     // 0=query progress, 1=query failed blocks
} mesh_ota_status_t;
```

## API Reference

### `esp_err_t mesh_ota_distribute_firmware(void)`

Start firmware distribution to all mesh nodes.

**Description**: Distributes the firmware from the inactive OTA partition to all leaf nodes in the mesh network. This function can only be called on the root node.

**Returns**:
- `ESP_OK` on success
- `ESP_ERR_INVALID_STATE` if not root node or distribution already in progress
- `ESP_ERR_NOT_FOUND` if no update partition or no target nodes
- `ESP_ERR_NO_MEM` if memory allocation fails
- `ESP_ERR_INVALID_SIZE` if firmware size is invalid

**Example**:
```c
esp_err_t err = mesh_ota_distribute_firmware();
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Distribution started");
}
```

### `esp_err_t mesh_ota_get_distribution_status(mesh_ota_distribution_status_t *status)`

Get distribution status.

**Parameters**:
- `status`: Pointer to status structure to fill

**Returns**:
- `ESP_OK` on success
- `ESP_ERR_INVALID_ARG` if status pointer is NULL

**Example**:
```c
mesh_ota_distribution_status_t status;
esp_err_t err = mesh_ota_get_distribution_status(&status);
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Progress: %.1f%%, Nodes: %d/%d complete",
             status.overall_progress * 100.0f,
             status.nodes_complete, status.nodes_total);
}
```

### `float mesh_ota_get_distribution_progress(void)`

Get distribution progress.

**Returns**: Progress value (0.0-1.0), or 0.0 if not distributing

**Example**:
```c
float progress = mesh_ota_get_distribution_progress();
ESP_LOGI(TAG, "Progress: %.1f%%", progress * 100.0f);
```

### `esp_err_t mesh_ota_cancel_distribution(void)`

Cancel ongoing distribution.

**Description**: Cancels the current distribution operation if one is in progress. This function is idempotent.

**Returns**: `ESP_OK` on success

**Example**:
```c
esp_err_t err = mesh_ota_cancel_distribution();
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Distribution cancelled");
}
```

### `esp_err_t mesh_ota_register_progress_callback(mesh_ota_progress_callback_t callback)`

Register progress callback.

**Parameters**:
- `callback`: Callback function pointer, or NULL to unregister

**Returns**: `ESP_OK` on success

**Callback Signature**:
```c
typedef void (*mesh_ota_progress_callback_t)(
    float overall_progress,    // Overall progress (0.0-1.0)
    int nodes_complete,        // Number of nodes that completed
    int nodes_total,           // Total number of nodes
    int blocks_sent,           // Number of blocks sent so far
    int blocks_total           // Total number of blocks
);
```

**Example**:
```c
void progress_cb(float progress, int complete, int total, int sent, int total_blocks) {
    ESP_LOGI(TAG, "Progress: %.1f%% (%d/%d nodes)", progress * 100.0f, complete, total);
}

mesh_ota_register_progress_callback(progress_cb);
```

### `esp_err_t mesh_ota_handle_mesh_message(mesh_addr_t *from, uint8_t *data, uint16_t len)`

Handle OTA message from mesh.

**Description**: Processes incoming OTA messages from leaf nodes (OTA_REQUEST, OTA_ACK, OTA_STATUS). This function should be called from the mesh receive handler.

**Parameters**:
- `from`: Source node address
- `data`: Message data
- `len`: Message length

**Returns**:
- `ESP_OK` on success
- `ESP_ERR_INVALID_ARG` if parameters are invalid
- `ESP_ERR_INVALID_STATE` if not root node
- `ESP_ERR_NOT_SUPPORTED` if message type is not supported

**Example**:
```c
/* In mesh receive handler */
if (data.proto == MESH_PROTO_BIN && data.size >= 1) {
    uint8_t cmd = data.data[0];
    if (cmd == MESH_CMD_OTA_REQUEST || cmd == MESH_CMD_OTA_ACK || cmd == MESH_CMD_OTA_STATUS) {
        mesh_ota_handle_mesh_message(&from, data.data, data.size);
    }
}
```

## Integration Guide

### Root Node Integration

The distribution module is automatically integrated with the root node receive handler in `src/mesh_child.c`. Root nodes process OTA messages (OTA_REQUEST, OTA_ACK, OTA_STATUS) in the mesh receive loop.

### Web API Integration

The distribution module provides web API endpoints in `src/mesh_web.c`:

- `POST /api/ota/distribute`: Start distribution
- `GET /api/ota/distribution/status`: Get distribution status
- `GET /api/ota/distribution/progress`: Get progress
- `POST /api/ota/distribution/cancel`: Cancel distribution

### Leaf Node Integration

Leaf nodes need to implement handlers for OTA_START and OTA_BLOCK messages. This is typically done in the mesh receive handler by checking for `MESH_CMD_OTA_START` and `MESH_CMD_OTA_BLOCK` commands.

When receiving OTA_START:
- Store metadata (total_blocks, firmware_size, version)
- Prepare OTA partition for writing
- Clear any previous OTA state

When receiving OTA_BLOCK:
- Verify block checksum
- Write block to OTA partition at correct offset
- Send OTA_ACK with block_number and status=0

### Task Configuration

The distribution task is created with:
- Stack size: 8192 bytes
- Priority: 5
- Name: "ota_distribute"

The task runs until distribution completes or is cancelled.

## Testing Guide

### Unit Testing

Test block fragmentation:
- Verify block size calculation for various firmware sizes
- Test checksum calculation
- Test block header generation

Test distribution logic:
- Test distribution state management
- Test routing table handling
- Test block sending logic
- Test ACK tracking

### Integration Testing

**Small Mesh Test (2-3 nodes)**:
- Start distribution with small mesh
- Verify all nodes receive all blocks
- Test ACK handling
- Test progress tracking

**Large Mesh Test (10+ nodes)**:
- Test distribution with larger mesh
- Test performance and timing
- Test node disconnection handling
- Test partial distribution scenarios

**Error Scenario Tests**:
- Test node disconnection during distribution
- Test mesh send failures
- Test timeout scenarios
- Test retry logic
- Test distribution cancellation

**Edge Case Tests**:
- Test distribution with nodes at different mesh layers
- Test simultaneous OTA requests
- Test distribution when firmware not available
- Test distribution cancellation mid-way

### Test Commands

```bash
# Start distribution
curl -X POST http://<root-ip>/api/ota/distribute

# Check status
curl http://<root-ip>/api/ota/distribution/status

# Check progress
curl http://<root-ip>/api/ota/distribution/progress

# Cancel distribution
curl -X POST http://<root-ip>/api/ota/distribution/cancel
```

## Error Handling

### Mesh Send Failures

If `esp_mesh_send()` fails for a node:
- Error is logged
- Node is tracked as needing retry
- Distribution continues for other nodes
- Failed node is retried up to 3 times per block

### Node Disconnection

If a node disconnects during distribution:
- Node is removed from active distribution list
- Distribution continues for remaining nodes
- Disconnected node can reconnect and request update again
- Distribution status shows failed nodes

### ACK Timeout

If a node doesn't ACK within 5 seconds:
- Block is marked for retry
- Retry count is incremented
- If retries exceed maximum, node is marked as failed for that block
- Distribution continues with remaining nodes

### Partial Distribution

If some nodes have all blocks but others don't:
- Distribution status shows nodes_complete and nodes_failed
- Failed nodes can retry by requesting update again
- Distribution can be cancelled and restarted if needed

### Memory Errors

If memory allocation fails:
- Distribution fails with `ESP_ERR_NO_MEM`
- All allocated resources are cleaned up
- Error is logged

### Partition Errors

If partition read/write fails:
- Distribution fails immediately
- Error is logged
- All nodes are notified of failure via timeout

## Related Documentation

- [OTA Foundation](ota-foundation.md) - Partition table and version management
- [OTA Download](ota-download.md) - Root node firmware download
- [User Guide](../../user-guides/ota-updates.md) - User-facing documentation
- [ESP-MESH API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp-wifi-mesh.html) - ESP-IDF documentation
