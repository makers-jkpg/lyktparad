# Plugin System - Developer Guide

**Last Updated:** 2025-01-XX

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

- **Automatic Command ID Assignment**: Plugins receive unique command IDs automatically (0x10-0xEF)
- **Command Routing**: Mesh commands are automatically routed to the appropriate plugin
- **Optional Callbacks**: Plugins can provide timer callbacks, initialization, and state queries
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
│  Mesh Command Handler (mesh_child.c)                   │
│                                                          │
│  - Receives mesh commands                              │
│  - Routes core commands (0x01-0x0F) directly           │
│  - Routes plugin commands (0x10-0xEF) to plugin system │
└──────────────────┬──────────────────────────────────────┘
                   │
                   │ plugin_system_handle_command()
                   ▼
┌─────────────────────────────────────────────────────────┐
│  Plugin System (plugin_system.c)                        │
│                                                          │
│  ┌────────────────────────────────────────────────────┐ │
│  │  Plugin Registry                                   │ │
│  │  - Array of registered plugins                     │ │
│  │  - Each plugin has:                                │ │
│  │    - Unique name                                   │ │
│  │    - Assigned command ID (0x10-0xEF)              │ │
│  │    - Callback functions                           │ │
│  └───────────────┬────────────────────────────────────┘ │
│                   │                                       │
│                   ▼                                       │
│  ┌────────────────────────────────────────────────────┐ │
│  │  Command Router                                    │ │
│  │  - Look up plugin by command ID                    │ │
│  │  - Call plugin's command_handler callback         │ │
│  │  - Return result to caller                         │ │
│  └────────────────────────────────────────────────────┘ │
└──────────────────┼──────────────────────────────────────┘
                   │
                   │ command_handler callback
                   ▼
┌─────────────────────────────────────────────────────────┐
│  Plugin Implementation (e.g., effects, sequence)      │
│                                                          │
│  - command_handler: Handle mesh commands               │
│  - timer_callback: Periodic updates (optional)         │
│  - init: Plugin initialization (optional)              │
│  - deinit: Plugin cleanup (optional)                   │
│  - is_active: Query plugin state (optional)            │
└─────────────────────────────────────────────────────────┘
```

### Command ID Allocation

- **0x01-0x0F**: Core functionality commands (heartbeat, RGB, light control)
- **0x10-0xEF**: Plugin commands (automatic assignment, 224 plugins maximum)
- **0xF0-0xFF**: Internal mesh use (OTA, etc.)

Command IDs are assigned sequentially starting from 0x10 when plugins register.

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
        .command_id = 0,  /* Will be assigned automatically */
        .callbacks = {
            .command_handler = my_plugin_command_handler,
            .timer_callback = my_plugin_timer_callback,  /* Optional */
            .init = my_plugin_init,  /* Optional */
            .deinit = my_plugin_deinit,  /* Optional */
            .is_active = my_plugin_is_active,  /* Optional */
        },
        .user_data = NULL,  /* Optional */
    };

    uint8_t assigned_cmd_id;
    esp_err_t err = plugin_register(&info, &assigned_cmd_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register plugin: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Plugin registered with command ID 0x%02X", assigned_cmd_id);
    }
}
```

## Command Handling

### Command Handler Signature

```c
esp_err_t my_plugin_command_handler(uint8_t cmd, uint8_t *data, uint16_t len);
```

**Parameters:**
- `cmd`: Command ID (should match plugin's assigned command ID)
- `data`: Pointer to command data (includes command byte at `data[0]`)
- `len`: Length of command data in bytes (includes command byte)

**Returns:**
- `ESP_OK` on success
- Error code on failure

### Command Handler Implementation

The command handler is called when a mesh command with your plugin's command ID is received. You should:

1. Validate the command ID matches your plugin's assigned ID
2. Validate the data length is appropriate
3. Parse the command data
4. Execute the command logic
5. Return appropriate error codes

Example:

```c
static esp_err_t my_plugin_command_handler(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* Validate command ID */
    if (cmd != my_plugin_cmd_id) {
        ESP_LOGE(TAG, "Invalid command ID: 0x%02X", cmd);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate data */
    if (data == NULL || len < 1) {
        ESP_LOGE(TAG, "Invalid command data");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse command data */
    uint8_t command_byte = data[0];
    if (len > 1) {
        /* Process additional data */
    }

    /* Execute command logic */
    switch (command_byte) {
        case MY_PLUGIN_CMD_DO_SOMETHING:
            return my_plugin_do_something();
        default:
            ESP_LOGE(TAG, "Unknown command: 0x%02X", command_byte);
            return ESP_ERR_NOT_SUPPORTED;
    }
}
```

### Command Data Format

Command data includes the command byte at `data[0]`. The command handler receives the full data buffer including the command byte.

For example, if a command is sent with:
- Command ID: 0x10 (your plugin's assigned ID)
- Command byte: 0x01 (specific command within your plugin)
- Additional data: 0x02, 0x03

Then `data[0]` = 0x01, `data[1]` = 0x02, `data[2]` = 0x03, and `len` = 3.

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

## Plugin Registration

### Registration Function

Each plugin must provide a registration function that creates a `plugin_info_t` structure and calls `plugin_register()`.

```c
void my_plugin_plugin_register(void)
{
    plugin_info_t info = {
        .name = "my_plugin",
        .command_id = 0,  /* Will be assigned automatically */
        .callbacks = {
            .command_handler = my_plugin_command_handler,  /* Required */
            .timer_callback = my_plugin_timer_callback,     /* Optional */
            .init = my_plugin_init,                        /* Optional */
            .deinit = my_plugin_deinit,                    /* Optional */
            .is_active = my_plugin_is_active,              /* Optional */
        },
        .user_data = NULL,  /* Optional */
    };

    uint8_t assigned_cmd_id;
    esp_err_t err = plugin_register(&info, &assigned_cmd_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register plugin: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Plugin registered with command ID 0x%02X", assigned_cmd_id);
        /* Store assigned_cmd_id if needed for command validation */
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
- `ESP_ERR_NO_MEM`: Plugin registry full or command ID range exhausted

### Adding Plugin to plugins.h

After creating your plugin, add it to `src/plugins/plugins.h`:

```c
#include "plugins/my_plugin/my_plugin_plugin.h"

static inline void plugins_init(void)
{
    effects_plugin_register();
    sequence_plugin_register();
    my_plugin_plugin_register();  /* Add your plugin */
}
```

## Web Integration

### Plugin Selection System

The web interface includes a dropdown menu at the top right that allows users to select which plugin's HTML to display. Only one plugin's HTML is visible at a time, with all other plugins hidden by default.

**Key Features:**
- Dropdown automatically lists all registered plugins
- Plugin names are formatted from directory names (e.g., "effects" → "Effects")
- Selection persists across page reloads using localStorage
- Default selection is the first plugin if no saved selection exists

**HTML Structure:**
Plugin HTML sections are automatically wrapped in `<section>` tags with:
- Class: `plugin-section plugin-{plugin_name}`
- Data attribute: `data-plugin-name="{plugin_name}"`

Example generated HTML:
```html
<section class="plugin-section plugin-effects" data-plugin-name="effects">
    <!-- Your plugin HTML content here -->
</section>
```

**CSS Classes:**
- `.plugin-section`: Hidden by default (`display: none`)
- `.plugin-section.active`: Visible when selected (`display: block`)

**JavaScript API:**
The plugin selection system is handled automatically. Your plugin JavaScript doesn't need to manage visibility - it's handled by the system.

### HTML Files

Plugins can include HTML files that are automatically integrated into the web interface. HTML files should contain HTML fragments (not full pages) that will be inserted into the main page.

**Important:** Your HTML will be wrapped in a `<section>` tag automatically, so you don't need to include one yourself unless you need nested sections.

Example `my_plugin.html`:

```html
<h2>My Plugin</h2>
<button id="my-plugin-button">Do Something</button>
<div id="my-plugin-content">
    <!-- Your plugin content here -->
</div>
```

The build system will automatically wrap this in:
```html
<section class="plugin-section plugin-my_plugin" data-plugin-name="my_plugin">
    <h2>My Plugin</h2>
    <button id="my-plugin-button">Do Something</button>
    <div id="my-plugin-content">
        <!-- Your plugin content here -->
    </div>
</section>
```

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
- Embeds them in firmware for embedded webserver
- Copies them to external webserver directory

No manual build configuration is required.

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

### Web File Embedding

Plugin web files (HTML/JS/CSS) are automatically:
- Converted to C string literals at build time
- Embedded in generated header files
- Included in the firmware build
- Copied to external webserver directory

### No Manual Configuration

You don't need to modify `CMakeLists.txt` or any build configuration files. The build system handles everything automatically.

## Best Practices

### Plugin Naming

- Use lowercase with underscores: `my_plugin`, `effects`, `sequence`
- Keep names descriptive and unique
- Avoid generic names that might conflict

### Command Handling

- Always validate command ID matches your plugin's assigned ID
- Validate data length before accessing data
- Return appropriate error codes
- Log errors for debugging

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

static esp_err_t my_plugin_command_handler(uint8_t cmd, uint8_t *data, uint16_t len)
{
    ESP_LOGI(TAG, "Command received: cmd=0x%02X, len=%d", cmd, len);
    return ESP_OK;
}

void my_plugin_plugin_register(void)
{
    plugin_info_t info = {
        .name = "my_plugin",
        .command_id = 0,
        .callbacks = {
            .command_handler = my_plugin_command_handler,
            .timer_callback = NULL,
            .init = NULL,
            .deinit = NULL,
            .is_active = NULL,
        },
        .user_data = NULL,
    };

    uint8_t assigned_cmd_id;
    plugin_register(&info, &assigned_cmd_id);
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
            .timer_callback = my_plugin_timer_callback,
            .init = my_plugin_init,
            .deinit = my_plugin_deinit,
            .is_active = my_plugin_is_active,
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
