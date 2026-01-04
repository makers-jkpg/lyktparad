# OTA Downgrade Prevention Implementation

**Last Updated:** 2026-01-02

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Check Points](#check-points)
4. [API Reference](#api-reference)
5. [Integration Points](#integration-points)
6. [Error Handling](#error-handling)
7. [Testing Guide](#testing-guide)

## Overview

### Purpose

The downgrade prevention mechanism ensures that firmware with older version numbers cannot be installed through the OTA update process. This prevents accidental downgrades that could introduce security vulnerabilities or remove features, maintaining system integrity and consistency across the mesh network.

### Design Decisions

**Version source**: The current firmware version is obtained from compile-time defines via `mesh_version_get_string()`, ensuring the running firmware version is always used as the reference point.

**Numerical comparison**: Version strings are compared numerically using `mesh_version_compare()`, ensuring correct ordering (e.g., "1.0.9" < "1.1.0").

**Fail-safe approach**: If version extraction or comparison fails, the system rejects the update to prevent potential downgrades from being allowed due to errors.

**Multiple checkpoints**: Downgrade checks are performed at three critical points: after download completion, before distribution start, and before leaf node reboot. This provides defense in depth.

**Same version allowed**: Re-installation of the same firmware version is allowed, which is useful for recovery scenarios.

> **For Users**: See [User Guide](../../user-guides/ota-updates.md) for usage instructions.

## Architecture

### Check Points Flow

```
Download Firmware (HTTP or HTTPS)
    ↓
esp_ota_end() / esp_https_ota_finish() succeeds
    ↓
[CHECKPOINT 1: mesh_ota_check_downgrade()]
    ├─→ Downgrade detected → Abort download, cleanup, return error
    └─→ OK (same/newer) → Continue
        ↓
Distribution Start
    ↓
[CHECKPOINT 2: mesh_ota_check_downgrade()]
    ├─→ Downgrade detected → Return error (distribution not started)
    └─→ OK (same/newer) → Start distribution
        ↓
Leaf Node Receives REBOOT Command
    ↓
[CHECKPOINT 3: mesh_ota_check_downgrade()]
    ├─→ Downgrade detected → Send error ACK, reject reboot
    └─→ OK (same/newer) → Set boot partition, reboot
```

### Version Extraction

Version information is extracted from the firmware partition's app description structure:

1. Use `esp_ota_get_partition_description()` to read `esp_app_desc_t` structure
2. Extract version string from `app_desc.version` field
3. Compare with current version using `mesh_version_compare()`

The version format must match "MAJOR.MINOR.PATCH" format expected by the version management module.

## Check Points

### Checkpoint 1: After Download Completion

**Location**: `src/mesh_ota.c` - `download_firmware_http()` and `download_firmware_https()`

**Timing**: After `esp_ota_end()` (HTTP) or `esp_https_ota_finish()` (HTTPS) succeeds

**Action on Downgrade**:
- HTTP: Abort OTA handle, reset state, return `ESP_ERR_INVALID_VERSION`
- HTTPS: Return `ESP_ERR_INVALID_VERSION` (partition already finalized, cannot abort)

**Rationale**: Prevents downgraded firmware from being marked as ready for distribution. Early detection saves bandwidth and prevents distribution of invalid firmware.

### Checkpoint 2: Before Distribution Start

**Location**: `src/mesh_ota.c` - `mesh_ota_distribute_firmware()`

**Timing**: After partition validation, before starting distribution task

**Action on Downgrade**: Return `ESP_ERR_INVALID_VERSION` immediately, do not start distribution

**Rationale**: Double-check before distributing to all nodes. Prevents wasting mesh network bandwidth and node resources on invalid firmware.

### Checkpoint 3: Before Leaf Node Reboot

**Location**: `src/mesh_ota.c` - REBOOT message handler in `mesh_ota_handle_mesh_message()`

**Timing**: After firmware ready check, before `esp_ota_set_boot_partition()`

**Action on Downgrade**: Send error ACK (status=1) to root node, return error, do not reboot

**Rationale**: Final safety check at the leaf node level. Prevents nodes from rebooting to older firmware even if previous checks were bypassed or if firmware was modified.

## API Reference

### `mesh_ota_check_downgrade()`

Check if firmware in partition is a downgrade compared to current firmware.

```c
esp_err_t mesh_ota_check_downgrade(const esp_partition_t *partition);
```

**Parameters:**
- `partition`: Pointer to the firmware partition to check

**Returns:**
- `ESP_OK`: Version is same or newer (upgrade/same version allowed)
- `ESP_ERR_INVALID_ARG`: Partition is NULL, version extraction failed, or version comparison failed
- `ESP_ERR_INVALID_VERSION`: Downgrade detected (partition version < current version)

**Behavior:**
1. Validates partition pointer (returns error if NULL)
2. Reads partition description using `esp_ota_get_partition_description()`
3. Extracts version string from `app_desc.version`
4. Gets current firmware version using `mesh_version_get_string()`
5. Compares versions using `mesh_version_compare()`
6. Returns `ESP_ERR_INVALID_VERSION` if comparison result < 0 (downgrade)
7. Returns `ESP_OK` if comparison result >= 0 (same or newer)

**Error Handling:**
- All errors are logged with clear messages
- Fail-safe approach: rejects update on any error
- Logs both current and attempted versions on downgrade detection

**Example:**
```c
const esp_partition_t *update_part = mesh_ota_get_update_partition();
esp_err_t err = mesh_ota_check_downgrade(update_part);
if (err == ESP_ERR_INVALID_VERSION) {
    ESP_LOGE(TAG, "Downgrade detected, rejecting update");
    return err;
} else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Version check failed: %s", esp_err_to_name(err));
    return err;
}
/* Version check passed - proceed with update */
```

## Integration Points

### Download Functions

**HTTP Download**: `src/mesh_ota.c` - `download_firmware_http()`
- Check after `esp_ota_end()` succeeds (line ~458)
- On downgrade: abort OTA handle, reset state, return error

**HTTPS Download**: `src/mesh_ota.c` - `download_firmware_https()`
- Check after `esp_https_ota_finish()` (line ~278)
- On downgrade: return error (partition already finalized)

### Distribution Function

**Location**: `src/mesh_ota.c` - `mesh_ota_distribute_firmware()`
- Check after partition validation (line ~1064)
- On downgrade: return error immediately, do not start distribution

### Leaf Node Reboot Handler

**Location**: `src/mesh_ota.c` - REBOOT message handler
- Check after firmware ready check (line ~1762)
- On downgrade: send error ACK to root, return error, do not reboot

### Web API Error Handling

**Download Endpoint**: `src/mesh_web.c` - `api_ota_download_post_handler()`
- Checks for `ESP_ERR_INVALID_VERSION` return code
- Returns HTTP 409 Conflict with user-friendly error message

**Distribution Endpoint**: `src/mesh_web.c` - `api_ota_distribute_post_handler()`
- Checks for `ESP_ERR_INVALID_VERSION` return code
- Returns HTTP 409 Conflict with user-friendly error message

## Error Handling

### Partition Description Read Errors

**Scenario**: `esp_ota_get_partition_description()` fails

**Handling**:
- Log error with function name
- Return `ESP_ERR_INVALID_ARG`
- Fail-safe: reject update (cannot verify version, assume unsafe)

**Example:**
```c
esp_app_desc_t app_desc;
esp_err_t err = esp_ota_get_partition_description(partition, &app_desc);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get partition description for downgrade check: %s", esp_err_to_name(err));
    return ESP_ERR_INVALID_ARG;  /* Fail-safe: reject */
}
```

### Version Comparison Errors

**Scenario**: `mesh_version_compare()` fails (invalid version format)

**Handling**:
- Log error with both version strings
- Return `ESP_ERR_INVALID_ARG`
- Fail-safe: reject update (cannot compare, assume unsafe)

**Example:**
```c
err = mesh_version_compare(app_desc.version, current_version, &comparison_result);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to compare versions (partition: %s, current: %s): %s",
             app_desc.version, current_version, esp_err_to_name(err));
    return ESP_ERR_INVALID_ARG;  /* Fail-safe: reject */
}
```

### Current Version Retrieval Errors

**Scenario**: `mesh_version_get_string()` returns NULL

**Handling**:
- Log error
- Return `ESP_ERR_INVALID_ARG`
- Fail-safe: reject update (cannot determine current version, assume unsafe)

**Note**: This should never happen in practice, as `mesh_version_get_string()` returns a compile-time constant string.

### Edge Cases

**Same Version**: Allowed (comparison result == 0)
- Useful for re-installation and recovery scenarios
- Logged as informational message

**Newer Version**: Allowed (comparison result > 0)
- Normal upgrade scenario
- Logged as informational message with version numbers

**NULL Partition**: Rejected immediately
- Returns `ESP_ERR_INVALID_ARG` before attempting version extraction
- Should not happen in normal flow, but handled gracefully

**Invalid Version Format**: Rejected
- Detected during `mesh_version_compare()` call
- Returns `ESP_ERR_INVALID_ARG`
- Logged with error details

## Testing Guide

### Test Scenarios

1. **Normal Upgrade (Newer Version)**
   - Download firmware with version newer than current (e.g., 1.0.1 vs 1.0.0)
   - Verify download completes successfully
   - Verify distribution proceeds normally
   - Verify leaf nodes reboot successfully
   - Check logs for "Version check: Upgrade" message

2. **Same Version (Re-installation)**
   - Download firmware with same version as current (e.g., 1.0.0 vs 1.0.0)
   - Verify download completes successfully
   - Verify distribution proceeds normally
   - Verify leaf nodes reboot successfully
   - Check logs for "Version check: Same version" message

3. **Downgrade Prevention at Download (HTTP)**
   - Download firmware with older version than current (e.g., 0.9.0 vs 1.0.0)
   - Verify download fails with `ESP_ERR_INVALID_VERSION`
   - Verify OTA handle is aborted
   - Verify partition is not marked as valid
   - Check logs for "Downgrade prevented" message with versions
   - Verify web API returns 409 Conflict

4. **Downgrade Prevention at Download (HTTPS)**
   - Download firmware with older version via HTTPS
   - Verify download fails with `ESP_ERR_INVALID_VERSION`
   - Verify web API returns 409 Conflict

5. **Downgrade Prevention at Distribution**
   - Manually trigger distribution with downgraded firmware in partition
   - Verify distribution fails with `ESP_ERR_INVALID_VERSION`
   - Verify distribution task is not started
   - Verify web API returns 409 Conflict

6. **Downgrade Prevention at Leaf Node Reboot**
   - Send REBOOT command to leaf node with downgraded firmware
   - Verify leaf node sends error ACK (status=1)
   - Verify leaf node does not reboot
   - Verify error is logged

7. **Version Extraction Failure**
   - Test with corrupted partition (if possible)
   - Verify update is rejected (fail-safe)
   - Verify appropriate error is logged

8. **Version Comparison Failure**
   - Test with invalid version format in partition description (if possible)
   - Verify update is rejected (fail-safe)
   - Verify appropriate error is logged

### Test Procedures

**Test 1: Upgrade Flow**
1. Flash firmware version 1.0.0 to all nodes
2. Download firmware version 1.0.1
3. Verify download completes
4. Verify distribution completes
5. Verify all nodes reboot
6. Verify all nodes are running version 1.0.1

**Test 2: Same Version Re-installation**
1. Flash firmware version 1.0.0 to all nodes
2. Download firmware version 1.0.0 (same version)
3. Verify download completes (same version allowed)
4. Verify distribution completes
5. Verify all nodes reboot
6. Verify all nodes remain on version 1.0.0

**Test 3: Downgrade Prevention**
1. Flash firmware version 1.0.1 to all nodes
2. Attempt to download firmware version 1.0.0
3. Verify download fails with downgrade error
4. Check web API returns 409 Conflict
5. Verify partition is not marked as valid
6. Check logs for "Downgrade prevented" message

### Expected Log Messages

**Successful Version Check (Upgrade)**:
```
[mesh_ota] Version check: Upgrade from 1.0.0 to 1.0.1
```

**Successful Version Check (Same)**:
```
[mesh_ota] Version check: Same version 1.0.0 (re-installation allowed)
```

**Downgrade Detected**:
```
[mesh_ota] Downgrade prevented: Current version 1.0.1, attempted version 1.0.0
```

**Version Check Failure**:
```
[mesh_ota] Failed to get partition description for downgrade check: <error>
```
or
```
[mesh_ota] Failed to compare versions (partition: <version>, current: <version>): <error>
```
