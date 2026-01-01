# Leaf Node OTA Handler & Coordinated Reboot Implementation

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

The Leaf Node OTA Handler enables leaf nodes to receive firmware blocks from the root node, write them to the inactive OTA partition, and participate in coordinated reboots. This completes the OTA update flow initiated by the root node's distribution protocol.

### Design Decisions

**Block-based reception**: Leaf nodes receive firmware in 1KB blocks matching the root's distribution protocol, enabling efficient error recovery and retry mechanisms.

**Checksum verification**: Each block is verified using CRC32 checksums before writing to ensure data integrity.

**Partition validation**: After all blocks are received, the partition is finalized and validated using ESP-IDF OTA APIs.

**Coordinated reboot**: All nodes reboot simultaneously after verifying firmware readiness, ensuring version consistency across the mesh network.

> **For Users**: See [User Guide](../../user-guides/ota-updates.md) for usage instructions.

## Architecture

### Data Flow

```
Root: Sends OTA_START
    ↓
Leaf: Receives OTA_START
    - Initialize OTA partition
    - Allocate block tracking bitmap
    - Prepare for block reception
    ↓
Root: Sends OTA_BLOCK (repeated for each block)
    ↓
Leaf: Receives OTA_BLOCK
    - Verify checksum
    - Write to partition
    - Send OTA_ACK
    ↓
Leaf: All blocks received?
    - Yes → Finalize partition (esp_ota_end)
    - Verify partition validity
    ↓
Root: Distribution complete → Sends PREPARE_REBOOT
    ↓
Leaf: Receives PREPARE_REBOOT
    - Verify firmware ready
    - Send ACK (status=0 if ready, status=1 if not)
    ↓
Root: All nodes ready → Sends REBOOT
    ↓
Leaf: Receives REBOOT
    - Set boot partition
    - Reboot (esp_restart)
```

### State Management

Leaf node OTA state is managed in `src/mesh_ota.c`:

- **Receiving flag**: Tracks if OTA reception is in progress
- **OTA handle**: ESP-IDF OTA handle for partition writing
- **Update partition**: Pointer to inactive OTA partition
- **Block bitmap**: Tracks which blocks have been received
- **Metadata**: Total blocks, firmware size, version string
- **Completion flag**: Indicates firmware is complete and validated
- **Last block time**: Tracks timeout for block reception

## Protocol Specification

### OTA_START Message Handling

**Received from**: Root node
**Message Structure**: `mesh_ota_start_t` (23 bytes)

**Leaf Node Behavior**:
1. Validate message size
2. Convert byte order (big-endian to little-endian)
3. Get inactive OTA partition using `esp_ota_get_next_update_partition()`
4. Initialize OTA operation with `esp_ota_begin()`
5. Allocate block tracking bitmap
6. Store metadata (total_blocks, firmware_size, version)
7. Set receiving flag to true

**Error Handling**:
- If partition not found: Log error and return ESP_ERR_NOT_FOUND
- If OTA begin fails: Log error and return error code
- If bitmap allocation fails: Abort OTA and return ESP_ERR_NO_MEM

### OTA_BLOCK Message Handling

**Received from**: Root node
**Message Structure**: Header (10 bytes) + Block data (up to 1KB)

**Leaf Node Behavior**:
1. Parse block header (`mesh_ota_block_header_t`)
2. Validate message size
3. Extract block number, block size, checksum
4. Verify block number is within expected range
5. Check if block already received (prevent duplicates)
6. Calculate CRC32 of block data
7. Compare with header checksum
8. If checksum matches:
   - Write block to partition using `esp_ota_write()`
   - Mark block as received in bitmap
   - Update bytes written counter
   - Send OTA_ACK with status=0 (success)
9. If checksum fails:
   - Log error
   - Send OTA_ACK with status=1 (error)
10. Check if all blocks received:
    - If yes: Call `esp_ota_end()` to finalize
    - Validate partition integrity
    - Set completion flag

**Error Handling**:
- Invalid block number: Send error ACK, return ESP_ERR_INVALID_ARG
- Checksum mismatch: Send error ACK, return ESP_ERR_INVALID_ARG
- Write failures: Send error ACK, abort on critical errors
- Timeout: If no blocks received for 30 seconds, abort OTA

### OTA_ACK Message Sending

**Sent to**: Root node
**Message Structure**: `mesh_ota_ack_t` (4 bytes)

**Usage**: Leaf nodes send ACK after processing each block:
- status=0: Block received and written successfully
- status=1: Block failed (checksum error, write error, etc.)

### PREPARE_REBOOT Message Handling

**Received from**: Root node
**Message Structure**: `mesh_ota_prepare_reboot_t` (19 bytes)

**Leaf Node Behavior**:
1. Parse message (timeout, version)
2. Verify firmware is complete (`s_leaf_firmware_complete == true`)
3. Verify partition is valid using `esp_ota_get_state_partition()`
4. Send ACK with:
   - status=0: Firmware ready for reboot
   - status=1: Firmware not ready (incomplete or invalid)

**Error Handling**:
- Firmware not complete: Send status=1
- Partition validation fails: Send status=1, log error

### REBOOT Message Handling

**Received from**: Root node
**Message Structure**: `mesh_ota_reboot_t` (3 bytes)

**Leaf Node Behavior**:
1. Parse message (reboot delay)
2. Verify firmware is complete and ready
3. Set boot partition to new OTA partition using `esp_ota_set_boot_partition()`
4. Verify boot partition was set correctly using `esp_ota_get_boot_partition()`
5. Wait for specified delay (if any)
6. Call `esp_restart()` to reboot

**Error Handling**:
- Firmware not ready: Return ESP_ERR_INVALID_STATE
- Boot partition set fails: Log error and return error code

### OTA_REQUEST Message Sending

**Sent to**: Root node
**Message Structure**: Single byte (`MESH_CMD_OTA_REQUEST`)

**Usage**: Leaf nodes can request firmware update by calling `mesh_ota_request_update()`, which sends this message to the root node.

## API Reference

### `esp_err_t mesh_ota_handle_leaf_message(mesh_addr_t *from, uint8_t *data, uint16_t len)`

Handle OTA message from mesh (leaf node only).

**Description**: Processes incoming OTA messages from root node (OTA_START, OTA_BLOCK, PREPARE_REBOOT, REBOOT). This function should be called from the mesh receive handler.

**Parameters**:
- `from`: Source node address
- `data`: Message data
- `len`: Message length

**Returns**:
- `ESP_OK` on success
- `ESP_ERR_INVALID_ARG` if parameters are invalid
- `ESP_ERR_INVALID_SIZE` if message size is invalid for command type
- `ESP_ERR_INVALID_STATE` if called on root node or firmware not ready (for REBOOT)
- `ESP_ERR_NOT_SUPPORTED` if message type is not supported

**Example**:
```c
/* In mesh receive handler */
if (data.proto == MESH_PROTO_BIN && data.size >= 1) {
    uint8_t cmd = data.data[0];
    if (cmd == MESH_CMD_OTA_START || cmd == MESH_CMD_OTA_BLOCK ||
        cmd == MESH_CMD_OTA_PREPARE_REBOOT || cmd == MESH_CMD_OTA_REBOOT) {
        mesh_ota_handle_leaf_message(&from, data.data, data.size);
    }
}
```

### `esp_err_t mesh_ota_request_update(void)`

Request firmware update from root node.

**Description**: Sends an OTA_REQUEST message to the root node to initiate firmware distribution. This function can only be called on leaf nodes.

**Returns**:
- `ESP_OK` on success
- `ESP_ERR_INVALID_STATE` if called on root node or update already in progress
- `ESP_ERR_NOT_FOUND` if root node address cannot be determined

**Example**:
```c
esp_err_t err = mesh_ota_request_update();
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Update requested");
}
```

### `esp_err_t mesh_ota_cleanup_on_disconnect(void)`

Cleanup OTA reception on mesh disconnection.

**Description**: This function should be called when the mesh connection is lost to clean up any ongoing OTA reception state. It's safe to call even if no OTA is in progress.

**Returns**: `ESP_OK` on success

**Example**:
```c
/* In mesh event handler */
case MESH_EVENT_PARENT_DISCONNECTED:
    mesh_ota_cleanup_on_disconnect();
    break;
```

### `esp_err_t mesh_ota_initiate_coordinated_reboot(uint16_t timeout_seconds, uint16_t reboot_delay_ms)`

Initiate coordinated reboot of all mesh nodes (root node only).

**Description**: Coordinates a reboot of all nodes in the mesh network. First sends PREPARE_REBOOT to all nodes and waits for acknowledgments. If all nodes are ready, sends REBOOT command to trigger simultaneous reboot.

**Parameters**:
- `timeout_seconds`: Timeout for waiting for PREPARE_REBOOT ACKs
- `reboot_delay_ms`: Delay before reboot in milliseconds

**Returns**:
- `ESP_OK` on success
- `ESP_ERR_INVALID_STATE` if not root node or distribution not complete
- `ESP_ERR_TIMEOUT` if not all nodes ready within timeout
- `ESP_ERR_NOT_FOUND` if no target nodes available

**Example**:
```c
/* After distribution completes */
esp_err_t err = mesh_ota_initiate_coordinated_reboot(10, 1000);
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Reboot initiated");
}
```

## Integration Guide

### Mesh Receive Handler Integration

The leaf node OTA handler is integrated into the mesh receive loop in `src/mesh_child.c`:

```c
void esp_mesh_p2p_rx_main(void *arg)
{
    // ... existing code ...

    if (esp_mesh_is_root()) {
        /* Root node handles OTA messages separately */
        // ... root handling ...
        continue;
    }

    // ... existing message handling ...

    else if (data.proto == MESH_PROTO_BIN && data.size >= 1) {
        /* Check for OTA messages (leaf nodes only) */
        uint8_t cmd = data.data[0];
        if (cmd == MESH_CMD_OTA_START || cmd == MESH_CMD_OTA_BLOCK ||
            cmd == MESH_CMD_OTA_PREPARE_REBOOT || cmd == MESH_CMD_OTA_REBOOT) {
            mesh_ota_handle_leaf_message(&from, data.data, data.size);
        }
    }
}
```

### Mesh Disconnection Handling

To handle mesh disconnections during OTA, call the cleanup function from the mesh event handler:

```c
/* In mesh_common.c event handler */
case MESH_EVENT_PARENT_DISCONNECTED:
    mesh_ota_cleanup_on_disconnect();
    break;
```

### State Management

Leaf node OTA state is managed using static variables in `src/mesh_ota.c`:
- State is thread-safe within the message handler context
- State is cleaned up automatically on completion or error
- State persists until reboot (for reboot coordination)

### Web API Integration (Root Node)

The reboot coordination API is exposed via web endpoint in `src/mesh_web.c`:
- `POST /api/ota/reboot`: Initiate coordinated reboot
- Optional JSON body: `{"timeout": 10, "delay": 1000}`

## Testing Guide

### Unit Testing

**Block Reception**:
- Test OTA_START handler with valid message
- Test OTA_START handler with invalid message size
- Test OTA_BLOCK handler with valid block (checksum OK)
- Test OTA_BLOCK handler with invalid checksum
- Test duplicate block handling
- Test out-of-order block handling
- Test block write failures
- Test completion detection (all blocks received)

**Reboot Coordination**:
- Test PREPARE_REBOOT handler when firmware ready
- Test PREPARE_REBOOT handler when firmware not ready
- Test REBOOT handler with valid state
- Test REBOOT handler with invalid state

### Integration Testing

**Full Update Flow**:
1. Root downloads firmware
2. Root distributes to 2-3 leaf nodes
3. Verify leaf nodes receive all blocks
4. Verify leaf nodes send ACKs correctly
5. Verify partition is finalized on leaf nodes
6. Initiate coordinated reboot
7. Verify all nodes reboot and boot with new firmware

**Error Scenarios**:
- Leaf node disconnects during block reception
- Block checksum failures
- Partition write failures
- Incomplete firmware (missing blocks)
- Reboot coordination timeout
- Node not ready for reboot

### Test Commands

```bash
# Request update from leaf node (via serial/logs, or implement GPIO trigger)
# Or wait for root to distribute

# Initiate coordinated reboot from root
curl -X POST http://<root-ip>/api/ota/reboot

# With custom parameters
curl -X POST http://<root-ip>/api/ota/reboot \
  -H "Content-Type: application/json" \
  -d '{"timeout": 15, "delay": 2000}'
```

## Error Handling

### Mesh Disconnection

**Detection**: Handled via `MESH_EVENT_PARENT_DISCONNECTED` event
**Action**: Cleanup OTA reception state, abort OTA operation
**Recovery**: Node can reconnect and request update again

### Partition Write Errors

**ESP_ERR_INVALID_SIZE**: Block size exceeds partition space
- Action: Send error ACK, abort OTA
- Recovery: Restart update process

**ESP_ERR_OTA_VALIDATE_FAILED**: Partition validation failed during write
- Action: Send error ACK, abort OTA
- Recovery: Restart update process

**ESP_ERR_NO_MEM**: Insufficient memory
- Action: Abort OTA
- Recovery: Restart after memory is available

### Checksum Failures

**Detection**: Calculated CRC32 doesn't match header checksum
**Action**: Send error ACK with status=1
**Recovery**: Root will retry block (up to 3 times)

### Incomplete Firmware

**Detection**: Timeout (30 seconds) without receiving blocks
**Action**: Abort OTA reception, cleanup state
**Recovery**: Request update again

**Detection**: Not all blocks received when distribution completes
**Action**: Partition not finalized, firmware marked as incomplete
**Recovery**: Request update again

### Reboot Failures

**Boot partition set failure**: Partition cannot be set as boot partition
- Action: Log error, return error code
- Recovery: Check partition state, retry reboot

**Firmware not ready**: PREPARE_REBOOT received but firmware incomplete
- Action: Send ACK with status=1
- Recovery: Wait for distribution to complete

## Related Documentation

- [OTA Foundation](ota-foundation.md) - Partition table and version management
- [OTA Download](ota-download.md) - Root node firmware download
- [OTA Distribution](ota-distribution.md) - Root node firmware distribution
- [User Guide](../../user-guides/ota-updates.md) - User-facing documentation
- [ESP-IDF OTA API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html) - ESP-IDF documentation
