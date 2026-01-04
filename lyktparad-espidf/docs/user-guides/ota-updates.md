# Over-The-Air (OTA) Firmware Updates

**Last Updated:** 2026-01-01

## Table of Contents

1. [Introduction](#introduction)
2. [Prerequisites](#prerequisites)
3. [Checking Firmware Version](#checking-firmware-version)
4. [Understanding OTA Partitions](#understanding-ota-partitions)
5. [Preparing for Updates](#preparing-for-updates)
6. [Performing Updates](#performing-updates)
7. [Automatic Rollback Mechanism](#automatic-rollback-mechanism)
8. [Troubleshooting](#troubleshooting)
9. [FAQ](#faq)

## Introduction

Over-The-Air (OTA) firmware updates allow you to update the firmware on your mesh network nodes without physically connecting to each device. This feature enables you to deploy bug fixes, new features, and improvements to all nodes in your network remotely.

> **For Developers**: See [Developer Guide](../dev-guides/ota-mupdate/ota-download.md) for technical implementation details.

### What Can OTA Updates Do?

- Download firmware from remote servers (HTTP/HTTPS)
- Distribute firmware to all mesh nodes automatically
- Deploy bug fixes and new features without physical access
- Maintain version consistency across all nodes
- **Automatic rollback** to previous firmware version if mesh connection fails after update
- **Automatic downgrade prevention** to prevent installing older firmware versions
- Check current firmware version via web interface or serial logs
- Monitor download and distribution progress in real-time

### Current Status

The OTA system is currently implemented with the following features:
- Dual OTA partition support (app0 and app1)
- Firmware version management and tracking
- Version comparison to prevent downgrades
- **Root node firmware download** (HTTP and HTTPS)
- **Download progress monitoring** via API
- **Download cancellation** support
- **Mesh firmware distribution** - Root node distributes firmware to all leaf nodes
- **Distribution progress tracking** - Monitor distribution to all nodes via API
- **Coordinated reboot** - Simultaneous reboot of all mesh nodes after firmware update
- **Automatic rollback** - Automatic reversion to previous firmware if mesh connection fails after update
- **Downgrade prevention** - Automatic rejection of firmware versions older than current version

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

**Note**: The device does NOT automatically switch to the new firmware after download. You must initiate a coordinated reboot after distribution completes (see [Coordinated Reboot](#coordinated-reboot)).

## Distributing Firmware to Mesh Nodes

After downloading firmware to the root node, you can distribute it to all leaf nodes in the mesh network. The root node will automatically fragment the firmware into blocks and send them to all connected nodes, tracking acknowledgments and retrying failed blocks.

### Prerequisites

- Root node must have successfully downloaded firmware (see [Downloading Firmware](#downloading-firmware))
- Mesh network must be active with leaf nodes connected
- All nodes must have sufficient flash space for the firmware

### Initiating Distribution

To start firmware distribution, send a POST request to the root node's web API:

```bash
curl -X POST http://<root-node-ip>/api/ota/distribute
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
  "error": "Distribution failed: ..."
}
```

Distribution can also be triggered automatically when a leaf node sends an OTA_REQUEST message to the root node.

### Monitoring Distribution Progress

Check the distribution status and progress:

```bash
curl http://<root-node-ip>/api/ota/distribution/status
```

**Response:**
```json
{
  "distributing": true,
  "total_blocks": 150,
  "current_block": 75,
  "overall_progress": 0.50,
  "nodes_total": 5,
  "nodes_complete": 2,
  "nodes_failed": 0
}
```

- `distributing`: `true` if distribution is in progress, `false` otherwise
- `total_blocks`: Total number of firmware blocks
- `current_block`: Current block being distributed (approximate)
- `overall_progress`: Float between 0.0 and 1.0 (0.0 = 0%, 1.0 = 100%)
- `nodes_total`: Total number of target nodes
- `nodes_complete`: Number of nodes that have received all blocks
- `nodes_failed`: Number of nodes that failed to receive all blocks

You can also get just the progress value:

```bash
curl http://<root-node-ip>/api/ota/distribution/progress
```

**Response:**
```json
{
  "progress": 0.50
}
```

Progress is also logged to the serial console at 10% intervals.

### Cancelling Distribution

To cancel an ongoing distribution:

```bash
curl -X POST http://<root-node-ip>/api/ota/distribution/cancel
```

**Response:**
```json
{
  "success": true
}
```

### What Happens During Distribution

1. **Verification** - Root node verifies firmware is available in inactive partition
2. **Node discovery** - Root node gets list of all leaf nodes from routing table
3. **Fragmentation** - Firmware is fragmented into 1KB blocks
4. **Distribution start** - Root sends OTA_START command to all nodes with metadata (total blocks, firmware size, version)
5. **Block sending** - For each block:
   - Root sends block to all nodes that need it
   - Nodes receive block, verify checksum, and send ACK
   - Root waits for ACKs with 5-second timeout
   - Failed blocks are retried up to 3 times per node
6. **Progress tracking** - Progress is calculated based on blocks received by all nodes
7. **Completion** - When all nodes have all blocks, distribution is complete

**Note**: After distribution completes, you can initiate a coordinated reboot to switch all nodes to the new firmware simultaneously (see [Coordinated Reboot](#coordinated-reboot)).

## Performing Updates

The complete OTA update process includes:

1. **Download firmware** - Root node downloads firmware from update server (see [Downloading Firmware](#downloading-firmware))
2. **Verify integrity** - Firmware is automatically validated after download
3. **Distribute to nodes** - Root node distributes firmware to all mesh nodes (see [Distributing Firmware to Mesh Nodes](#distributing-firmware-to-mesh-nodes))
4. **Verify distribution** - Check that all nodes received all blocks successfully
5. **Coordinated reboot** - Initiate simultaneous reboot of all nodes to switch to new firmware (see [Coordinated Reboot](#coordinated-reboot))

## Coordinated Reboot

After firmware distribution is complete, you can initiate a coordinated reboot to switch all mesh nodes to the new firmware simultaneously. The reboot process ensures all nodes verify their firmware is ready before rebooting.

### Prerequisites

- Distribution must be complete (all nodes have received all blocks)
- All nodes must have valid firmware in their inactive OTA partition
- Mesh network must be stable

### Initiating Coordinated Reboot

To initiate a coordinated reboot, send a POST request to the root node's web API:

```bash
curl -X POST http://<root-node-ip>/api/ota/reboot
```

Optionally, you can specify timeout and delay parameters:

```bash
curl -X POST http://<root-node-ip>/api/ota/reboot \
  -H "Content-Type: application/json" \
  -d '{"timeout": 15, "delay": 2000}'
```

- `timeout`: Timeout for waiting for all nodes to be ready (in seconds, default: 10)
- `delay`: Delay before reboot in milliseconds (default: 1000)

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
  "error": "Reboot failed: ..."
}
```

### What Happens During Coordinated Reboot

1. **Verification** - Root verifies distribution is complete (all nodes have all blocks)
2. **Preparation** - Root sends PREPARE_REBOOT command to all nodes
3. **Node verification** - Each node verifies its firmware is complete and valid
4. **Acknowledgments** - Nodes send ACK when ready (status=0) or error (status=1)
5. **Coordination** - Root waits for all ACKs (with timeout)
6. **Reboot command** - If all nodes ready, root sends REBOOT command to all
7. **Simultaneous reboot** - All nodes reboot and switch to new firmware partition

### Troubleshooting Reboot Issues

**Problem**: Reboot fails with timeout error.

**Solutions**:
- Check that distribution is complete (all nodes have all blocks)
- Verify mesh network connectivity
- Check serial logs for nodes that are not ready
- Increase timeout value if nodes need more time to verify firmware
- Ensure all nodes have sufficient flash space

**Problem**: Some nodes don't reboot.

**Solutions**:
- Check serial logs on nodes that didn't reboot
- Verify nodes received REBOOT command
- Check for firmware validation errors
- Ensure boot partition can be set correctly

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

### Rollback Issues

**Problem**: Rollback triggered unexpectedly after successful update.

**Solutions**:
- Check mesh network connectivity (verify network is stable and accessible)
- Verify firmware compatibility with mesh network configuration
- Check serial logs for "Rollback flag detected" messages to understand why rollback was triggered
- Ensure mesh connection is established within 5 minutes after reboot
- Review network conditions (very slow networks might trigger rollback unnecessarily)

**Problem**: Rollback not working when expected.

**Solutions**:
- Verify both OTA partitions (app0 and app1) are valid and contain firmware
- Check serial logs for rollback-related error messages
- Verify rollback attempt limit hasn't been exceeded (check for "Rollback attempt limit exceeded" messages)
- Ensure NVS storage is functioning correctly
- Check that rollback flag is set before reboot (check logs for "Rollback flag set before coordinated reboot")

**Problem**: Device continuously rebooting (infinite rollback loop).

**Solutions**:
- Check serial logs for "Rollback attempt limit exceeded" message (system should stop after 3 attempts)
- Verify mesh network is operational and accessible
- Check for firmware compatibility issues that prevent mesh connection
- If rollback loop persists, manually reflash firmware using physical access

## FAQ

### Q: What is the current firmware version?

A: The current firmware version is displayed in serial logs during boot. Look for `[STARTUP] Firmware version: X.Y.Z` in the logs.

### Q: Can I downgrade to an older firmware version?

A: No, downgrades are automatically prevented by the OTA system. When you attempt to download, distribute, or install firmware with an older version number, the operation is rejected. Only the same version (for re-installation) or newer versions are allowed. This prevents accidentally installing older firmware that might have security vulnerabilities or missing features.

If you need to downgrade for testing or recovery purposes, you would need to manually flash the older firmware using a physical connection to the device.

### Q: What happens if an OTA update fails?

A: The system includes automatic rollback mechanisms. If a device can't connect to the mesh network after updating to new firmware, it will automatically roll back to the previous working firmware version. The rollback happens automatically without user intervention. See the [Automatic Rollback Mechanism](#automatic-rollback-mechanism) section for more details.

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

A: No, the device does NOT automatically reboot after download or distribution. After distribution completes, you must manually initiate a coordinated reboot using the `/api/ota/reboot` endpoint to switch all nodes to the new firmware simultaneously.

### Q: Can I download firmware to non-root nodes?

A: No, only the root node can download firmware because it's the only node with internet access via the router. However, after downloading, the root node automatically distributes the firmware to all mesh nodes via the mesh network.

### Q: How does mesh distribution work?

A: The root node fragments the downloaded firmware into 1KB blocks and sends them to all leaf nodes. Each node acknowledges receipt of each block, and the root retries failed blocks up to 3 times. Distribution progress can be monitored via the `/api/ota/distribution/status` endpoint.

### Q: What happens if a node disconnects during distribution?

A: If a node disconnects during distribution, the root node will continue distributing to remaining nodes. The disconnected node can reconnect and request the update again, or distribution can be restarted once the node reconnects.

### Q: How long does distribution take?

A: Distribution time depends on firmware size, number of nodes, and mesh network conditions. A typical 1MB firmware distributed to 5 nodes may take 2-5 minutes. Progress can be monitored via the API.

### Q: Can I cancel distribution?

A: Yes, you can cancel an ongoing distribution by sending a POST request to `/api/ota/distribution/cancel`. The distribution will be aborted and nodes will retain whatever blocks they have received so far.

### Q: How does coordinated reboot work?

A: Coordinated reboot ensures all mesh nodes reboot simultaneously to switch to the new firmware. The root node first verifies all nodes have complete firmware, then sends a PREPARE_REBOOT command. Nodes verify their firmware is ready and send ACK. Once all nodes are ready, the root sends a REBOOT command and all nodes reboot within the specified delay.

### Q: What happens if some nodes are not ready for reboot?

A: If some nodes are not ready when the timeout expires, the reboot process fails and an error is returned. You should check the distribution status and verify all nodes have complete firmware before retrying the reboot. Individual nodes that aren't ready will log error messages.

### Q: Can leaf nodes request updates?

A: Yes, leaf nodes can send an OTA_REQUEST message to the root node to request a firmware update. If firmware is available and distribution is not already in progress, the root node will start distributing firmware to all nodes.

### Q: What happens if I try to install an older firmware version?

A: The OTA system will reject the update at multiple checkpoints. If you attempt to download older firmware, the download will fail with a "Downgrade prevented" error. If older firmware somehow gets into the system, distribution and reboot operations will also reject it. The system will log the current and attempted versions so you can see what happened.

### Q: Why is downgrade prevention important?

A: Downgrade prevention protects your system from:
- Security vulnerabilities in older firmware versions
- Loss of features that were added in newer versions
- Configuration incompatibilities between old and new firmware
- Accidental installation of the wrong firmware version

---

> **For Developers**: See [OTA Download Guide](../dev-guides/ota-mupdate/ota-download.md) for download implementation details, [OTA Distribution Guide](../dev-guides/ota-mupdate/ota-distribution.md) for mesh distribution protocol details, and [OTA Leaf Handler Guide](../dev-guides/ota-mupdate/ota-leaf-handler.md) for leaf node implementation details.
