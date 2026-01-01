# Over-The-Air (OTA) Firmware Updates

**Last Updated:** 2025-01-15

## Table of Contents

1. [Introduction](#introduction)
2. [Prerequisites](#prerequisites)
3. [Checking Firmware Version](#checking-firmware-version)
4. [Understanding OTA Partitions](#understanding-ota-partitions)
5. [Preparing for Updates](#preparing-for-updates)
6. [Performing Updates](#performing-updates)
7. [Troubleshooting](#troubleshooting)
8. [FAQ](#faq)

## Introduction

Over-The-Air (OTA) firmware updates allow you to update the firmware on your mesh network nodes without physically connecting to each device. This feature enables you to deploy bug fixes, new features, and improvements to all nodes in your network remotely.

> **For Developers**: See [Developer Guide](../dev-guides/ota-mupdate/ota-download.md) for technical implementation details.

### What Can OTA Updates Do?

- Download firmware from remote servers (HTTP/HTTPS)
- Update firmware on all mesh nodes remotely (mesh distribution coming soon)
- Deploy bug fixes and new features without physical access
- Maintain version consistency across all nodes
- Roll back to previous firmware version if needed (when implemented)
- Check current firmware version via web interface or serial logs
- Monitor download progress in real-time

### Current Status

The OTA system is currently implemented with the following features:
- Dual OTA partition support (app0 and app1)
- Firmware version management and tracking
- Version comparison to prevent downgrades
- **Root node firmware download** (HTTP and HTTPS)
- **Download progress monitoring** via API
- **Download cancellation** support

Mesh firmware distribution and coordinated reboot will be available in future updates.

## Prerequisites

Before using OTA updates, ensure:

1. **Mesh network is running** - All nodes must be connected to the mesh network
2. **Root node is accessible** - You need access to the root node's web interface or serial connection
3. **Sufficient flash memory** - Device must have at least 4MB flash for dual OTA partitions
4. **Stable network connection** - Root node must have stable connection to update server

## Checking Firmware Version

### Via Serial Logs

The firmware version is automatically logged during boot. Connect to the device's serial port (115200 baud) and look for:

```
[STARTUP] Firmware version: 1.0.0
```

The version format is `MAJOR.MINOR.PATCH` (e.g., `1.0.0`).

### Via Web Interface

You can check the firmware version through the web API:

```bash
curl http://<root-node-ip>/api/ota/version
```

Response:
```json
{
  "version": "1.0.0"
}
```

Replace `<root-node-ip>` with the IP address of your root node.

### Version Format

Firmware versions follow semantic versioning:
- **MAJOR**: Major version number (incremented for incompatible API changes)
- **MINOR**: Minor version number (incremented for backwards-compatible functionality)
- **PATCH**: Patch version number (incremented for backwards-compatible bug fixes)

Example: Version `1.2.3` means major version 1, minor version 2, patch version 3.

## Understanding OTA Partitions

The device uses a dual partition scheme for OTA updates:

- **app0**: Primary application partition (currently running firmware)
- **app1**: Secondary application partition (for new firmware during updates)
- **otadata**: OTA data partition (tracks which partition to boot from)

When an OTA update is performed:
1. New firmware is downloaded to the inactive partition (app1 if running from app0, or vice versa)
2. After successful download and verification, the device switches to the new partition
3. The old partition remains intact, allowing rollback if needed

This dual partition scheme ensures that:
- The device always has a working firmware image
- Updates can be verified before switching
- Rollback is possible if the new firmware has issues

## Preparing for Updates

Before performing an OTA update:

1. **Backup current configuration** - Ensure your mesh configuration is saved
2. **Verify network stability** - Ensure root node has stable connection
3. **Check available flash space** - Verify device has sufficient space for new firmware
4. **Prepare firmware binary** - Have the new firmware binary ready on your update server
5. **Test on single node first** - If possible, test update on a single node before deploying to entire network

## Downloading Firmware

The root node can download firmware from remote servers over HTTP or HTTPS. The downloaded firmware is stored in the inactive OTA partition and validated before being marked as ready.

### Prerequisites

- Root node must be connected to the internet via router
- Firmware binary must be accessible via HTTP or HTTPS URL
- Root node must have sufficient flash space for the firmware

### Initiating Download

To start a firmware download, send a POST request to the root node's web API:

```bash
curl -X POST http://<root-node-ip>/api/ota/download \
  -H "Content-Type: application/json" \
  -d '{"url":"http://example.com/firmware.bin"}'
```

For HTTPS:
```bash
curl -X POST http://<root-node-ip>/api/ota/download \
  -H "Content-Type: application/json" \
  -d '{"url":"https://example.com/firmware.bin"}'
```

**Success Response:**
```json
{
  "success": true
}
```

**Error Response:**
```json
{
  "success": false,
  "error": "Download failed: ..."
}
```

### Monitoring Download Progress

Check the download status and progress:

```bash
curl http://<root-node-ip>/api/ota/status
```

**Response:**
```json
{
  "downloading": true,
  "progress": 0.45
}
```

- `downloading`: `true` if download is in progress, `false` otherwise
- `progress`: Float between 0.0 and 1.0 (0.0 = 0%, 1.0 = 100%)

Progress is also logged to the serial console at 10% intervals.

### Cancelling Download

To cancel an ongoing download:

```bash
curl -X POST http://<root-node-ip>/api/ota/cancel
```

**Response:**
```json
{
  "success": true
}
```

### What Happens During Download

1. **URL validation** - The URL is validated (must start with `http://` or `https://`)
2. **Partition selection** - The inactive OTA partition is automatically selected (app1 if running from app0, or vice versa)
3. **Download** - Firmware is downloaded in chunks (1024 bytes) and written to the partition
4. **Progress tracking** - Progress is tracked and can be queried via API
5. **Verification** - After download completes, the firmware is validated:
   - Partition structure is verified
   - Firmware size is verified (matches expected size)
6. **Completion** - If validation passes, the firmware is ready (but device continues running from current partition until reboot)

**Note**: The device does NOT automatically switch to the new firmware. This will be handled in a future update (coordinated reboot).

## Performing Updates

The current OTA implementation supports downloading firmware to the root node. The full update process (including mesh distribution and coordinated reboot) will be available in future updates.

**Current Process:**

1. **Download firmware** - Root node downloads firmware from update server (see [Downloading Firmware](#downloading-firmware))
2. **Verify integrity** - Firmware is automatically validated after download

**Future Process (coming soon):**

1. **Download firmware** - Root node downloads firmware from update server
2. **Distribute to nodes** - Root node distributes firmware to all mesh nodes
3. **Verify integrity** - All nodes verify firmware checksum
4. **Coordinated reboot** - All nodes reboot simultaneously to new firmware

## Troubleshooting

### Version Not Displayed

**Problem**: Version information is not shown in serial logs.

**Solutions**:
- Check that NVS is properly initialized
- Verify version management module is initialized
- Check serial logs for version initialization errors

### Version Format Errors

**Problem**: Version string format is invalid.

**Solutions**:
- Ensure version follows format: `MAJOR.MINOR.PATCH` (e.g., `1.0.0`)
- Check that version numbers are non-negative integers
- Verify version string doesn't contain invalid characters

### Partition Table Issues

**Problem**: Device fails to boot after partition table changes.

**Solutions**:
- Verify partition table syntax is correct
- Check that total flash usage doesn't exceed device capacity
- Ensure app0 and app1 partitions are properly configured
- Verify otadata partition exists

### Download Fails to Start

**Problem**: Download request returns an error immediately.

**Solutions**:
- Verify root node is accessible (check IP address)
- Ensure URL is correct and accessible from root node
- Check URL format (must start with `http://` or `https://`)
- Verify OTA system is initialized (check serial logs)
- Ensure no other download is in progress

### Download Progress Stuck

**Problem**: Download progress doesn't increase or gets stuck.

**Solutions**:
- Check network connection stability
- Verify server is still accessible
- Check serial logs for error messages
- Try cancelling and restarting download
- Check if firmware size exceeds partition size

### Network Errors During Download

**Problem**: Download fails with network errors.

**Solutions**:
- Verify root node has internet connectivity
- Check DNS resolution (can root node resolve the hostname?)
- Verify firewall rules allow outbound connections
- Check server is accessible from root node's network
- Try using HTTP instead of HTTPS (or vice versa)
- Check for network timeouts (download may be too slow)

### Partition Full Errors

**Problem**: Download fails with "partition full" or "no space" error.

**Solutions**:
- Verify firmware size is smaller than partition size (typically 1.6MB per partition)
- Check partition table configuration
- Ensure sufficient flash memory (at least 4MB total)
- Try downloading a smaller firmware binary

## FAQ

### Q: What is the current firmware version?

A: The current firmware version is displayed in serial logs during boot. Look for `[STARTUP] Firmware version: X.Y.Z` in the logs.

### Q: Can I downgrade to an older firmware version?

A: The version management system prevents downgrades by default. Only same or newer versions are allowed. This prevents accidentally installing older firmware with known issues.

### Q: What happens if an OTA update fails?

A: When fully implemented, the system will include rollback mechanisms. If an update fails or the device can't connect to the mesh after updating, it will automatically roll back to the previous firmware version.

### Q: How much flash space is needed for OTA updates?

A: The device needs at least 4MB of flash memory to support dual OTA partitions. Each OTA partition (app0 and app1) requires space equal to your firmware binary size (typically 1-2MB).

### Q: Can I update individual nodes?

A: When fully implemented, OTA updates will update all nodes in the mesh network simultaneously to ensure version consistency across the network.

### Q: Where is the firmware version stored?

A: The firmware version is stored in NVS (Non-Volatile Storage) in the "mesh" namespace with the key "fw_version". This ensures the version persists across reboots.

### Q: How long does a download take?

A: Download time depends on firmware size and network speed. A typical 1MB firmware binary may take 10-30 seconds on a good connection. Progress can be monitored via the `/api/ota/status` endpoint.

### Q: Can I cancel a download?

A: Yes, you can cancel an ongoing download by sending a POST request to `/api/ota/cancel`. The download will be aborted and all resources will be cleaned up.

### Q: What happens if download fails?

A: If a download fails, the system will:
- Automatically retry up to 3 times for transient network errors
- Clean up partial downloads
- Not mark the partition as valid
- Log detailed error information

You can check the error message in the API response or serial logs.

### Q: How do I check download progress?

A: Use the `/api/ota/status` endpoint to check if a download is in progress and get the current progress (0.0 to 1.0). Progress is also logged to the serial console at 10% intervals.

### Q: Does the device automatically reboot after download?

A: No, the device does NOT automatically reboot after download. The downloaded firmware is validated and ready, but the device continues running from the current partition. Automatic reboot will be available in a future update (coordinated reboot feature).

### Q: Can I download firmware to non-root nodes?

A: No, only the root node can download firmware because it's the only node with internet access via the router. In the future, the root node will distribute downloaded firmware to all mesh nodes.

---

> **For Developers**: See [Developer Guide](../dev-guides/ota-mupdate/ota-download.md) for technical implementation details, API reference, and integration information.
