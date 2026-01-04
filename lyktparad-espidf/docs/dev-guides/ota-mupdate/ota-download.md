# Root Node OTA Download Implementation

**Last Updated:** 2026-01-01

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [API Reference](#api-reference)
4. [Download Implementation](#download-implementation)
5. [HTTP vs HTTPS](#http-vs-https)
6. [Error Handling](#error-handling)
7. [Integration Points](#integration-points)
8. [Testing](#testing)

## Overview

### Purpose

The Root Node OTA Download module provides functionality for downloading firmware from remote servers over HTTP or HTTPS. The downloaded firmware is stored in the inactive OTA partition (app1 if running from app0, or vice versa) and validated before being marked as ready for use.

This module is the second step in the OTA implementation, building on the foundation of partition table and version management. It focuses on single-node OTA (root node only), as the root node is the only node with internet access via the router.

> **For Users**: See [User Guide](../../user-guides/ota-updates.md) for usage instructions.

### Design Decisions

**Root-node only**: Only the root node has internet access via the router, so OTA downloads are restricted to the root node. The module checks if the node is root before allowing downloads.

**Dual protocol support**: Both HTTP and HTTPS are supported. The protocol is automatically detected from the URL.

**Progress tracking**: Download progress is tracked and can be queried via the API. Progress is logged at 10% intervals.

**Retry logic**: Transient network failures are automatically retried up to 3 times with a 1-second delay between retries.

**No automatic partition switch**: The downloaded firmware is validated but the device continues running from the current partition. Partition switching will be handled in a future phase (coordinated reboot).

**Downgrade prevention**: After download completion, the firmware version is compared with the current version. Downgrades (older versions) are rejected to prevent security issues and feature loss. See [OTA Downgrade Prevention](ota-downgrade-prevention.md) for details.

## Architecture

### Data Flow

```
User/API Request (URL)
    ↓
mesh_ota_download_firmware()
    ↓
URL Protocol Detection (HTTP vs HTTPS)
    ↓
    ├─→ HTTPS: esp_https_ota API
    │       ↓
    │   Download chunks → esp_ota_write()
    │       ↓
    │   esp_ota_end() → Validation
    │
    └─→ HTTP: esp_http_client + esp_ota_ops
            ↓
        Download chunks → esp_ota_write()
            ↓
        esp_ota_end() → Validation
    ↓
Success/Failure Response
```

### Partition Selection

The module automatically selects the inactive OTA partition:
- If running from `app0`, downloads go to `app1`
- If running from `app1`, downloads go to `app0`

This ensures the current running firmware is never overwritten during download.

## API Reference

### `esp_err_t mesh_ota_init(void)`

Initialize the OTA system. This function must be called once during system initialization, after NVS is initialized.

**Returns:**
- `ESP_OK` on success
- `ESP_ERR_NOT_FOUND` if OTA partitions are not found
- Other error codes on failure

**Example:**
```c
esp_err_t err = mesh_ota_init();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA init failed: %s", esp_err_to_name(err));
}
```

### `esp_err_t mesh_ota_download_firmware(const char *url)`

Download firmware from the specified URL. The URL must start with `http://` or `https://`.

**Parameters:**
- `url`: URL of the firmware binary

**Returns:**
- `ESP_OK` on success
- `ESP_ERR_INVALID_ARG` if URL is NULL or invalid format
- `ESP_ERR_INVALID_STATE` if OTA not initialized or download already in progress
- Network/HTTP errors on download failure

**Example:**
```c
esp_err_t err = mesh_ota_download_firmware("http://example.com/firmware.bin");
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Download started successfully");
}
```

### `const esp_partition_t* mesh_ota_get_update_partition(void)`

Get a pointer to the inactive OTA partition where new firmware will be written.

**Returns:**
- Pointer to update partition, or `NULL` if not initialized

**Example:**
```c
const esp_partition_t *update_part = mesh_ota_get_update_partition();
if (update_part != NULL) {
    ESP_LOGI(TAG, "Update partition: %s at 0x%x", update_part->label, update_part->address);
}
```

### `bool mesh_ota_is_downloading(void)`

Check if a download is currently in progress.

**Returns:**
- `true` if download is in progress, `false` otherwise

**Example:**
```c
if (mesh_ota_is_downloading()) {
    ESP_LOGI(TAG, "Download in progress");
}
```

### `float mesh_ota_get_download_progress(void)`

Get the current download progress as a float between 0.0 and 1.0.

**Returns:**
- Progress value (0.0-1.0), or 0.0 if not downloading

**Example:**
```c
float progress = mesh_ota_get_download_progress();
ESP_LOGI(TAG, "Download progress: %.1f%%", progress * 100.0f);
```

### `esp_err_t mesh_ota_cancel_download(void)`

Cancel the current download operation if one is in progress. This function is idempotent.

**Returns:**
- `ESP_OK` on success

**Example:**
```c
esp_err_t err = mesh_ota_cancel_download();
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Download cancelled");
}
```

## Download Implementation

### HTTP Download Flow

1. **Initialize HTTP client** with URL and timeout
2. **Open connection** and check HTTP status code (must be 200)
3. **Get content-length** from response headers
4. **Begin OTA write** to inactive partition
5. **Read data in chunks** (1024 bytes) from HTTP response
6. **Write chunks** to OTA partition using `esp_ota_write()`
7. **Track progress** based on bytes read vs content-length
8. **Finalize OTA write** using `esp_ota_end()` (validates partition)
9. **Verify size** matches content-length
10. **Clean up** HTTP client and OTA handle

### HTTPS Download Flow

1. **Configure HTTPS OTA** with URL and certificate settings
2. **Begin HTTPS OTA** using `esp_https_ota_begin()`
3. **Begin OTA write** to inactive partition
4. **Perform HTTPS OTA** in loop using `esp_https_ota_perform()`
   - Continues until `ESP_OK` or error (not `ESP_ERR_HTTPS_OTA_IN_PROGRESS`)
5. **Track progress** using `esp_https_ota_get_image_len_read()` and `esp_https_ota_get_image_len()`
6. **Finalize OTA write** using `esp_ota_end()` (validates partition)
7. **Verify size** matches expected image length
8. **Finish HTTPS OTA** and clean up

### Progress Tracking

Progress is calculated as:
```
progress = bytes_read / total_bytes
```

Progress is logged at 10% intervals during download. If total size is unknown (no content-length header), progress is logged every 10 chunks (10KB).

### Firmware Verification

After download completes, the firmware is verified:

1. **Partition validation**: `esp_ota_end()` automatically validates the partition structure
2. **Size verification**: Actual bytes written are compared to expected size (from Content-Length header or HTTPS OTA image length)
3. **Error handling**: If validation fails, the partition is not marked as valid and an error is returned

## HTTP vs HTTPS

### HTTP Implementation

- Uses `esp_http_client` component
- Manual chunk reading and writing
- Requires Content-Length header for accurate progress tracking
- Simpler implementation, no certificate handling

### HTTPS Implementation

- Uses `esp_https_ota` component
- Automatic chunk handling by the component
- Built-in certificate validation (uses default CA bundle)
- More secure, recommended for production use

### Protocol Detection

The protocol is automatically detected from the URL:
- URLs starting with `https://` (case-insensitive) use HTTPS
- URLs starting with `http://` use HTTP
- Invalid URLs are rejected with `ESP_ERR_INVALID_ARG`

## Error Handling

### Network Errors

- **Connection timeouts**: Detected and logged, retried up to 3 times
- **DNS resolution failures**: Detected and logged, not retried
- **SSL/TLS errors**: Detected and logged, not retried

### HTTP Errors

- **404 Not Found**: Logged, not retried (client error)
- **500 Internal Server Error**: Logged, may be retried (server error)
- **Other 4xx errors**: Logged, not retried (client errors)
- **Other 5xx errors**: Logged, may be retried (server errors)

### OTA Partition Errors

- **Partition write errors**: Detected immediately, download aborted
- **Partition validation failures**: Detected during `esp_ota_end()`, partition not marked as valid
- **Out-of-space errors**: Detected during write, download aborted

### Retry Logic

Transient errors are automatically retried:
- **Retryable errors**: Network timeouts, connection failures, HTTP 5xx errors
- **Non-retryable errors**: HTTP 4xx errors, invalid firmware, partition errors
- **Max retries**: 3 attempts with 1-second delay between retries
- **Retry counter**: Reset on success

### Cleanup on Failure

On any error, the module:
1. Aborts OTA operation (if handle is valid)
2. Closes HTTP/HTTPS connections
3. Releases OTA handle
4. Resets download state variables
5. Does NOT mark partition as valid

## Integration Points

### Root Node Initialization

OTA is initialized in `mesh_root_init()` after version management is initialized:

```c
esp_err_t ota_err = mesh_ota_init();
if (ota_err != ESP_OK) {
    ESP_LOGW(MESH_TAG, "[STARTUP] OTA initialization failed: %s", esp_err_to_name(ota_err));
} else {
    ESP_LOGI(MESH_TAG, "[STARTUP] OTA system initialized");
}
```

The initialization is safe to call on all nodes, but only the root node will use OTA functionality.

### Web API Endpoints

The module provides the following HTTP API endpoints:

#### `POST /api/ota/download`

Initiates firmware download.

**Request:**
```json
{
  "url": "http://example.com/firmware.bin"
}
```

**Response (success):**
```json
{
  "success": true
}
```

**Response (error):**
```json
{
  "success": false,
  "error": "Download failed: ..."
}
```

#### `GET /api/ota/status`

Returns current download status.

**Response:**
```json
{
  "downloading": true,
  "progress": 0.45
}
```

#### `POST /api/ota/cancel`

Cancels ongoing download.

**Response:**
```json
{
  "success": true
}
```

#### `GET /api/ota/version`

Returns current firmware version.

**Response:**
```json
{
  "version": "1.0.0"
}
```

### Build System Integration

The module requires the following ESP-IDF components:
- `app_update`: For OTA partition operations (provides `esp_ota_ops.h`)
- `esp_https_ota`: For HTTPS downloads
- `esp_http_client`: For HTTP downloads

These are added to `CMakeLists.txt`:
```cmake
REQUIRES led_strip esp_wifi esp_http_server esp_https_ota esp_http_client app_update
```

## Testing

### Unit Testing

Test the following scenarios:

1. **OTA initialization**:
   - Verify partitions are detected correctly
   - Test error handling if partitions are missing
   - Verify initialization is idempotent

2. **URL parsing**:
   - Test HTTP URL detection
   - Test HTTPS URL detection
   - Test invalid URL handling

3. **Download state management**:
   - Test concurrent download prevention
   - Test download cancellation
   - Test progress tracking

### Integration Testing

Test the following scenarios:

1. **HTTP download**:
   - Successful download with various firmware sizes
   - Network failure during download
   - HTTP error responses (404, 500)
   - Invalid firmware binary

2. **HTTPS download** (if supported):
   - Successful download with valid certificate
   - Certificate validation errors

3. **Error scenarios**:
   - Network timeouts
   - DNS resolution failures
   - Partition full errors
   - Retry logic for transient failures

4. **Web API**:
   - Download initiation via API
   - Progress monitoring via API
   - Download cancellation via API
   - Version query via API

### Test Setup

1. Set up local HTTP/HTTPS server with test firmware binary
2. Ensure root node has network access
3. Monitor serial logs for progress and errors
4. Verify firmware is written to correct partition
5. Verify partition validation passes after download

## Related Documentation

- [OTA Foundation](ota-foundation.md) - Partition table and version management
- [User Guide](../../user-guides/ota-updates.md) - User-facing documentation
- [ESP-IDF OTA API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html) - ESP-IDF documentation
- [ESP-IDF HTTPS OTA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html) - HTTPS OTA documentation
