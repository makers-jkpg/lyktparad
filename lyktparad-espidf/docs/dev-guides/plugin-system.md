# Plugin System - Developer Guide

**Last Updated:** 2025-01-27

**Note**: Basic UI feature added for plugins without custom HTML files. Plugin protocol redesigned with plugin ID prefix (0x0B-0xEE). API endpoints added for plugin control (stop, pause, reset).

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Creating a Plugin](#creating-a-plugin)
4. [Plugin Interface](#plugin-interface)
5. [Command Handling](#command-handling)
6. [Timer Callbacks](#timer-callbacks)
7. [Plugin Registration](#plugin-registration)
8. [Web Integration](#web-integration)
9. [Build System Integration](#build-system-integration)
10. [Best Practices](#best-practices)
11. [Examples](#examples)

## Overview

### Purpose

The Plugin System provides a modular architecture for extending the mesh network firmware with new visualization modes and features. Plugins can handle mesh commands, provide web interfaces, and integrate seamlessly with the existing system.

### Key Features

- **Automatic Plugin ID Assignment**: Plugins receive unique plugin IDs automatically (0x0B-0xEE, 228 plugins maximum)
- **Stateless Protocol**: Commands are self-contained with plugin ID prefix, making routing stateless
- **Command Routing**: Mesh commands are automatically routed to the appropriate plugin based on plugin ID
- **Optional Callbacks**: Plugins can provide timer callbacks, initialization, activation/deactivation, and state queries
- **Web Integration**: Plugins can include HTML, JavaScript, and CSS files for web interfaces
- **Build System Integration**: Plugin files are automatically discovered and compiled

### Design Principles

**Modularity**: Each plugin is self-contained with its own source files, headers, and web assets.

**Automatic Discovery**: Plugins are automatically discovered by the build system - no manual CMakeLists.txt modification required.

**Command Isolation**: Each plugin handles its own command range, preventing conflicts.

**Optional Features**: Plugins only need to implement required callbacks; optional callbacks can be NULL.

## Architecture

### System Flow

```
┌─────────────────────────────────────────────────────────┐
│  Mesh Command Handler (mesh_child.c, mesh_root.c)      │
│                                                          │
│  - Receives mesh commands                              │
│  - Routes core commands (0x01-0x0A) directly         │
│  - Routes plugin commands (0x0B-0xEE) to plugin system│
│  - Protocol: [PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N]│
└──────────────────┬──────────────────────────────────────┘
                   │
                   │ plugin_system_handle_command()
                   │ (extracts plugin ID from data[0])
                   ▼
┌─────────────────────────────────────────────────────────┐
│  Plugin System (plugin_system.c)                        │
│                                                          │
│  ┌────────────────────────────────────────────────────┐ │
│  │  Plugin Registry                                   │ │
│  │  - Array of registered plugins                     │ │
│  │  - Each plugin has:                                │ │
│  │    - Unique name                                   │ │
│  │    - Assigned plugin ID (0x0B-0xEE)              │ │
│  │    - Callback functions                           │ │
│  └───────────────┬────────────────────────────────────┘ │
│                   │                                       │
│                   ▼                                       │
│  ┌────────────────────────────────────────────────────┐ │
│  │  Command Router                                    │ │
│  │  - Extract plugin ID from data[0]                 │ │
│  │  - Look up plugin by plugin ID                     │ │
│  │  - Call plugin's command_handler with remaining   │ │
│  │    data (data[1] onwards, len-1)                    │ │
│  │  - Return result to caller                         │ │
│  └────────────────────────────────────────────────────┘ │
└──────────────────┼──────────────────────────────────────┘
                   │
                   │ command_handler callback
                   │ (receives: [CMD:1] [LENGTH:2?] [DATA:N])
                   ▼
┌─────────────────────────────────────────────────────────┐
│  Plugin Implementation (e.g., effects, sequence)      │
│                                                          │
│  - command_handler: Handle plugin commands             │
│  - on_start: START command callback (optional)          │
│  - on_pause: PAUSE command callback (optional)          │
│  - on_reset: RESET command callback (optional)          │
│  - on_activate: Activation callback (optional)         │
│  - on_deactivate: Deactivation callback (optional)      │
│  - timer_callback: Periodic updates (optional)          │
│  - init: Plugin initialization (optional)               │
│  - deinit: Plugin cleanup (optional)                    │
└─────────────────────────────────────────────────────────┘
```

### Command ID Allocation

- **0x01-0x0A**: Core functionality commands (heartbeat, RGB, light control)
- **0x0B-0xEE**: Plugin IDs (automatic assignment, 228 plugins maximum)
- **0xEF-0xFF**: Internal mesh use (OTA, web server IP broadcast, etc.)

Plugin IDs are assigned sequentially starting from 0x0B when plugins register. Registration order is deterministic (fixed in `plugins.h`), ensuring consistency across all nodes with the same firmware version.

### Plugin Protocol Format

The plugin protocol uses a self-contained format where the plugin ID is included in every command:

```
[PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N]
```

- **PLUGIN_ID** (1 byte): Plugin identifier (0x0B-0xEE)
- **CMD** (1 byte): Command type:
  - `PLUGIN_CMD_START` (0x01): Start plugin playback
  - `PLUGIN_CMD_PAUSE` (0x02): Pause plugin playback
  - `PLUGIN_CMD_RESET` (0x03): Reset plugin state
  - `PLUGIN_CMD_DATA` (0x04): Plugin-specific data command (variable length)
- **LENGTH** (2 bytes, optional): Length prefix for variable-length data (network byte order, only for DATA commands)
- **DATA** (N bytes, optional): Command-specific data
- **Total size**: Maximum 1024 bytes (including all fields)

**Fixed-size commands**:
- START, PAUSE, RESET: 2 bytes total (PLUGIN_ID + CMD)

**Variable-size commands** (DATA): 4 bytes header (PLUGIN_ID + CMD + LENGTH) + data

**Note**: Sequence synchronization is handled via `MESH_CMD_HEARTBEAT` (core mesh command), not via plugin BEAT commands. The heartbeat format is `[MESH_CMD_HEARTBEAT:1] [POINTER:1] [COUNTER:1]` (3 bytes total), where POINTER is the sequence pointer (0-255, 0 when sequence inactive) and COUNTER is a synchronization counter (0-255, wraps).

**Mutual Exclusivity**: When a START command is received for a plugin, the system automatically stops any other running plugin before activating the target plugin.

## Creating a Plugin

### Directory Structure

Create a new plugin directory under `src/plugins/`:

```
src/plugins/
└── my_plugin/
    ├── my_plugin_plugin.h      # Plugin header file
    ├── my_plugin_plugin.c       # Plugin implementation
    ├── my_plugin.html           # Web interface (optional)
    ├── js/
    │   └── my_plugin.js         # JavaScript (optional)
    └── css/
        └── my_plugin.css        # CSS styles (optional)
```

### File Naming Conventions

- **Header file**: `<plugin-name>_plugin.h`
- **Source file**: `<plugin-name>_plugin.c`
- **HTML file**: `<plugin-name>.html` or `index.html`
- **JavaScript**: `js/<plugin-name>.js`
- **CSS**: `css/<plugin-name>.css`

Plugin names should be lowercase with underscores (e.g., `my_plugin`, `effects`, `sequence`).

### Required Files

At minimum, a plugin must have:
- `<plugin-name>_plugin.h` - Header file with plugin interface
- `<plugin-name>_plugin.c` - Implementation file with plugin registration

### Optional Files

Plugins can optionally include:
- HTML file for web interface
- JavaScript file for web functionality
- CSS file for styling

## Plugin Interface

### Header File Structure

Your plugin header file should include:

```c
#ifndef __MY_PLUGIN_PLUGIN_H__
#define __MY_PLUGIN_PLUGIN_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Plugin-specific constants and types */

/**
 * @brief Register the plugin with the plugin system
 */
void my_plugin_plugin_register(void);

/* Plugin-specific public functions (if any) */

#endif /* __MY_PLUGIN_PLUGIN_H__ */
```

### Implementation File Structure

Your plugin implementation should include:

```c
#include "my_plugin_plugin.h"
#include "plugin_system.h"
#include "esp_log.h"

static const char *TAG = "my_plugin";

/* Plugin state variables */

/* Forward declarations */

/* Command handler implementation */

/* Optional callback implementations */

/* Plugin registration function */
void my_plugin_plugin_register(void)
{
    plugin_info_t info = {
        .name = "my_plugin",
        .command_id = 0,  /* Will be assigned automatically as plugin ID */
        .callbacks = {
            .command_handler = my_plugin_command_handler,  /* Required */
            .on_start = my_plugin_on_start,  /* Optional */
            .on_pause = my_plugin_on_pause,  /* Optional */
            .on_reset = my_plugin_on_reset,  /* Optional */
            .on_activate = my_plugin_on_activate,  /* Optional */
            .on_deactivate = my_plugin_on_deactivate,  /* Optional */
            .timer_callback = my_plugin_timer_callback,  /* Optional */
            .init = my_plugin_init,  /* Optional */
            .deinit = my_plugin_deinit,  /* Optional */
            .is_active = my_plugin_is_active,  /* Optional */
            .get_state = my_plugin_get_state,  /* Optional - for plugin query interface */
            .execute_operation = my_plugin_execute_operation,  /* Optional - for plugin query interface */
            .get_helper = my_plugin_get_helper,  /* Optional - for plugin query interface */
        },
        .user_data = NULL,  /* Optional */
    };

    uint8_t assigned_plugin_id;
    esp_err_t err = plugin_register(&info, &assigned_plugin_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register plugin: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Plugin registered with plugin ID 0x%02X", assigned_plugin_id);
        /* Store assigned_plugin_id if needed for command broadcasting */
    }
}
```

## Command Handling

### Command Handler Signature

```c
esp_err_t my_plugin_command_handler(uint8_t *data, uint16_t len);
```

**Parameters:**
- `data`: Pointer to command data (command byte at `data[0]`, plugin ID already extracted by system)
- `len`: Length of command data in bytes (does not include plugin ID)

**Returns:**
- `ESP_OK` on success
- Error code on failure

**Important**: The plugin system extracts the plugin ID from the first byte of the received command and routes to your plugin. Your command handler receives only the remaining data starting from the command byte (`data[0]` = command byte, `data[1]` onwards = command data).

### Command Handler Implementation

The command handler is called when a mesh command with your plugin's plugin ID is received. The plugin system has already:
1. Extracted the plugin ID from `data[0]` of the original command
2. Looked up your plugin by plugin ID
3. Passed the remaining data (starting from command byte) to your handler

You should:

1. Validate the data length is appropriate
2. Parse the command byte from `data[0]`
3. Parse any additional command data
4. Execute the command logic
5. Return appropriate error codes

Example:

```c
static esp_err_t my_plugin_command_handler(uint8_t *data, uint16_t len)
{
    /* Validate data */
    if (data == NULL || len < 1) {
        ESP_LOGE(TAG, "Invalid command data");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse command byte (data[0] is the command byte, not plugin ID) */
    uint8_t command_byte = data[0];

    /* Execute command logic */
    switch (command_byte) {
        case PLUGIN_CMD_DATA:
            /* Handle DATA command with variable-length payload */
            if (len < 3) {
                ESP_LOGE(TAG, "DATA command too short: len=%d", len);
                return ESP_ERR_INVALID_ARG;
            }
            /* Extract length prefix (network byte order) */
            uint16_t data_len = (data[1] << 8) | data[2];
            if (len < 3 + data_len) {
                ESP_LOGE(TAG, "DATA command incomplete: expected %d bytes, got %d", 3 + data_len, len);
                return ESP_ERR_INVALID_SIZE;
            }
            /* Process data starting from data[3] */
            return my_plugin_handle_data(&data[3], data_len);

        default:
            ESP_LOGE(TAG, "Unknown command byte: 0x%02X", command_byte);
            return ESP_ERR_NOT_SUPPORTED;
    }
}
```

### Command Data Format

The command handler receives data in the format `[CMD:1] [LENGTH:2?] [DATA:N]`:

- **Note**: The `command_handler` callback is only used for `PLUGIN_CMD_DATA` commands. Fixed-size commands (START, PAUSE, RESET) are handled by their dedicated callbacks (`on_start`, `on_pause`, `on_reset`) and do not go through `command_handler`.
- **For variable-size commands** (DATA): `len >= 3`, `data[0]` = `PLUGIN_CMD_DATA` (0x04), `data[1-2]` = length prefix (network byte order), `data[3]` onwards = actual data

**Example DATA command:**
If a DATA command is sent with:
- Plugin ID: 0x0B (your plugin's assigned ID)
- Command byte: `PLUGIN_CMD_DATA` (0x04)
- Length: 0x0005 (5 bytes)
- Data: 0x01, 0x02, 0x03, 0x04, 0x05

Then your command handler receives:
- `data[0]` = 0x04 (`PLUGIN_CMD_DATA`)
- `data[1]` = 0x00 (length high byte)
- `data[2]` = 0x05 (length low byte)
- `data[3]` = 0x01
- `data[4]` = 0x02
- `data[5]` = 0x03
- `data[6]` = 0x04
- `data[7]` = 0x05
- `len` = 8

## Timer Callbacks

### Timer Callback Signature

```c
void my_plugin_timer_callback(void *arg);
```

**Parameters:**
- `arg`: Optional argument passed when timer was created

### Timer Callback Implementation

Timer callbacks are called periodically when your plugin's timer fires. Use ESP-IDF's `esp_timer` API to create and manage timers.

Example:

```c
static esp_timer_handle_t my_plugin_timer = NULL;

static void my_plugin_timer_callback(void *arg)
{
    (void)arg;

    /* Periodic update logic */
    ESP_LOGD(TAG, "Timer callback fired");

    /* Update plugin state */
    /* Call LED functions, etc. */
}

static esp_err_t my_plugin_start_timer(uint32_t interval_ms)
{
    if (my_plugin_timer != NULL) {
        return ESP_ERR_INVALID_STATE;  /* Timer already running */
    }

    esp_timer_create_args_t timer_args = {
        .callback = &my_plugin_timer_callback,
        .arg = NULL,
        .name = "my_plugin_timer"
    };

    esp_err_t err = esp_timer_create(&timer_args, &my_plugin_timer);
    if (err != ESP_OK) {
        return err;
    }

    uint64_t interval_us = (uint64_t)interval_ms * 1000;
    return esp_timer_start_periodic(my_plugin_timer, interval_us);
}

static void my_plugin_stop_timer(void)
{
    if (my_plugin_timer != NULL) {
        esp_timer_stop(my_plugin_timer);
        esp_timer_delete(my_plugin_timer);
        my_plugin_timer = NULL;
    }
}
```

**Note**: The timer callback in `plugin_callback_t` is optional. If your plugin doesn't need periodic updates, set it to `NULL`.

## Plugin Query Interface

The plugin system provides a query interface that allows core files to interact with plugins without direct function calls. This promotes encapsulation and allows plugins to control their own state and operations.

### Query Interface Callbacks

Plugins can optionally provide three query interface callbacks:

**`get_state`**: Query plugin-specific state (pointer, active status, rhythm, length, etc.)
```c
esp_err_t (*get_state)(uint32_t query_type, void *result);
```

**`execute_operation`**: Execute plugin operations (store, start, pause, reset, etc.)
```c
esp_err_t (*execute_operation)(uint32_t operation_type, void *params);
```

**`get_helper`**: Get helper function results (size calculations, etc.)
```c
esp_err_t (*get_helper)(uint32_t helper_type, void *params, void *result);
```

### Using the Query Interface

Core files can use these functions to interact with plugins:

- `plugin_query_state(plugin_name, query_type, result)` - Query plugin state
- `plugin_execute_operation(plugin_name, operation_type, params)` - Execute plugin operation
- `plugin_get_helper(plugin_name, helper_type, params, result)` - Get helper result

**Example**: The sequence plugin uses this interface to allow core files to query the sequence pointer, execute operations like storing sequence data, and get helper calculations like payload sizes, all without exposing direct function calls.

**Note**: These callbacks are optional. If your plugin doesn't need to expose state or operations to core files, set them to `NULL`.

## Plugin Registration

### Registration Function

Each plugin must provide a registration function that creates a `plugin_info_t` structure and calls `plugin_register()`.

```c
void my_plugin_plugin_register(void)
{
    plugin_info_t info = {
        .name = "my_plugin",
        .command_id = 0,  /* Will be assigned automatically as plugin ID */
        .callbacks = {
            .command_handler = my_plugin_command_handler,  /* Required */
            .on_start = NULL,  /* Optional - called when START command received */
            .on_pause = NULL,  /* Optional - called when PAUSE command received */
            .on_reset = NULL,  /* Optional - called when RESET command received */
            .on_activate = NULL,  /* Optional - called when plugin activated */
            .on_deactivate = NULL,  /* Optional - called when plugin deactivated */
            .timer_callback = my_plugin_timer_callback,     /* Optional */
            .init = my_plugin_init,                        /* Optional */
            .deinit = my_plugin_deinit,                    /* Optional */
            .is_active = my_plugin_is_active,              /* Optional */
        },
        .user_data = NULL,  /* Optional */
    };

    uint8_t assigned_plugin_id;
    esp_err_t err = plugin_register(&info, &assigned_plugin_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register plugin: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Plugin registered with plugin ID 0x%02X", assigned_plugin_id);
        /* Store assigned_plugin_id if needed for command broadcasting */
    }
}
```

### Registration Requirements

- **Plugin name**: Must be unique, non-NULL, and non-empty
- **Command handler**: Must be non-NULL (required callback)
- **Other callbacks**: May be NULL (optional)

### Registration Errors

`plugin_register()` can return:
- `ESP_OK`: Registration successful
- `ESP_ERR_INVALID_ARG`: Invalid parameters (NULL name, NULL command_handler, etc.)
- `ESP_ERR_INVALID_STATE`: Plugin with same name already registered
- `ESP_ERR_NO_MEM`: Plugin registry full or plugin ID range exhausted (0x0B-0xEE)

### Plugin ID Consistency

Plugin IDs are assigned deterministically based on registration order in `plugins.h`. This ensures:
- All nodes with the same firmware version have identical plugin IDs
- Plugin IDs are consistent across the mesh network
- Commands can be routed correctly without activation state synchronization

**Important**: If you change the registration order in `plugins.h`, plugin IDs will change, potentially breaking compatibility with existing commands. Always maintain a consistent registration order across firmware versions.

### Adding Plugin to plugins.h

After creating your plugin, add it to `src/plugins/plugins.h`:

```c
#include "plugins/my_plugin/my_plugin_plugin.h"

static inline void plugins_init(void)
{
    effect_strobe_plugin_register();
    effect_fade_plugin_register();
    sequence_plugin_register();
    my_plugin_plugin_register();  /* Add your plugin */
}
```

## Web Integration

### Embedded Webserver Interface

The embedded webserver on the root node serves a simple static HTML page that provides basic plugin control. This interface includes:
- Plugin selection dropdown
- Play, Pause, and Rewind control buttons
- Active plugin status display
- Status message feedback

**Note**: The embedded webserver does not serve plugin-specific HTML files. For plugins with custom interfaces, use the external webserver.

### External Webserver Plugin Files

Plugins can include HTML, JavaScript, and CSS files that are served by the external webserver (Node.js). These files are automatically copied to the external webserver during the build process.

**File Locations:**
- HTML: `src/plugins/<plugin-name>/<plugin-name>.html` or `index.html`
- JavaScript: `src/plugins/<plugin-name>/js/<plugin-name>.js`
- CSS: `src/plugins/<plugin-name>/css/<plugin-name>.css`

**Served URLs:**
- HTML: `/plugins/<plugin-name>/<plugin-name>.html` or `/plugins/<plugin-name>/index.html`
- JavaScript: `/plugins/<plugin-name>/js/<plugin-name>.js`
- CSS: `/plugins/<plugin-name>/css/<plugin-name>.css`

**Example Plugin HTML:**

```html
<!DOCTYPE html>
<html>
<head>
    <title>My Plugin</title>
    <link rel="stylesheet" href="/plugins/my_plugin/css/my_plugin.css">
</head>
<body>
    <h2>My Plugin</h2>
    <button id="my-plugin-button">Do Something</button>
    <div id="my-plugin-content">
        <!-- Your plugin content here -->
    </div>
    <script src="/plugins/my_plugin/js/my_plugin.js"></script>
</body>
</html>
```

**Note**: Plugin HTML files for the external webserver should be complete HTML pages, not fragments. They are served as standalone pages.

### JavaScript Files

JavaScript files are automatically included in the web interface. Use namespacing to avoid conflicts with other plugins.

Example `js/my_plugin.js`:

```javascript
(function() {
    'use strict';

    const MyPlugin = {
        init: function() {
            const button = document.getElementById('my-plugin-button');
            if (button) {
                button.addEventListener('click', this.handleButtonClick.bind(this));
            }
        },

        handleButtonClick: function() {
            // Send command to plugin via API
            fetch('/api/my-plugin/do-something', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action: 'do_something' })
            });
        }
    };

    // Initialize when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', MyPlugin.init.bind(MyPlugin));
    } else {
        MyPlugin.init();
    }
})();
```

### CSS Files

CSS files are automatically included. Use plugin-specific class names to avoid conflicts.

**Important:** Don't use `.plugin-section` in your CSS - it's managed by the system. Instead, use your plugin-specific classes or IDs.

Example `css/my_plugin.css`:

```css
/* Use plugin-specific classes, not .plugin-section */
#my-plugin-content {
    margin: 20px 0;
    padding: 15px;
    border: 1px solid #ccc;
}

#my-plugin-button {
    padding: 10px 20px;
    background-color: #007bff;
    color: white;
    border: none;
    cursor: pointer;
}

/* You can target your plugin's section specifically if needed */
.plugin-my_plugin {
    /* Styles for your plugin's section */
}
```

### Build System Integration

The build system automatically:
- Discovers plugin HTML/JS/CSS files
- Copies them to external webserver directory during build
- Does NOT embed plugin HTML/JS/CSS files in firmware (only source files are compiled)

**Note**: The embedded webserver uses a simple static HTML page for plugin control. Plugin-specific HTML files are only served by the external webserver.

No manual build configuration is required.

### HTTP API Endpoints

The plugin system provides HTTP API endpoints for plugin control. These endpoints are available on both embedded and external webservers.

**Plugin Activation/Deactivation:**
- `POST /api/plugin/activate` - Activate a plugin by name
  - Request body: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}` or `{"success": false, "error": "error_message"}`
- `POST /api/plugin/deactivate` - Deactivate a plugin by name
  - Request body: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}` or `{"success": false, "error": "error_message"}`
- `GET /api/plugin/active` - Get currently active plugin
  - Response: `{"plugin": "plugin_name"}` or `{"plugin": null}` if none active

**Plugin Control Commands:**
- `POST /api/plugin/stop` - Stop plugin (calls `on_pause` callback if available, then deactivates)
  - Request body: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}` or `{"success": false, "error": "error_message"}`
- `POST /api/plugin/pause` - Pause plugin playback
  - Request body: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}` or `{"success": false, "error": "error_message"}`
- `POST /api/plugin/reset` - Reset plugin state
  - Request body: `{"name": "plugin_name"}`
  - Response: `{"success": true, "plugin": "plugin_name"}` or `{"success": false, "error": "error_message"}`

**Plugin Discovery:**
- `GET /api/plugins` - Get list of all registered plugins
  - Response: `{"plugins": ["plugin1", "plugin2", ...]}`

All endpoints return JSON responses and include CORS headers for cross-origin requests.

## Build System Integration

### Automatic Discovery

The build system automatically:
1. Scans `src/plugins/` directory
2. Finds plugin subdirectories
3. Validates plugin structure (requires `.h` and `.c` files)
4. Collects source files for compilation
5. Collects web files (HTML/JS/CSS) for embedding

### Source File Compilation

Plugin source files (`.c` and `.h`) are automatically:
- Added to the build
- Compiled with the rest of the firmware
- Linked into the final binary

### External Webserver File Copying

Plugin web files (HTML/JS/CSS) are automatically:
- Copied to external webserver directory during build
- NOT embedded in firmware (only source files are compiled)
- Served by the external webserver at `/plugins/<plugin-name>/`
- NOT accessible from the embedded webserver (which only serves a simple plugin control page)

### No Manual Configuration

You don't need to modify `CMakeLists.txt` or any build configuration files. The build system handles everything automatically.

## Best Practices

### Plugin Naming

- Use lowercase with underscores: `my_plugin`, `effects`, `sequence`
- Keep names descriptive and unique
- Avoid generic names that might conflict

### Command Handling

- The plugin system handles plugin ID extraction and routing - your command handler receives data starting from the command byte
- Always validate data length before accessing data
- For DATA commands, extract length prefix from `data[1-2]` (network byte order)
- Return appropriate error codes
- Log errors for debugging
- Use `PLUGIN_CMD_*` constants from `mesh_commands.h` for command bytes

### State Management

- Use static variables for plugin state
- Initialize state in `init` callback (if provided)
- Clean up state in `deinit` callback (if provided)
- Handle errors gracefully

### Timer Management

- Create timers when needed
- Stop and delete timers when done
- Handle timer creation failures
- Don't assume timers are always valid

### Memory Management

- Use static allocation when possible
- Free dynamically allocated memory in `deinit` callback
- Avoid memory leaks
- Check for NULL pointers

### Error Handling

- Return appropriate ESP error codes
- Log errors with ESP_LOGE
- Log debug information with ESP_LOGD
- Handle edge cases gracefully

### Web Integration

- Use namespaced CSS classes and JavaScript variables
- Keep HTML fragments self-contained
- Test web interface in both embedded and external webservers
- Ensure HTML is valid

### Thread Safety

- Plugin callbacks may be called from different threads
- Use appropriate synchronization if needed
- Be aware of mesh event handler context

### RGB LED Control

**Plugin Exclusivity**: RGB LEDs are controlled exclusively by plugins when a plugin is active. This applies to both root nodes and child nodes, ensuring unified behavior across all mesh nodes.

**Recommended Function**:
- `plugin_set_rgb(r, g, b)`: Unified function that automatically controls all available LED systems
  - Always controls Neopixel/WS2812 LEDs (works on root and child nodes)
  - Conditionally controls common-cathode RGB LEDs if `RGB_ENABLE` is defined
  - Eliminates the need for conditional compilation in plugins
  - Handles type conversion internally
  - This is the recommended function for all plugin LED control

**Advanced Functions** (for fine-grained control):
- `plugin_light_set_rgb(r, g, b)`: Control Neopixel/WS2812 LEDs only (works on root and child nodes)
- `plugin_set_rgb_led(r, g, b)`: Control common-cathode RGB LEDs only (works on root and child nodes, requires `RGB_ENABLE`)

**Root Node Behavior**:
- Root node RGB LEDs respond to plugin control calls exactly like child nodes
- Root node heartbeat handler skips LED control when a plugin is active
- Root node RGB command handler skips LED control when a plugin is active
- Root node and child nodes have identical RGB LED behavior for the same plugin

**Usage in Plugins** (Recommended):
```c
/* In timer callback or command handler - unified function handles all LED types */
esp_err_t err = plugin_set_rgb(255, 0, 0);  /* Set to red on all available LED systems */
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set LED: 0x%x", err);
}
```

**Usage in Plugins** (Advanced - for fine-grained control):
```c
/* Only control Neopixel LEDs */
esp_err_t err = plugin_light_set_rgb(255, 0, 0);  /* Set to red */
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set LED: 0x%x", err);
}

/* Only control common-cathode RGB LED (if RGB_ENABLE is defined) */
#ifdef RGB_ENABLE
plugin_set_rgb_led(255, 0, 0);  /* Set to red */
#endif
```

**Important**: All LED control functions check if a plugin is active before allowing LED control. Only the active plugin can control LEDs. If no plugin is active, these functions return `ESP_ERR_INVALID_STATE`.

**Backward Compatibility**: The individual functions (`plugin_light_set_rgb`, `plugin_set_rgb_led`) remain available for advanced use cases, but new plugins should use the unified `plugin_set_rgb()` function.

## Examples

### Minimal Plugin

A minimal plugin that only handles commands:

```c
/* my_plugin_plugin.h */
#ifndef __MY_PLUGIN_PLUGIN_H__
#define __MY_PLUGIN_PLUGIN_H__

#include "esp_err.h"

void my_plugin_plugin_register(void);

#endif

/* my_plugin_plugin.c */
#include "my_plugin_plugin.h"
#include "plugin_system.h"
#include "esp_log.h"

static const char *TAG = "my_plugin";

static esp_err_t my_plugin_command_handler(uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 1) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Command received: cmd=0x%02X, len=%d", data[0], len);
    return ESP_OK;
}

void my_plugin_plugin_register(void)
{
    plugin_info_t info = {
        .name = "my_plugin",
        .command_id = 0,  /* Assigned automatically */
        .callbacks = {
            .command_handler = my_plugin_command_handler,
            .on_start = NULL,
            .on_pause = NULL,
            .on_reset = NULL,
            .on_activate = NULL,
            .on_deactivate = NULL,
            .timer_callback = NULL,
            .init = NULL,
            .deinit = NULL,
            .is_active = NULL,
            .get_state = NULL,
            .execute_operation = NULL,
            .get_helper = NULL,
        },
        .user_data = NULL,
    };

    uint8_t assigned_plugin_id;
    plugin_register(&info, &assigned_plugin_id);
}
```

### Plugin with Timer

A plugin that uses a timer for periodic updates:

```c
static esp_timer_handle_t my_plugin_timer = NULL;
static bool my_plugin_active = false;

static void my_plugin_timer_callback(void *arg)
{
    (void)arg;
    if (my_plugin_active) {
        /* Periodic update */
        ESP_LOGD(TAG, "Timer callback");
    }
}

static esp_err_t my_plugin_init(void)
{
    esp_timer_create_args_t timer_args = {
        .callback = &my_plugin_timer_callback,
        .arg = NULL,
        .name = "my_plugin_timer"
    };

    esp_err_t err = esp_timer_create(&timer_args, &my_plugin_timer);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t my_plugin_deinit(void)
{
    if (my_plugin_timer != NULL) {
        esp_timer_stop(my_plugin_timer);
        esp_timer_delete(my_plugin_timer);
        my_plugin_timer = NULL;
    }
    return ESP_OK;
}

static bool my_plugin_is_active(void)
{
    return my_plugin_active;
}

void my_plugin_plugin_register(void)
{
    plugin_info_t info = {
        .name = "my_plugin",
        .command_id = 0,
        .callbacks = {
            .command_handler = my_plugin_command_handler,
            .on_start = NULL,
            .on_pause = NULL,
            .on_reset = NULL,
            .on_activate = NULL,
            .on_deactivate = NULL,
            .timer_callback = my_plugin_timer_callback,
            .init = my_plugin_init,
            .deinit = my_plugin_deinit,
            .is_active = my_plugin_is_active,
            .get_state = NULL,
            .execute_operation = NULL,
            .get_helper = NULL,
        },
        .user_data = NULL,
    };

    uint8_t assigned_cmd_id;
    plugin_register(&info, &assigned_cmd_id);
}
```

### Reference Implementations

For complete examples, see:
- `src/plugins/effects/` - Effects plugin implementation
- `src/plugins/sequence/` - Sequence plugin implementation

These plugins demonstrate:
- Command handling
- Timer management
- State management
- Root vs child node logic
- Web integration

## Additional Resources

- [Plugin Build System Guide](plugin-build-system.md) - Details on build system integration
- [Plugin System Header](../include/plugin_system.h) - Complete API reference
- [Plugin System Implementation](../src/plugin_system.c) - Implementation details
