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

> **For Developers**: See [Developer Guide](../dev-guides/ota-foundation.md) for technical implementation details.

### What Can OTA Updates Do?

- Update firmware on all mesh nodes remotely
- Deploy bug fixes and new features without physical access
- Maintain version consistency across all nodes
- Roll back to previous firmware version if needed (when implemented)
- Check current firmware version via web interface or serial logs

### Current Status

The OTA foundation is currently implemented, providing:
- Dual OTA partition support (app0 and app1)
- Firmware version management and tracking
- Version comparison to prevent downgrades

Full OTA update functionality (download, distribution, and coordinated reboot) will be available in future updates.

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

When the OTA update interface is fully implemented, you will be able to check the firmware version through the web interface. The version will be displayed in the OTA update section.

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

## Performing Updates

> **Note**: Full OTA update functionality (download, distribution, and coordinated reboot) is not yet implemented. This section will be updated when the feature is available.

When OTA updates are fully implemented, the update process will:

1. **Initiate update** - Start update from root node web interface or via leaf node request
2. **Download firmware** - Root node downloads firmware from update server
3. **Distribute to nodes** - Root node distributes firmware to all mesh nodes
4. **Verify integrity** - All nodes verify firmware checksum
5. **Coordinated reboot** - All nodes reboot simultaneously to new firmware

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

---

> **For Developers**: See [Developer Guide](../dev-guides/ota-foundation.md) for technical implementation details, API reference, and integration information.
