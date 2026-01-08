# Plugins - User Guide

**Last Updated:** 2026-01-05

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

### Effect Strobe Plugin

The Effect Strobe plugin provides synchronized strobe (on/off flashing) effects across all mesh nodes.

**Features:**
- Automatic effect start when plugin is activated
- White strobe effect (100ms on, 100ms off)
- Continuous operation until plugin deactivation
- Hardcoded default parameters (no configuration needed)

**Usage:**
- Activate the plugin to start the strobe effect automatically
- Deactivate the plugin to stop the effect
- Effect runs continuously with default parameters

### Effect Fade Plugin

The Effect Fade plugin provides synchronized fade (smooth color transitions) effects across all mesh nodes.

**Features:**
- Automatic effect start when plugin is activated
- Smooth fade transitions (500ms fade in, 200ms hold, 500ms fade out)
- Continuous operation until plugin deactivation
- Hardcoded default parameters (no configuration needed)

**Usage:**
- Activate the plugin to start the fade effect automatically
- Deactivate the plugin to stop the effect
- Effect runs continuously with default parameters

### Sequence Plugin

The Sequence plugin provides synchronized color sequence playback across all mesh nodes.

**Features:**
- 16x16 color grid for sequence design
- Synchronized playback across all nodes
- Tempo control (speed adjustment)
- Start, stop, and reset controls
- Beat synchronization for child nodes
- Hardcoded default RGB-rainbow pattern (loaded automatically if no user data exists)

**Default Data:**
- The sequence plugin includes a hardcoded RGB-rainbow pattern that is automatically loaded at initialization if no user sequence data exists
- Default tempo: 50ms (rhythm = 5)
- Default length: 16 rows (256 squares)
- Users can override the default data by uploading their own sequence via the web UI
- Currently, only RGB-rainbow is supported as default data (future extensibility planned)

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
  - `PLUGIN_CMD_DATA` (0x04): General-purpose plugin data command (variable length). Used by plugins for custom data, configuration, or plugin-specific sub-commands that cannot be handled by standard commands. See [Plugin System Developer Guide](../dev-guides/plugin-system.md) for detailed protocol specification.
  - `PLUGIN_CMD_STOP` (0x05): Stop plugin (deactivate and reset state)
- **LENGTH** (2 bytes, optional): Length prefix for variable-length data (only for DATA commands, plugin-specific)
- **DATA** (N bytes, optional): Command-specific parameters
  - For PLUGIN_CMD_DATA: Contains plugin-specific sub-command ID and data payload. Each plugin defines its own protocol structure.

**Command Sizes**:
- START, PAUSE, RESET, STOP: 2 bytes (PLUGIN_ID + CMD)
- DATA: Variable size (minimum 2 bytes: PLUGIN_ID + CMD, plus plugin-specific data). Maximum recommended payload: 512 bytes.

**When Plugins Use PLUGIN_CMD_DATA**:
Plugins use PLUGIN_CMD_DATA when they need to send custom data or configuration that doesn't fit the simple START/PAUSE/RESET/STOP model. For example, the sequence plugin uses it to send sequence data (colors, rhythm, length), while effect plugins use standard commands since they don't need custom data. For developers creating plugins, see the [Plugin System Developer Guide](../dev-guides/plugin-system.md) for detailed protocol specification and examples.

**Note**: Sequence synchronization is handled via `MESH_CMD_HEARTBEAT` (core mesh command), not via plugin BEAT commands. The heartbeat format is `[MESH_CMD_HEARTBEAT:1] [POINTER:1] [COUNTER:1]` (3 bytes total), where POINTER is the sequence pointer (0-255, 0 when sequence inactive) and COUNTER is a synchronization counter (0-255, wraps).

**Total size**: Maximum 1024 bytes (including all fields)

**Mutual Exclusivity**: When a START command is received for a plugin, the system automatically stops any other running plugin before activating the target plugin.

### Effect Strobe Plugin Usage

The Effect Strobe plugin automatically starts its effect when activated. No commands are needed - simply activate the plugin to start the strobe effect.

**Default Parameters:**
- On color: RGB(255, 255, 255) - white
- Off color: RGB(0, 0, 0) - black
- Duration on: 100ms
- Duration off: 100ms
- Repeat: Infinite (runs until plugin deactivated)

### Effect Fade Plugin Usage

The Effect Fade plugin automatically starts its effect when activated. No commands are needed - simply activate the plugin to start the fade effect.

**Default Parameters:**
- On color: RGB(255, 255, 255) - white
- Off color: RGB(0, 0, 0) - black
- Fade in: 500ms (smooth fade from on to off)
- Fade out: 500ms (smooth fade from off to on)
- Hold duration: 200ms (hold at off color before fading back)
- Repeat: Infinite (runs until plugin deactivated)

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
- Beat commands update pointer position for synchronization (includes pointer and counter)

## Web Interface

### Accessing the Web Interface

The embedded webserver on the root node provides a simple plugin control interface accessible at the root node's IP address. The external webserver (Node.js) may provide additional features for plugins with custom HTML interfaces.

### Embedded Webserver Interface

The embedded webserver serves a simple static HTML page that provides basic plugin control functionality:

**Features:**
- **Plugin Selection Dropdown**: Lists all available plugins
- **Active Plugin Display**: Shows which plugin is currently active
- **Control Buttons**:
  - **Play**: Activates the selected plugin (equivalent to START)
  - **Pause**: Temporarily pauses the plugin's operation
  - **Rewind**: Resets the plugin's internal state
  - **Stop**: Stops and deactivates the plugin (resets state and deactivates)
- **Status Messages**: Shows success/error feedback for operations
- **Auto-refresh**: Active plugin status is polled every 2 seconds

**Using the Interface:**
1. Connect to the root node's IP address in your web browser
2. Select a plugin from the dropdown menu
3. Click **Play** to activate the plugin
4. Use **Pause** to pause, **Rewind** to reset the plugin state, or **Stop** to deactivate the plugin
5. The active plugin indicator updates automatically

**Note**: The embedded webserver provides a simple control interface. For plugins with custom HTML interfaces, use the external webserver which serves plugin-specific HTML files.

### External Webserver Plugin Interfaces

Plugins with custom HTML files are served by the external webserver (Node.js). These interfaces provide full-featured control and configuration for plugins that require complex UIs.

**Accessing Plugin Interfaces:**
- Plugin HTML files are served at `/plugins/<plugin-name>/<plugin-name>.html` or `/plugins/<plugin-name>/index.html`
- JavaScript and CSS files are served from `/plugins/<plugin-name>/js/` and `/plugins/<plugin-name>/css/` respectively

**Sequence Plugin Interface:**
The Sequence plugin provides a full web interface accessible via the external webserver:
- **Grid Editor**: 16x16 color grid for sequence design
- **Tempo Controls**: Adjust playback speed
- **Playback Controls**: Start, stop, and reset
- **Export/Import**: CSV file support

**Effect Strobe and Effect Fade Plugins:**
The Effect Strobe and Effect Fade plugins do not currently provide web interfaces. Effects are automatically started when the plugins are activated via the plugin control API or web interface.

### Plugin Web UI

Plugins can provide dynamic web interfaces that are loaded on-demand from the ESP32 root node. These interfaces are served via the plugin web UI system, which enables plugins to provide HTML, CSS, and JavaScript content at runtime.

**Accessing Plugin Web UI:**
- Plugin web UIs are loaded automatically when a plugin is selected in the web interface
- The system fetches HTML/CSS/JS bundles from the root node via `GET /api/plugin/<plugin-name>/bundle`
- Content is injected into the DOM dynamically

**Bundle Endpoint:**
- **Endpoint**: `GET /api/plugin/<plugin-name>/bundle`
- **Response**: JSON object `{"html": "...", "js": "...", "css": "..."}`
- **Content-Type**: `application/json; charset=utf-8`
- NULL callbacks are omitted from the JSON response

**Data Endpoint:**
- **Endpoint**: `POST /api/plugin/<plugin-name>/data`
- **Content-Type**: `application/octet-stream`
- **Body**: Raw bytes (e.g., `[0xFF, 0x00, 0x00]` for RGB values)
- **Max Payload**: 512 bytes recommended
- **Response**: `200 OK` on success

**Usage Examples:**
- **RGB Effect Plugin**: Provides RGB sliders for direct color control
  - Load bundle: `PluginWebUI.loadPluginBundle('rgb_effect')`
  - Send RGB values: `PluginWebUI.sendPluginData('rgb_effect', PluginWebUI.encodeRGB(255, 0, 0))`

**JavaScript API:**
- `PluginWebUI.loadPluginBundle(pluginName)`: Loads and injects plugin UI bundle
- `PluginWebUI.sendPluginData(pluginName, data)`: Sends raw bytes to plugin
- `PluginWebUI.encodeRGB(r, g, b)`: Encodes RGB values as 3 bytes
- `PluginWebUI.encodeUint8(value)`: Encodes single byte value
- `PluginWebUI.encodeUint16(value)`: Encodes 16-bit value (little-endian)

**For Developers:**
See the [Plugin Web UI Integration Guide](../dev-guides/plugin-web-ui-integration.md) for detailed information on implementing web UI support in plugins.

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
- `POST /api/plugin/stop` - Stop plugin (calls on_stop callback if available, then deactivates)
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

#### Effect Strobe and Effect Fade Plugin API

Effect plugins are controlled via the general plugin activation/deactivation API:

**Activate Effect Strobe:**
```
POST /api/plugin/activate
{"name": "effect_strobe"}
```

**Activate Effect Fade:**
```
POST /api/plugin/activate
{"name": "effect_fade"}
```

**Deactivate Effect:**
```
POST /api/plugin/deactivate
{"name": "effect_strobe"}  /* or "effect_fade" */
```

Effects automatically start when activated and stop when deactivated. No additional commands are needed.

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
- Embedded webserver page doesn't load
- Plugin list is empty
- Control buttons don't work

**Solutions:**
- Verify root node has obtained an IP address
- Check that webserver is running (check serial logs)
- Verify you're accessing the root node (not a child node)
- Check browser console for errors
- For external webserver plugin interfaces: Verify external server is running and plugin files are copied

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
- Effect doesn't start when plugin is activated
- Effect stops unexpectedly

**Solutions:**
- Verify plugin is activated (check active plugin status)
- Check that only one plugin is active at a time (plugin exclusivity)
- Verify LED hardware is connected
- Check effect timer is running (review firmware logs)
- Verify plugin registered successfully during initialization

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
