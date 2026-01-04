# Mesh Force Node Status - Development Guide

This document describes code patterns and API usage for attempting to force node status in ESP-IDF mesh networks. These approaches were found to be unreliable due to fundamental limitations in the ESP-IDF mesh implementation.

## Overview

This guide covers the code changes made to force a node to be either a root node or a non-root node, and to maintain that status while allowing mesh connectivity. The focus is on the actual ESP-IDF mesh API calls and event handling code that was used.

## Mesh Networking Information Element (IE)

Understanding the mesh networking IE is crucial to understanding why node status forcing is unreliable:

- **What it is**: The mesh networking IE is a data structure embedded in Wi-Fi beacons and probe responses that carries mesh network state information
- **How it works**: All mesh nodes broadcast this IE in their beacons, allowing other nodes to discover mesh networks and understand network state
- **Key flags in the IE**:
  - `MESH_ASSOC_FLAG_ROOT_FIXED`: Indicates if fixed root is enabled in the network
  - `MESH_ASSOC_FLAG_VOTE_IN_PROGRESS`: Indicates root election voting is in progress
  - `MESH_ASSOC_FLAG_NETWORK_FREE`: Indicates no root exists in current network
  - `MESH_ASSOC_FLAG_ROOTS_FOUND`: Indicates root conflict (multiple roots detected)
- **Propagation**: When a device joins a mesh network, it reads the parent's mesh networking IE and updates its own settings to match
- **Why this matters**: The mesh networking IE can override local API settings. If a device joins a network with different fixed root settings, it will update to match the network's setting (see `MESH_EVENT_ROOT_FIXED` event)
- **Timing issues**: There's a delay between calling mesh APIs and the mesh networking IE being updated and broadcast. This creates race conditions where API calls may succeed but the network state hasn't propagated yet

## Forcing Root Node Status

### Basic Root Fixing

The fundamental API call to fix a node as root:

```c
esp_err_t err = esp_mesh_fix_root(true);
```

**Internal Mechanism**:**
- `esp_mesh_fix_root(true)` enables the "Fixed Root Setting" which disables automatic root node election via voting
- This setting is propagated through the mesh network via the mesh networking Information Element (IE) in beacons
- The `MESH_ASSOC_FLAG_ROOT_FIXED` flag is set in the mesh networking IE, which all nodes read to determine if fixed root is enabled
- When fixed root is enabled, the mesh stack will not initiate root election voting, and nodes will not attempt to become root unless explicitly designated
- **Important**: All devices in the network must use the same Fixed Root Setting (enabled or disabled). If a device joins with a different setting, it will update its setting to match the parent's setting (see `MESH_EVENT_ROOT_FIXED` event)

This must be called before `esp_mesh_start()`. To verify it took effect:

```c
bool is_fixed = esp_mesh_is_root_fixed();
```

**Note**: The fixed root status can be checked via the mesh networking IE flags, not just the local API call. The status may change if a device joins a network with a different fixed root setting.

### Device Type Configuration

To explicitly declare a device as root:

```c
esp_err_t err = esp_mesh_set_type(MESH_ROOT);
```

**Internal Mechanism**:**
- `esp_mesh_set_type()` designates the device type over the mesh network
- `MESH_ROOT`: Designates the root node - the only sink of the mesh network with ability to access external IP network
- `MESH_LEAF`: Designates a device as a standalone Wi-Fi station that connects to a parent but has no forwarding ability
- `MESH_NODE`: Intermediate device with forwarding ability
- `MESH_IDLE`: Device hasn't joined the mesh network yet
- The device type is used by the mesh stack to determine routing behavior and network topology
- **Important**: Setting `MESH_LEAF` does not guarantee the device won't become root - the mesh stack's root election algorithm can override this during network events

This should be called before `esp_mesh_start()` along with `esp_mesh_fix_root(true)`.

### Self-Organization Configuration

The critical challenge is managing self-organization. Initial attempts disabled it:

```c
esp_err_t err = esp_mesh_set_self_organized(false, false);
```

**Internal Mechanism of Self-Organized Networking**:**
- Self-organized networking is a feature where nodes autonomously scan/select/connect/reconnect to other nodes and routers
- It has three main functions:
  1. **Selection or election of the root node**: When enabled, nodes participate in automatic root node election based primarily on RSSI from the external router
  2. **Selection of a preferred parent node**: Nodes automatically find and connect to the best parent node based on signal strength and network topology
  3. **Automatic reconnection**: Upon detecting disconnection, nodes automatically attempt to reconnect
- When self-organized networking is enabled, the ESP-WIFI-MESH stack **internally makes calls to Wi-Fi APIs** (scan, connect, disconnect). This is why the application layer must not call Wi-Fi APIs while self-organized networking is enabled - it would interfere with the mesh stack's internal operations
- The mesh stack uses these Wi-Fi API calls to:
  - Scan for available parent nodes or routers
  - Connect to selected parents
  - Monitor connection status
  - Handle reconnection attempts

**Problem**: When self-organization is disabled, the mesh softAP (Access Point) may not accept child connections. The mesh AP needs self-organization enabled to properly handle association requests from child nodes. Disabling it prevents the root from accepting new child connections, even though the mesh AP is technically active.

### Enabling Self-Organization After Router Connection

To allow child connections while maintaining router connection, self-organization was enabled after the router connected:

```c
// In IP_EVENT_STA_GOT_IP event handler
if (is_root && is_root_node_forced) {
    // Verify root is still fixed
    bool is_root_fixed = esp_mesh_is_root_fixed();
    if (!is_root_fixed) {
        esp_mesh_fix_root(true);  // Re-fix if lost
    }

    // Enable self-organization with select_parent=false
    esp_err_t err = esp_mesh_set_self_organized(true, false);

    // Verify root is still fixed after enabling
    is_root_fixed = esp_mesh_is_root_fixed();
    if (!is_root_fixed) {
        esp_mesh_fix_root(true);  // Re-fix again
    }
}
```

**Internal Mechanism of `select_parent` Parameter**:**
- When `esp_mesh_set_self_organized(true, select_parent)` is called:
  - If `select_parent=false`: The node maintains its current Wi-Fi state. For a root node already connected to router, it stays connected. Nodes already connected to a parent remain connected. This mode allows the mesh AP to accept child connections without causing the root to search for a new parent.
  - If `select_parent=true`: The behavior depends on node type:
    - **For non-root nodes**: Nodes without a parent will automatically select a preferred parent and connect. Nodes already connected will disconnect, reselect a preferred parent, and reconnect.
    - **For root nodes**: A root node must give up its role as root to connect to a parent node. Therefore, calling `esp_mesh_set_self_organized(true, true)` on a root node causes it to disconnect from the router and all child nodes, select a preferred parent node, and connect - effectively demoting itself from root.

**Problem**:
- Root fixing could be lost when self-organization was enabled because the mesh stack's internal state management may clear the fixed root flag during the transition
- The mesh networking IE may not immediately reflect the fixed root status after enabling self-organization
- There's a race condition between enabling self-organization and the mesh stack updating its internal state
- The mesh AP may not immediately accept connections even after self-organization is enabled, requiring additional time for the mesh stack to update its association handling

### Router Disconnection Handling

When the router disconnected, self-organization was disabled to prevent the root from searching for a parent:

```c
// In IP_EVENT_STA_LOST_IP event handler
if (is_root && is_root_node_forced) {
    esp_mesh_set_self_organized(false, false);
}
```

### Root Status Monitoring in Event Handlers

Multiple event handlers were needed to monitor and enforce root status:

```c
// In MESH_EVENT_STARTED handler
if (is_root_node_forced && !esp_mesh_is_root()) {
    // Violation detected - reboot
    esp_restart();
}

// In MESH_EVENT_PARENT_DISCONNECTED handler
if (is_root_node_forced && is_router_connected) {
    bool is_root_fixed = esp_mesh_is_root_fixed();
    if (!is_root_fixed) {
        esp_mesh_fix_root(true);  // Re-fix
    }
}

// In MESH_EVENT_ROOT_SWITCH_ACK handler
if (is_root_node_forced && !esp_mesh_is_root()) {
    // Violation detected - reboot
    esp_restart();
}
```

## Forcing Non-Root Node Status

### Initial Approach - Disable Self-Organization

The first attempt disabled self-organization to prevent root election:

```c
esp_err_t err = esp_mesh_set_self_organized(false, false);
```

**Internal Mechanism When Disabling Self-Organization**:**
- When self-organization is disabled, ESP-WIFI-MESH attempts to maintain the node's current Wi-Fi state:
  - If the node was previously connected to other nodes, it remains connected
  - If the node was previously disconnected and scanning for a parent node or router, it stops scanning
  - If the node was previously attempting to reconnect, it stops reconnecting
- **Critical**: With self-organization disabled, the node cannot automatically find or connect to parent nodes. The application must manually set a parent using `esp_mesh_set_parent()` if the node needs to connect to the mesh network.
- The mesh stack will not make any Wi-Fi API calls (scan, connect, etc.) when self-organization is disabled, allowing the application to safely call Wi-Fi APIs directly.

**Problem**:
- This also prevented connection to parent nodes because the mesh stack stops all automatic scanning and connection attempts
- Without self-organization, the mesh AP may not properly handle association requests from child nodes
- Manual parent setting via `esp_mesh_set_parent()` requires knowing the exact parent's SSID, channel, and BSSID, which is impractical for dynamic mesh networks

### Enable Self-Organization Without Router Connection

To allow parent connection while preventing router connection:

```c
esp_mesh_set_self_organized(true, false);  // Enable self-org, disable router
esp_mesh_set_type(MESH_LEAF);              // Set as leaf node
esp_mesh_fix_root(false);                   // Release root fixing
```

**Internal Mechanism**:**
- `esp_mesh_set_self_organized(true, false)` enables self-organization but with `select_parent=false`, which means:
  - The node will maintain its current connection state
  - If disconnected, it will not automatically search for a parent (requires manual `esp_mesh_connect()` call)
  - However, this doesn't actually "disable router connection" - the second parameter controls parent selection behavior, not router connection
- **Root Election Process**: The ESP-WIFI-MESH root election algorithm works as follows:
  - Root election is primarily based on **RSSI from the external router**
  - During networking, devices vote for which device should be root
  - Only when a device obtains vote percentage that reaches the threshold (default 0.9, set via `esp_mesh_set_vote_percentage()`) can it become root
  - The mesh stack's root election algorithm can override device type settings during network topology changes
  - If no router is available and fixed root is disabled, nodes will still participate in root election to establish a mesh network

**Problem**:
- This still allowed root election under certain network conditions because:
  - Setting `MESH_LEAF` type doesn't prevent root election - it only designates the device type, which can be overridden
  - The root election algorithm runs independently of device type settings
  - If the router becomes unavailable or RSSI conditions change, the mesh stack may initiate root election even with these settings
  - Network topology changes (parent disconnection, new nodes joining) can trigger root election

### Full Self-Organization with Leaf Type

The final approach enabled full self-organization but set device type to leaf:

```c
esp_mesh_set_self_organized(true, true);    // Full self-organization
esp_mesh_set_type(MESH_LEAF);               // Prevent root election
esp_mesh_fix_root(false);                   // Release root fixing
```

**Internal Mechanism**:**
- `esp_mesh_set_self_organized(true, true)` enables full self-organization with `select_parent=true`:
  - Non-root nodes without a parent will automatically select a preferred parent and connect
  - Nodes already connected will disconnect, reselect a preferred parent, and reconnect
  - This enables full autonomous networking behavior
- **Root Election Override**: The ESP-WIFI-MESH root election process operates at a lower level than device type settings:
  - Root election is based on voting mechanism where devices vote for root candidates
  - The election algorithm considers RSSI from router, network capacity, and other factors
  - During root election, the mesh stack can change a device's type from `MESH_LEAF` to `MESH_ROOT` if it wins the election
  - The `MESH_LEAF` type is more of a "preference" than a hard constraint - it indicates the device should not forward packets, but doesn't prevent it from becoming root
  - According to ESP-IDF documentation: "ESP-WIFI-MESH supports designating a device as a child node. Once designated, the device will not participate in the root node election" - but this requires proper configuration that may not be achievable through `esp_mesh_set_type()` alone

**Problem**:
- The mesh stack could still override the leaf type during root election because:
  - Root election happens at the mesh protocol level, which can override application-level type settings
  - If no other suitable root candidate exists and the device has good router RSSI, it may be elected root despite being set as `MESH_LEAF`
  - Network topology changes (root failure, router disconnection) can trigger emergency root election where type constraints are relaxed
  - The mesh networking IE carries root election information that can override local device type settings

### Non-Root Status Monitoring

Event handlers monitored for violations:

```c
// In MESH_EVENT_STARTED handler
if (is_mesh_node_forced && esp_mesh_is_root()) {
    // Violation detected - reboot
    esp_restart();
}

// In MESH_EVENT_PARENT_CONNECTED handler
if (is_mesh_node_forced && esp_mesh_is_root()) {
    // Violation detected - reboot
    esp_restart();
}

// In IP_EVENT_STA_GOT_IP handler
if (is_mesh_node_forced && esp_mesh_is_root()) {
    // Violation detected - reboot
    esp_restart();
}
```

## Complete Initialization Sequence for Forced Root

```c
// Before esp_mesh_start()
esp_mesh_set_type(MESH_ROOT);
esp_mesh_fix_root(true);
esp_mesh_set_self_organized(false, false);  // Disable initially

// Start mesh
esp_mesh_start();

// After router connection (IP_EVENT_STA_GOT_IP)
bool is_root_fixed = esp_mesh_is_root_fixed();
if (!is_root_fixed) {
    esp_mesh_fix_root(true);
}
vTaskDelay(pdMS_TO_TICKS(100));  // Delay for stability
esp_mesh_set_self_organized(true, false);  // Enable with select_parent=false

// Verify
is_root_fixed = esp_mesh_is_root_fixed();
if (!is_root_fixed) {
    esp_mesh_fix_root(true);  // Re-fix
}
```

## Complete Initialization Sequence for Forced Non-Root

```c
// Before esp_mesh_start()
esp_mesh_set_type(MESH_LEAF);
esp_mesh_set_self_organized(true, true);   // Full self-organization
esp_mesh_fix_root(false);                  // Release root fixing

// Start mesh
esp_mesh_start();
```

## Root Election Mechanism

Understanding root election is essential to understanding why forcing non-root status fails:

- **Voting Process**: Root election uses a voting mechanism where devices vote for root candidates
  - Devices vote primarily based on **RSSI from the external router**
  - A device can only become root when it obtains a vote percentage that reaches the threshold (default 0.9, configurable via `esp_mesh_set_vote_percentage()`)
  - The voting process is initiated automatically when self-organization is enabled and no root exists, or when the current root initiates voting via `esp_mesh_waive_root()`

- **Election Triggers**: Root election can be triggered by:
  - Network startup when no root exists
  - Root node failure or disconnection
  - Router disconnection causing root to search for new parent
  - Manual root switch request via `esp_mesh_waive_root()`
  - Network topology changes that make current root unsuitable

- **Election Override**: During root election:
  - The mesh stack evaluates all nodes as potential root candidates
  - Device type settings (`MESH_LEAF`, `MESH_NODE`) can be overridden if a device wins the vote
  - The election algorithm prioritizes network connectivity over manual type designations
  - Fixed root setting (`esp_mesh_fix_root(true)`) disables voting, but if fixed root is lost, election can resume

- **Why Forcing Fails**:
  - Root election operates at the mesh protocol level, independent of application-level type settings
  - The election algorithm's goal is to ensure network connectivity, which may require overriding manual constraints
  - Network events (disconnections, topology changes) can trigger election even when type is set to `MESH_LEAF`

## Why These Approaches Failed

### Root Node Issues

1. **Self-Organization Conflict**:
   - `esp_mesh_set_self_organized(true, true)` with `select_parent=true` causes the root to search for a parent, disconnecting from the router
     - **Why**: According to ESP-IDF documentation, "For a root node to connect to a parent node, it must give up its role as root. Therefore, a root node will disconnect from the router and all child nodes, select a preferred parent node, and connect."
     - The mesh stack internally makes Wi-Fi API calls to scan for and connect to a parent, which requires the root to relinquish its router connection
   - `esp_mesh_set_self_organized(true, false)` with `select_parent=false` doesn't reliably allow child connections
     - **Why**: While this maintains the root's router connection, the mesh AP's ability to accept child connections depends on the mesh stack's internal state
     - The mesh AP needs the mesh stack to be in a state where it can process association requests, which may not be immediately available after enabling self-organization
     - There's a timing issue where the mesh networking IE may not immediately reflect that the root is ready to accept children
   - The mesh AP may not accept connections when self-organization is disabled
     - **Why**: The mesh softAP is managed by the mesh stack, which requires self-organization to be enabled to properly handle the association/disassociation lifecycle of child nodes
     - Without self-organization, the mesh stack doesn't actively manage the AP's connection state

2. **Root Fixing Not Persistent**:
   - `esp_mesh_fix_root(true)` can return `ESP_OK` but the root status may not persist
     - **Why**: The fixed root setting is stored in the mesh networking IE and propagated through beacons. However, the local API call may succeed before the IE is updated and broadcast
     - The fixed root status is also influenced by the parent's fixed root setting - if a device joins a network with a different fixed root setting, it will update to match (see `MESH_EVENT_ROOT_FIXED`)
   - Root fixing can be lost when `esp_mesh_set_self_organized()` is called
     - **Why**: Enabling self-organization triggers internal mesh stack state changes that may reset or clear the fixed root flag
     - The mesh stack may need to update the mesh networking IE, and during this transition, the fixed root status may be temporarily lost
     - There's a race condition between the API call and the mesh stack updating its internal state and broadcasting the updated IE
   - Network events can cause root to become unfixed
     - **Why**: Network topology changes (parent disconnection, root switch events, router reconnection) can trigger the mesh stack to re-evaluate the fixed root setting
     - The mesh stack may clear fixed root during network healing or root conflict resolution
     - If the mesh networking IE from other nodes indicates a different fixed root setting, the device may update its setting to match

3. **API Behavior Inconsistency**:
   - `esp_mesh_is_root_fixed()` may return false even after successful `esp_mesh_fix_root(true)`
   - Enabling self-organization can clear root fixing
   - Root status verification requires constant polling and re-fixing

### Non-Root Node Issues

1. **Root Election Override**:
   - `esp_mesh_set_type(MESH_LEAF)` doesn't guarantee the device won't become root
     - **Why**: Device type is a designation, not a hard constraint. The root election algorithm operates independently and can override type settings
     - According to ESP-IDF FAQ: "ESP-WIFI-MESH supports designating a device as a child node. Once designated, the device will not participate in the root node election" - but this requires proper implementation that may involve more than just `esp_mesh_set_type()`
   - The mesh stack's root election algorithm can override leaf type
     - **Why**: Root election is based on voting where devices vote for root candidates primarily based on RSSI from router
     - The election happens at the mesh protocol level, which can change device types during the election process
     - If a `MESH_LEAF` device has the best router RSSI and wins the vote (reaches the vote percentage threshold, default 0.9), it will become root regardless of its type setting
   - Network topology changes can trigger root election
     - **Why**: Events like root node failure, router disconnection, or network healing can trigger automatic root election
     - During root election, the mesh stack evaluates all nodes as potential root candidates, potentially overriding type constraints
     - The `MESH_EVENT_VOTE_STARTED` event indicates when root election begins, and during this process, type settings may be ignored

2. **Self-Organization Requirement**:
   - Disabling self-organization prevents parent connection
   - Enabling self-organization allows root election
   - No configuration allows parent connection while preventing root election

### Fundamental Limitations

1. **ESP-IDF Mesh Design**:
   - The mesh implementation is designed for self-organizing networks
     - **Why**: ESP-WIFI-MESH is built to automatically adapt to dynamic network topologies and conditions
     - The mesh stack internally manages network state, making Wi-Fi API calls to scan, connect, and reconnect as needed
     - Self-organizing behavior is the default and expected mode of operation
   - Manual status forcing conflicts with the self-organizing nature
     - **Why**: The mesh stack's autonomous behavior (root election, parent selection, reconnection) operates independently of manual configuration
     - When self-organization is enabled, the mesh stack makes decisions based on network conditions (RSSI, topology, connectivity) rather than manual settings
     - Manual forcing attempts to impose static configuration on a dynamic system
   - The mesh stack can override manual configuration during network events
     - **Why**: Network events trigger the mesh stack to re-evaluate network state and make autonomous decisions
     - Events like `MESH_EVENT_PARENT_DISCONNECTED`, `MESH_EVENT_ROOT_SWITCH_REQ`, or router disconnection cause the mesh stack to:
       - Re-scan for available parents/routers
       - Re-evaluate root candidates
       - Update network topology
       - Potentially override manual type or fixed root settings to maintain network connectivity
     - The mesh networking IE carries network state information that can override local settings when devices join or network conditions change

2. **State Persistence**:
   - Mesh state changes during network transitions
   - Forced status can be lost during:
     - Router connection/disconnection
     - Parent connection/disconnection
     - Root switch events
     - Network topology changes

3. **Event Handler Complexity**:
   - Maintaining forced status requires monitoring:
     - `MESH_EVENT_STARTED`
     - `MESH_EVENT_PARENT_CONNECTED`
     - `MESH_EVENT_PARENT_DISCONNECTED`
     - `MESH_EVENT_ROOT_SWITCH_ACK`
     - `IP_EVENT_STA_GOT_IP`
     - `IP_EVENT_STA_LOST_IP`
   - Each handler needs status verification and potential re-fixing
   - Reboot-on-violation creates instability

## Key API Functions Used

### Root Status Control

- `esp_mesh_fix_root(bool enable)` - Enable/disable network Fixed Root Setting
  - **Internal**: Sets the `MESH_ASSOC_FLAG_ROOT_FIXED` flag in mesh networking IE
  - Disables automatic root node election via voting
  - All devices in network must use same setting (enforced via mesh networking IE)
  - Can be called at any time after mesh is configured
  - Returns `ESP_OK` on success, but status may not persist through network events

- `esp_mesh_is_root_fixed()` - Check if network Fixed Root Setting is enabled
  - **Internal**: Checks both local setting and mesh networking IE flags
  - Status can change based on parent's fixed root setting (see `MESH_EVENT_ROOT_FIXED`)
  - May return false even after successful `esp_mesh_fix_root(true)` if mesh stack hasn't updated IE yet

- `esp_mesh_is_root()` - Check if device is currently root
  - Returns true/false based on current mesh network state
  - Can change during network events (root switch, reconnection, etc.)

- `esp_mesh_set_type(mesh_type_t type)` - Designate device type over mesh network
  - **Internal**: Sets device type designation (MESH_ROOT, MESH_LEAF, MESH_NODE, MESH_IDLE)
  - Type is used for routing behavior and network topology decisions
  - Can be overridden by mesh stack during root election or network events
  - Must be called before `esp_mesh_start()` for some types

### Self-Organization Control

- `esp_mesh_set_self_organized(bool enable, bool select_parent)` - Enable/disable self-organized networking
  - **Internal**: When enabled, mesh stack internally makes Wi-Fi API calls (scan, connect, disconnect)
  - Three main functions: root election, parent selection, automatic reconnection
  - `select_parent` parameter (only valid when enabling):
    - `true`: Root gives up root status and searches for parent; non-root nodes reselect parent
    - `false`: Maintains current Wi-Fi state, allows manual reconnection via `esp_mesh_connect()`
  - **Critical**: Application must not call Wi-Fi APIs while self-organization is enabled
  - Can be called at runtime to dynamically modify behavior

- `esp_mesh_get_self_organized()` - Check if self-organized networking is enabled
  - Returns true/false based on current mesh stack state

### Status Verification

- `esp_mesh_get_routing_table_size()` - Get number of devices in device's sub-network (including self)
  - Returns routing table size which includes the device itself
  - For root node, includes all nodes in mesh network
  - Value may be incorrect during network topology changes

- `esp_mesh_get_layer()` - Get current layer value over mesh network
  - Returns layer number (MESH_ROOT_LAYER = 0 for root, >0 for non-root)
  - Must be called after `MESH_EVENT_PARENT_CONNECTED` event
  - Layer can change during network topology changes

## Summary

The ESP-IDF mesh API does not reliably support forcing node status. The fundamental issue is that the mesh stack is designed for self-organization, and manual forcing conflicts with this design. While the API provides functions like `esp_mesh_fix_root()` and `esp_mesh_set_type()`, their behavior is not guaranteed to persist through network events.

Attempts to work around these limitations by:
- Enabling/disabling self-organization at different times
- Re-fixing root status in event handlers
- Monitoring status in multiple event handlers
- Rebooting on status violations

All proved unreliable and created complex, hard-to-maintain code that still didn't guarantee the desired behavior.
