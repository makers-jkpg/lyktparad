# Plugins - User Guide

**Last Updated:** 2025-01-27

**Note**: Basic UI feature added for plugins without custom HTML files. Plugin protocol redesigned with plugin ID prefix (0x0B-0xEE). API endpoints added for plugin control (stop, pause, reset).

## Table of Contents

1. [Overview](#overview)
2. [Available Plugins](#available-plugins)
3. [Using Plugins](#using-plugins)
4. [Web Interface](#web-interface)
5. [API Integration](#api-integration)
6. [Troubleshooting](#troubleshooting)

## Overview

### What are Plugins?

Plugins are modular extensions to the mesh network firmware that provide additional visualization modes and features. Each plugin handles specific types of commands and can provide web interfaces for configuration and control.

### Plugin System Benefits

- **Modular Design**: Each plugin is independent and can be enabled or disabled
- **Automatic Integration**: Plugins are automatically discovered and integrated into the system
- **Web Interfaces**: Plugins can provide web-based configuration and control interfaces
- **Command Isolation**: Each plugin has its own command ID range, preventing conflicts

### How Plugins Work

Plugins register with the system during firmware initialization and receive a unique plugin ID (0x0B-0xEE). When mesh commands are received, the system automatically routes commands to the appropriate plugin based on the plugin ID in the command. The plugin protocol is self-contained and stateless - each command includes the plugin ID, making commands independent of activation state.

## Available Plugins

### Effects Plugin

The Effects plugin provides synchronized visual effects across all mesh nodes.

**Features:**
- Strobe effects (on/off flashing)
- Fade effects (smooth color transitions)
- Configurable timing and colors
- Repeat count support

**Commands:**
- Effect commands are sent via mesh with effect parameters
- Supports strobe and fade effect types

### Sequence Plugin

The Sequence plugin provides synchronized color sequence playback across all mesh nodes.

**Features:**
- 16x16 color grid for sequence design
- Synchronized playback across all nodes
- Tempo control (speed adjustment)
- Start, stop, and reset controls
- Beat synchronization for child nodes

**Commands:**
- Sequence data storage and broadcast
- Start, stop, reset, and beat commands
- Pointer synchronization

**Web Interface:**
- Visual grid editor for creating sequences
- Tempo controls
- Playback controls (start, stop, reset)
- Export/import functionality

## Using Plugins

### Command Format

Plugins receive commands via the mesh network using a self-contained protocol format:

```
[PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N]
```

- **PLUGIN_ID** (1 byte): Plugin's assigned plugin ID (0x0B-0xEE)
- **CMD** (1 byte): Command type:
  - `PLUGIN_CMD_START` (0x01): Start plugin playback
  - `PLUGIN_CMD_PAUSE` (0x02): Pause plugin playback
  - `PLUGIN_CMD_RESET` (0x03): Reset plugin state
  - `PLUGIN_CMD_DATA` (0x04): Plugin-specific data (variable length)
  - `PLUGIN_CMD_BEAT` (0x05): Beat synchronization
- **LENGTH** (2 bytes, optional): Length prefix for variable-length data (only for DATA commands)
- **DATA** (N bytes, optional): Command-specific parameters

**Total size**: Maximum 1024 bytes (including all fields)

**Mutual Exclusivity**: When a START command is received for a plugin, the system automatically stops any other running plugin before activating the target plugin.

### Effects Plugin Usage

#### Strobe Effect

Send a strobe effect command with:
- Effect ID: `EFFECT_STROBE` (1)
- On color: RGB values for "on" state
- Off color: RGB values for "off" state
- Duration on: Time in milliseconds for "on" state
- Duration off: Time in milliseconds for "off" state
- Repeat count: Number of cycles (0 = infinite)

#### Fade Effect

Send a fade effect command with:
- Effect ID: `EFFECT_FADE` (2)
- Start color: RGB values for fade start
- End color: RGB values for fade end
- Fade in time: Time in milliseconds for fade in
- Fade out time: Time in milliseconds for fade out
- Hold time: Time in milliseconds to hold at end color
- Repeat count: Number of cycles (0 = infinite)

### Sequence Plugin Usage

#### Storing Sequence Data

Send sequence data with:
- Rhythm: Tempo value (1-255, where 25 = 250ms per step)
- Number of rows: Sequence length (1-16)
- Color data: Packed color array (16 squares per row)

#### Controlling Playback

- **Start**: Begin sequence playback
- **Stop**: Stop sequence playback
- **Reset**: Reset sequence pointer to beginning

#### Root Node Behavior

On the root node:
- Sequence data is stored and broadcast to all child nodes
- Playback is synchronized across all nodes
- Beat commands are broadcast to child nodes for synchronization

#### Child Node Behavior

On child nodes:
- Sequence data is received and stored
- Playback follows root node timing
- Beat commands update pointer position for synchronization

## Web Interface

### Accessing the Web Interface

Plugins with web interfaces are accessible through:
- **Embedded Webserver**: Connect to the root node's IP address (if root node)
- **External Webserver**: Access via the Node.js server (if running)

### Plugin Selection

The web interface includes a dropdown menu at the top right of the page that allows you to select which plugin's interface to view. Only one plugin's interface is visible at a time.

**Using the Dropdown:**
1. Click the dropdown menu at the top right of the page
2. Select the plugin you want to view from the list
3. The selected plugin's interface will be displayed
4. Your selection is saved and will persist across page reloads

**Page Layout:**
- **Header (Top 150px)**: Fixed header showing "MAKERS JÖNKÖPING LJUSPARAD 2026" title and the plugin dropdown
- **Content Area**: Takes up the remaining viewport height, showing the selected plugin's interface

**Plugin Visibility:**
- Only the selected plugin's HTML is visible
- Other plugins are hidden automatically
- The dropdown shows all available plugins with formatted names (e.g., "Effects", "Sequence")

### Basic Plugin UI

Plugins that don't provide custom HTML files automatically receive a basic control interface. This basic UI allows you to control plugins that don't need complex interfaces but still support basic operations.

**Basic UI Features:**
- **Plugin Name Display**: Shows the formatted plugin name at the top
- **Status Indicator**: Displays whether the plugin is currently Active or Inactive
- **Control Buttons**:
  - **START**: Activates the plugin and begins its operation
  - **PAUSE**: Temporarily pauses the plugin's operation (plugin remains active)
  - **RESET**: Resets the plugin's internal state
  - **STOP**: Gracefully stops the plugin (calls pause callback if available) and deactivates it
- **Feedback Messages**: Shows success/error messages for each operation

**Using Basic UI:**
1. Select a plugin from the dropdown that doesn't have a custom interface
2. The basic UI will appear with control buttons
3. Click START to activate the plugin
4. Use PAUSE to temporarily pause, RESET to reset state, or STOP to deactivate
5. The status indicator updates automatically to show the current state

**Note**: Plugins with custom HTML files will show their custom interface instead of the basic UI. The basic UI is only provided for plugins that don't need complex interfaces.

### Sequence Plugin Web Interface

The Sequence plugin provides a full web interface for creating and controlling sequences.

#### Grid Editor

- **16x16 Color Grid**: Click squares to set colors
- **Color Picker**: Select colors for grid squares
- **Row Management**: Add or remove rows (1-16 rows)
- **Visual Preview**: See sequence as you design it

#### Tempo Controls

- **Tempo Slider**: Adjust playback speed (1-255)
- **Tempo Display**: Shows current tempo value
- **Real-time Updates**: Tempo changes apply immediately

#### Playback Controls

- **Start Button**: Begin sequence playback
- **Stop Button**: Stop sequence playback
- **Reset Button**: Reset to beginning
- **Status Display**: Shows current playback state

#### Export/Import

- **Export**: Download sequence as CSV file
- **Import**: Upload sequence from CSV file
- **Format**: CSV format with color data

### Effects Plugin

The Effects plugin does not currently provide a web interface. Effects are controlled via mesh commands or API calls.

## API Integration

### HTTP API

Plugins can be controlled via HTTP API endpoints on both embedded and external webservers.

#### General Plugin API

**Plugin Activation/Deactivation:**
- `POST /api/plugin/activate` - Activate a plugin by name
  - Request: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}`
- `POST /api/plugin/deactivate` - Deactivate a plugin by name
  - Request: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}`
- `GET /api/plugin/active` - Get currently active plugin
  - Response: `{"plugin": "plugin_name"}` or `{"plugin": null}`

**Plugin Control Commands:**
- `POST /api/plugin/stop` - Stop plugin (gracefully pauses then deactivates)
  - Request: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}`
- `POST /api/plugin/pause` - Pause plugin playback
  - Request: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}`
- `POST /api/plugin/reset` - Reset plugin state
  - Request: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}`

**Plugin Discovery:**
- `GET /api/plugins` - Get list of all registered plugins
  - Response: `{"plugins": ["plugin1", "plugin2", ...]}`

#### Sequence Plugin API

**Store Sequence Data:**
```
POST /api/sequence
Content-Type: application/json

{
  "rhythm": 25,
  "num_rows": 16,
  "color_data": [/* packed color array */]
}
```

**Start Playback:**
```
POST /api/sequence/start
```

**Stop Playback:**
```
POST /api/sequence/stop
```

**Reset Pointer:**
```
POST /api/sequence/reset
```

**Get Status:**
```
GET /api/sequence/status
```

**Get Pointer:**
```
GET /api/sequence/pointer
```

#### Effects Plugin API

Effects are typically controlled via mesh commands rather than HTTP API, but API endpoints may be available depending on implementation.

### UDP Bridge API

The UDP bridge can forward API commands to the mesh network, allowing remote control of plugins.

## Troubleshooting

### Plugin Not Responding

**Symptoms:**
- Commands sent to plugin are not executed
- Plugin appears inactive

**Solutions:**
- Check that plugin is registered (check firmware logs)
- Verify command ID matches plugin's assigned ID
- Check mesh network connectivity
- Verify command data format is correct

### Web Interface Not Loading

**Symptoms:**
- Plugin web interface doesn't appear
- JavaScript errors in browser console
- CSS styles not applied

**Solutions:**
- Check that plugin HTML/JS/CSS files exist
- Verify webserver is running (embedded or external)
- Check browser console for errors
- Clear browser cache and reload

### Sequence Not Synchronized

**Symptoms:**
- Child nodes out of sync with root node
- Sequence playback timing is off

**Solutions:**
- Ensure root node is broadcasting beat commands
- Check mesh network connectivity
- Verify all nodes have same sequence data
- Check tempo settings are consistent

### Effects Not Playing

**Symptoms:**
- Effect commands sent but no visual output
- Effect stops unexpectedly

**Solutions:**
- Check effect parameters are valid
- Verify LED hardware is connected
- Check effect timer is running
- Review firmware logs for errors

### Command ID Conflicts

**Symptoms:**
- Commands routed to wrong plugin
- Unexpected behavior

**Solutions:**
- Plugin IDs are assigned automatically - conflicts should not occur
- Plugin IDs are deterministic based on registration order in firmware
- If issues persist, verify all nodes have the same firmware version
- Check plugin registration order matches across all nodes
- Verify only one plugin with same name is registered

## Additional Resources

- [Plugin System Developer Guide](../dev-guides/plugin-system.md) - For developers creating plugins
- [Plugin Build System Guide](../dev-guides/plugin-build-system.md) - Build system details
- [Sequence Mode User Guide](mode-sequence.md) - Detailed sequence mode documentation
- [Mesh Commands Reference](../include/mesh_commands.h) - Command ID reference
