# OTA Rollback Mechanism Implementation

**Last Updated:** 2026-01-02

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Rollback Flow](#rollback-flow)
4. [NVS Storage](#nvs-storage)
5. [API Reference](#api-reference)
6. [Integration Points](#integration-points)
7. [Error Handling](#error-handling)
8. [Testing Guide](#testing-guide)

## Overview

### Purpose

The rollback mechanism provides automatic recovery from failed firmware updates by reverting to the previous working firmware version if a node cannot connect to the mesh network after updating. This safety net ensures that nodes can recover from firmware issues without manual intervention.

### Design Decisions

**NVS-based flag management**: Rollback state is stored in NVS using a boolean flag, ensuring persistence across reboots and allowing the system to detect when rollback is needed.

**Partition switching**: Rollback is implemented by switching the boot partition to the previous OTA partition (app0 ↔ app1), leveraging the dual partition design.

**Mesh connection monitoring**: After rollback, the system monitors mesh connection for 5 minutes. If mesh connects successfully, the rollback flag is cleared. If mesh fails to connect, the rollback flag remains set for the next boot cycle.

**Infinite loop prevention**: A rollback attempt counter limits the number of rollback attempts to prevent infinite loops in cases of persistent network or firmware issues.

**Timeout-based verification**: A FreeRTOS task monitors mesh connection status for 5 minutes after rollback, providing a grace period for mesh connection to establish.

> **For Users**: See [User Guide](../../user-guides/ota-updates.md) for usage instructions.

## Architecture

### Rollback Flow Diagram

```
Normal Update Flow:
1. Download firmware → app1 (if running from app0)
2. Distribute to all nodes
3. Set "ota_rollback" flag in NVS
4. Coordinated reboot
5. Boot from app1
6. Start mesh connection
7. Start rollback timeout task (5 minutes)
8. If mesh connects → Clear rollback flag after timeout
9. If mesh fails → Flag remains set, rollback on next boot

Rollback Flow (if mesh fails):
1. Boot from app1 (rollback flag set)
2. Check rollback flag → Found
3. Check rollback attempt counter (prevent infinite loops)
4. Switch to app0 (previous partition)
5. Reboot to app0
6. Boot again from app0 (flag still set)
7. Start mesh connection
8. Start rollback timeout task (5 minutes)
9. If mesh connects → Clear rollback flag after timeout
10. If mesh fails → Flag remains set, rollback counter incremented
```

### State Machine

```
[Boot]
  ↓
[Check NVS for "ota_rollback" flag]
  ↓
  ├─→ Flag NOT set → [Normal boot, start mesh]
  │
  └─→ Flag SET → [Check rollback attempt counter]
                    ↓
                    ├─→ Counter >= MAX → [Clear flag, prevent loop]
                    │
                    └─→ Counter < MAX → [Increment counter]
                                          ↓
                                        [Switch to previous partition]
                                          ↓
                                        [Reboot to previous partition]
                                          ↓
                                        [Boot again from previous partition]
                                          ↓
                                        [Start mesh connection]
                                          ↓
                                        [Start timeout task (5 minutes)]
                                          ↓
                                          ├─→ Mesh connects → [Clear flag after timeout]
                                          └─→ Mesh fails → [Flag remains set for next boot]
```

## Rollback Flow

### Normal Update Flow with Rollback Protection

1. **Firmware Update Initiated**
   - Root node downloads firmware to inactive partition
   - Firmware distributed to all leaf nodes
   - All nodes prepare for reboot

2. **Rollback Flag Set**
   - Before coordinated reboot, `mesh_ota_set_rollback_flag()` is called
   - Flag stored in NVS (namespace "mesh", key "ota_rollback")
   - Rollback attempt counter reset to 0

3. **Coordinated Reboot**
   - All nodes reboot simultaneously
   - Boot from new firmware partition (app1 if was app0)

4. **Mesh Connection Monitoring**
   - On boot, `mesh_ota_check_rollback()` checks for rollback flag
   - If flag not set: Normal boot continues
   - If flag set: Rollback is triggered (see Rollback Flow below)
   - When mesh starts/connects: Rollback timeout task starts

5. **Rollback Timeout Task**
   - Monitors mesh connection for 5 minutes
   - If mesh connects and remains connected: Clear rollback flag
   - If mesh fails to connect: Keep flag set (triggers rollback on next boot)

### Rollback Flow (When Mesh Fails)

1. **Rollback Detection**
   - On boot, `mesh_ota_check_rollback()` reads rollback flag from NVS
   - If flag is set, rollback process begins

2. **Infinite Loop Prevention**
   - Check rollback attempt counter in NVS
   - If counter >= MESH_OTA_ROLLBACK_MAX_ATTEMPTS (3): Clear flag and abort rollback
   - Otherwise: Increment counter

3. **Partition Switching**
   - Get current boot partition using `esp_ota_get_running_partition()`
   - Get other OTA partition using `esp_ota_get_next_update_partition()`
   - Verify partitions are different
   - Switch boot partition using `esp_ota_set_boot_partition()`
   - Verify partition switch succeeded

4. **Reboot to Previous Partition**
   - Reboot device using `esp_restart()`
   - Device boots from previous firmware partition

5. **Mesh Connection Monitoring (After Rollback)**
   - Boot from previous partition (rollback flag still set)
   - Mesh connection starts
   - When mesh connects: Rollback timeout task starts
   - Task monitors for 5 minutes
   - If mesh connects: Clear flag after timeout period
   - If mesh fails: Flag remains set (will rollback again on next boot if counter allows)

## NVS Storage

### Namespace and Keys

- **Namespace**: `"mesh"` (reused from version management)
- **Rollback Flag Key**: `"ota_rollback"` (uint8_t, value 1 if set, key erased if cleared)
- **Rollback Counter Key**: `"ota_rollback_count"` (uint8_t, tracks number of rollback attempts)

### Storage Pattern

The rollback flag is stored as a uint8_t value:
- Value `1`: Rollback needed
- Key not present or value `0`: Rollback not needed

The rollback counter tracks the number of rollback attempts:
- Incremented before each rollback
- Reset to 0 when rollback flag is cleared
- Maximum value: `MESH_OTA_ROLLBACK_MAX_ATTEMPTS` (3)

### NVS Operations

All NVS operations use the standard ESP-IDF NVS API:
- `nvs_open()`: Open NVS namespace with READWRITE or READONLY mode
- `nvs_set_u8()`: Store uint8_t value
- `nvs_get_u8()`: Read uint8_t value
- `nvs_erase_key()`: Delete key (used to clear flag)
- `nvs_commit()`: Commit changes to flash
- `nvs_close()`: Close NVS handle

Error handling:
- `ESP_ERR_NVS_NOT_FOUND`: Key doesn't exist (treated as flag not set)
- Other errors: Logged and handled appropriately

## API Reference

### `mesh_ota_set_rollback_flag()`

Sets the rollback flag in NVS to indicate that rollback may be needed after firmware update.

```c
esp_err_t mesh_ota_set_rollback_flag(void);
```

**Returns**: `ESP_OK` on success, error code on failure

**Behavior**:
- Opens NVS namespace "mesh"
- Sets key "ota_rollback" to value 1
- Commits changes to flash
- Logs operation

**Usage**: Called before coordinated reboot in `mesh_ota_initiate_coordinated_reboot()` for root nodes, and in REBOOT message handler for leaf nodes.

### `mesh_ota_clear_rollback_flag()`

Clears the rollback flag from NVS, indicating that rollback is no longer needed.

```c
esp_err_t mesh_ota_clear_rollback_flag(void);
```

**Returns**: `ESP_OK` on success, error code on failure

**Behavior**:
- Opens NVS namespace "mesh"
- Erases key "ota_rollback"
- Erases key "ota_rollback_count" (resets counter)
- Commits changes to flash
- Logs operation

**Usage**: Called after successful mesh connection when rollback timeout task verifies connection is stable.

### `mesh_ota_get_rollback_flag()`

Reads the rollback flag from NVS to determine if rollback is needed.

```c
esp_err_t mesh_ota_get_rollback_flag(bool *rollback_needed);
```

**Parameters**:
- `rollback_needed`: Pointer to boolean to store flag status

**Returns**: `ESP_OK` on success, error code on failure (rollback_needed will be false on error)

**Behavior**:
- Opens NVS namespace "mesh" (read-only)
- Reads key "ota_rollback"
- Returns `true` if key exists and value is 1, `false` otherwise
- Handles `ESP_ERR_NVS_NOT_FOUND` as flag not set (returns false)

**Usage**: Called in `mesh_ota_check_rollback()` and in mesh event handlers to check flag status.

### `mesh_ota_check_rollback()`

Checks for rollback flag on boot and performs rollback if needed.

```c
esp_err_t mesh_ota_check_rollback(void);
```

**Returns**: `ESP_OK` if no rollback needed or rollback completed, error code on failure

**Behavior**:
- Checks NVS for rollback flag
- If flag not set: Returns `ESP_OK` (normal boot)
- If flag set:
  - Check rollback attempt counter (prevent infinite loops)
  - If counter >= MAX: Clear flag and return error
  - Otherwise: Increment counter
  - Get current and previous partitions
  - Switch to previous partition
  - Reboot device (never returns)

**Usage**: Called early in boot sequence in `src/mesh.c`, after NVS initialization but before mesh start.

### `mesh_ota_start_rollback_timeout()`

Starts the rollback timeout monitoring task.

```c
esp_err_t mesh_ota_start_rollback_timeout(void);
```

**Returns**: `ESP_OK` on success, error code on failure

**Behavior**:
- Creates FreeRTOS task "rollback_timeout"
- Task monitors mesh connection for 5 minutes
- If mesh connects: Clears rollback flag after timeout
- If mesh fails: Keeps flag set (triggers rollback on next boot)
- Task deletes itself when done

**Usage**: Called when mesh starts/connects if rollback flag is set, to monitor connection stability.

### `mesh_ota_stop_rollback_timeout()`

Stops the rollback timeout monitoring task.

```c
esp_err_t mesh_ota_stop_rollback_timeout(void);
```

**Returns**: `ESP_OK` on success

**Behavior**:
- Deletes the rollback timeout task if running
- Cleans up task handle

**Usage**: Called when rollback monitoring is no longer needed (typically not needed, task deletes itself).

## Integration Points

### Boot Sequence Integration

**Location**: `src/mesh.c` - `app_main()` function

**Timing**: After `mesh_version_init()`, before `mesh_common_init()` mesh start

**Code**:
```c
/* Check for rollback before starting mesh (after NVS and version init) */
esp_err_t rollback_err = mesh_ota_check_rollback();
if (rollback_err != ESP_OK && rollback_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW("mesh_main", "[STARTUP] Rollback check failed: %s", esp_err_to_name(rollback_err));
    /* Continue boot even if rollback check fails (shouldn't happen, but be safe) */
}
/* Note: If rollback is needed, mesh_ota_check_rollback() will reboot and never return */
```

### OTA Reboot Integration

**Location**: `src/mesh_ota.c` - `mesh_ota_initiate_coordinated_reboot()` function

**Timing**: Before sending REBOOT command to leaf nodes

**Code**:
```c
/* Set rollback flag before coordinated reboot */
esp_err_t rollback_flag_err = mesh_ota_set_rollback_flag();
if (rollback_flag_err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set rollback flag before reboot: %s", esp_err_to_name(rollback_flag_err));
    /* Continue with reboot even if flag setting fails (non-critical) */
} else {
    ESP_LOGI(TAG, "Rollback flag set before coordinated reboot");
}
```

**Leaf Node Integration**: Also set in leaf node REBOOT message handler (`src/mesh_ota.c`), before setting boot partition and rebooting.

### Mesh Connection Success Integration

**Root Node**: `src/mesh_common.c` - `MESH_EVENT_STARTED` handler

**Leaf Node**: `src/mesh_common.c` - `MESH_EVENT_PARENT_CONNECTED` handler

**Code**:
```c
/* Start rollback timeout monitoring if rollback flag is set */
bool rollback_needed = false;
esp_err_t rollback_check_err = mesh_ota_get_rollback_flag(&rollback_needed);
if (rollback_check_err == ESP_OK && rollback_needed) {
    /* Rollback flag is set - start timeout task to monitor mesh connection */
    esp_err_t timeout_err = mesh_ota_start_rollback_timeout();
    if (timeout_err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "Failed to start rollback timeout task: %s", esp_err_to_name(timeout_err));
    } else {
        ESP_LOGI(MESH_TAG, "Rollback timeout monitoring started");
    }
}
```

## Error Handling

### Partition Access Errors

**Scenario**: Partition operations fail (get partition, set partition)

**Handling**:
- Check for NULL partitions before use
- Verify partition operations return `ESP_OK`
- Log errors clearly
- Clear rollback flag on critical errors to prevent loops
- Return appropriate error codes

**Code Example**:
```c
const esp_partition_t *current_boot = esp_ota_get_running_partition();
if (current_boot == NULL) {
    ESP_LOGE(TAG, "Failed to get current boot partition, cannot rollback");
    mesh_ota_clear_rollback_flag();
    return ESP_ERR_NOT_FOUND;
}
```

### NVS Errors

**Scenario**: NVS operations fail (open, read, write, commit)

**Handling**:
- Handle `ESP_ERR_NVS_NOT_FOUND` as flag not set (normal case)
- Log other NVS errors
- Return appropriate error codes
- On critical errors, clear rollback flag to prevent loops

**Code Example**:
```c
err = nvs_get_u8(nvs_handle, MESH_OTA_ROLLBACK_KEY, &flag_value);
if (err == ESP_ERR_NVS_NOT_FOUND) {
    /* Flag not found, rollback not needed */
    *rollback_needed = false;
    return ESP_OK;
} else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read rollback flag: %s", esp_err_to_name(err));
    *rollback_needed = false;
    return err;
}
```

### Infinite Loop Prevention

**Scenario**: Rollback attempts exceed maximum limit

**Handling**:
- Check rollback attempt counter before each rollback
- If counter >= MESH_OTA_ROLLBACK_MAX_ATTEMPTS (3): Clear flag and abort
- Increment counter before each rollback
- Reset counter when flag is cleared

**Code Example**:
```c
if (rollback_count >= MESH_OTA_ROLLBACK_MAX_ATTEMPTS) {
    ESP_LOGE(TAG, "Rollback attempt limit (%d) exceeded, clearing rollback flag to prevent infinite loop",
             MESH_OTA_ROLLBACK_MAX_ATTEMPTS);
    /* Clear rollback flag and counter to prevent infinite loops */
    nvs_erase_key(nvs_handle, MESH_OTA_ROLLBACK_KEY);
    nvs_erase_key(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY);
    nvs_commit(nvs_handle);
    return ESP_ERR_INVALID_STATE;
}
```

### Edge Cases

**Both partitions identical**: Check if current and rollback partitions are the same. If so, clear flag and return (no rollback needed).

**Rollback flag set but no OTA occurred**: If rollback flag is set but partitions are identical, clear flag (handled by partition check).

**Mesh never connects**: Rollback flag remains set. Rollback will be attempted again on next boot (up to maximum attempts).

**Rollback during normal operation**: Rollback only happens on boot when flag is set. Flag is only set before OTA reboot, so this shouldn't happen in normal operation.

## Testing Guide

### Test Scenarios

1. **Normal Update Flow (Successful)**
   - Perform OTA update
   - Verify rollback flag is set before reboot
   - Verify mesh connects after reboot
   - Verify rollback flag is cleared after timeout period
   - Verify rollback attempt counter is reset

2. **Rollback Scenario (Mesh Fails)**
   - Perform OTA update
   - Disconnect node from mesh network after reboot
   - Verify rollback flag triggers rollback on next boot
   - Verify device boots from previous partition
   - Verify rollback attempt counter is incremented
   - Reconnect to mesh network
   - Verify rollback flag is cleared after timeout

3. **Infinite Loop Prevention**
   - Simulate multiple rollback attempts (set flag, disconnect mesh)
   - Verify rollback happens up to maximum attempts (3)
   - Verify rollback flag is cleared after maximum attempts
   - Verify system doesn't enter infinite rollback loop

4. **Edge Cases**
   - Test with both partitions identical (should clear flag)
   - Test with rollback flag set but no valid partitions (should handle gracefully)
   - Test with NVS errors (should handle gracefully)
   - Test with partition access errors (should handle gracefully)

### Test Procedures

**Test 1: Successful Update**
1. Flash firmware version 1.0.0 to all nodes
2. Verify mesh network is operational
3. Perform OTA update to version 1.1.0
4. Monitor serial logs for rollback flag being set
5. After reboot, verify mesh connects
6. Wait 5+ minutes
7. Check serial logs for rollback flag being cleared
8. Verify all nodes are running version 1.1.0

**Test 2: Rollback on Mesh Failure**
1. Flash firmware version 1.0.0 to all nodes
2. Verify mesh network is operational
3. Perform OTA update to version 1.1.0
4. Immediately after reboot, disconnect node from mesh (power off mesh root or block WiFi)
5. Wait for node to reboot (rollback should trigger)
6. Verify node boots from version 1.0.0 (previous partition)
7. Reconnect to mesh network
8. Verify mesh connects
9. Wait 5+ minutes
10. Check serial logs for rollback flag being cleared
11. Verify node remains on version 1.0.0

**Test 3: Infinite Loop Prevention**
1. Flash firmware version 1.0.0 to all nodes
2. Manually set rollback flag in NVS
3. Disconnect node from mesh
4. Boot node and verify rollback happens (attempt 1)
5. Keep mesh disconnected and boot again (attempt 2)
6. Keep mesh disconnected and boot again (attempt 3)
7. Boot again (attempt 4) - should clear flag and abort rollback
8. Verify flag is cleared and rollback doesn't happen
9. Check serial logs for "Rollback attempt limit exceeded" message

### Expected Log Messages

**Normal Boot (No Rollback)**:
```
[mesh_main] [STARTUP] Firmware version: 1.0.0
```

**Rollback Triggered**:
```
[mesh_ota] Rollback flag detected, attempting rollback (attempt 1/3)
[mesh_ota] Rolling back from partition at 0x10000 to partition at 0x1A0000
[mesh_ota] Rollback partition set successfully, rebooting...
```

**Rollback Flag Set**:
```
[mesh_ota] Rollback flag set before coordinated reboot
```

**Rollback Flag Cleared**:
```
[mesh_ota] Rollback flag cleared from NVS
```

**Infinite Loop Prevention**:
```
[mesh_ota] Rollback attempt limit (3) exceeded, clearing rollback flag to prevent infinite loop
```
