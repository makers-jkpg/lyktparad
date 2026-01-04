# Root Node RGB LED Behavior and Status LED - Development Guide

**Last Updated:** 2025-01-15

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Unified LED Behavior](#unified-led-behavior)
4. [Root Status LED](#root-status-led)
5. [Router Status LED Removal](#router-status-led-removal)
6. [Configuration](#configuration)
7. [Implementation Details](#implementation-details)
8. [Integration Points](#integration-points)
9. [API Reference](#api-reference)
10. [Error Handling](#error-handling)

## Overview

### Purpose

The Root Node RGB LED Behavior and Status LED system provides unified LED behavior for all mesh nodes (including root) and adds a separate single-color status LED to indicate root node status. This change ensures that root nodes use the same heartbeat-based LED behavior as child nodes, while providing a dedicated status indicator for root node identification.

**Critical Principle**: The embedded web server continues to run regardless of LED behavior changes. This implementation is about LED behavior changes only, not removing web server functionality. The root node's RGB LEDs are now used for display purposes like any other mesh node, instead of indicating router connection status.

### Design Decisions

**Unified LED Behavior**: All mesh nodes (root and child) now use the same heartbeat-based LED behavior. Root nodes no longer use RGB LEDs to indicate router connection status, allowing RGB LEDs to be used for display purposes like color commands and sequences.

**Status LED Separation**: A separate single-color status LED is used exclusively to indicate root node status. This LED is ON when the node is the root node and OFF when it is a non-root node. This separation ensures clear visual identification of the root node without interfering with RGB LED functionality.

**Router Status Independence**: RGB LEDs are no longer tied to router connection status. The root node's RGB LEDs behave the same as child nodes, responding to heartbeat parity and color commands. Router connection status is tracked internally but does not affect LED behavior.

**Immediate Role Updates**: The status LED updates immediately on role changes (when a node becomes root or loses root status), providing instant visual feedback of the current mesh role.

**Configurable GPIO**: The status LED GPIO pin is configurable via `mesh_device_config.h`, allowing hardware flexibility while providing a sensible default (GPIO 3).

### Key Features

1. **Unified Heartbeat Behavior**: All nodes use even/odd heartbeat logic for RGB LEDs
2. **Root Status LED**: Separate single-color LED indicates root node status
3. **Router Status Removal**: RGB LEDs no longer indicate router connection status
4. **Color Command Support**: Root nodes respond to RGB color commands like child nodes
5. **Sequence Compatibility**: RGB LEDs work with sequence playback (sequence takes precedence)
6. **Immediate Role Updates**: Status LED updates instantly on role changes
7. **GPIO Configuration**: Status LED GPIO pin is configurable

## Architecture

### Overall LED System Architecture

```
┌─────────────────────────────────────┐
│  All Mesh Nodes (Root + Child)      │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  Unified RGB LED Behavior       │ │
│  │  - Even heartbeat: LED OFF      │ │
│  │  - Odd heartbeat: LED ON        │ │
│  │    (BLUE default or custom RGB) │ │
│  │  - Responds to color commands   │ │
│  │  - Sequence takes precedence    │ │
│  └─────────────────────────────────┘ │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│  Root Node Only                      │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  Root Status LED                │ │
│  │  (Single-color, non-RGB)        │ │
│  │  - ON when root node            │ │
│  │  - OFF when child node          │ │
│  │  - Updates on role change       │ │
│  │  - Configurable GPIO (default 3)│ │
│  └─────────────────────────────────┘ │
└─────────────────────────────────────┘
```

### LED Behavior Flow

```
Heartbeat Timer (every ~0.5 seconds)
    │
    ├─► Check Sequence Status
    │   │
    │   ├─► Sequence Active?
    │   │   └─► Skip LED change (sequence controls LED)
    │   │
    │   └─► Sequence Inactive?
    │       │
    │       ├─► Check Heartbeat Parity
    │       │   │
    │       ├─► Even Heartbeat (cnt % 2 == 0)?
    │       │   └─► RGB LED OFF
    │       │
    │       └─► Odd Heartbeat (cnt % 2 == 1)?
    │           │
    │           ├─► RGB Color Set?
    │           │   └─► RGB LED ON (custom color)
    │           │
    │           └─► No RGB Color?
    │               └─► RGB LED ON (BLUE default)

Mesh Event (Role Change)
    │
    ├─► Node Becomes Root?
    │   └─► Status LED ON
    │
    └─► Node Loses Root Status?
        └─► Status LED OFF
```

### Role Change Flow

```
Mesh Role Change Event
    │
    ├─► MESH_EVENT_LAYER_CHANGE
    │   │
    │   ├─► Node Became Root?
    │   │   └─► root_status_led_set_root(true)
    │   │
    │   └─► Node Lost Root Status?
    │       └─► root_status_led_set_root(false)
    │
    ├─► MESH_EVENT_ROOT_SWITCH_ACK
    │   │
    │   ├─► Node is Root?
    │   │   └─► root_status_led_set_root(true)
    │   │
    │   └─► Node is Not Root?
    │       └─► root_status_led_set_root(false)
    │
    └─► IP_EVENT_STA_GOT_IP (Root Node)
        └─► root_status_led_set_root(true)
```

## Unified LED Behavior

### Heartbeat-Based Behavior

All mesh nodes (root and child) now use the same heartbeat-based LED behavior. The RGB LEDs alternate between OFF and ON based on heartbeat parity.

**Behavior Rules**:
1. **Even Heartbeat** (counter % 2 == 0): RGB LED is turned OFF
2. **Odd Heartbeat** (counter % 2 == 1): RGB LED is turned ON
3. **Default Color**: If no custom RGB color has been set, the LED uses BLUE (RGB: 0, 0, 155)
4. **Custom Color**: If a custom RGB color has been set via color command, the LED uses that color
5. **Sequence Precedence**: If a sequence is active, heartbeat-based LED changes are skipped (sequence controls the LED)

**Implementation**: `lyktparad-espidf/src/mesh_root.c` and `lyktparad-espidf/src/mesh_child.c`

**Root Node Heartbeat Handler**:

```169:195:lyktparad-espidf/src/mesh_root.c
    /* Unified LED behavior for all nodes (root and child):
     * - Even heartbeat: LED OFF
     * - Odd heartbeat: LED ON (BLUE default or custom RGB)
     * - Skip LED changes if sequence mode is active (sequence controls LED)
     */
    if (mode_sequence_root_is_active()) {
        ESP_LOGD(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu - skipping LED change (sequence active)", (unsigned long)cnt);
    } else if (!(cnt % 2)) {
        /* even heartbeat: turn off light */
        mesh_light_set_colour(0);
        set_rgb_led(0, 0, 0);
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu (even) - LED OFF", (unsigned long)cnt);
    } else {
        /* odd heartbeat: turn on light using last RGB color or default to MESH_LIGHT_BLUE */
        if (root_rgb_has_been_set) {
            /* Use the color from the latest MESH_CMD_SET_RGB command */
            mesh_light_set_rgb(root_rgb_r, root_rgb_g, root_rgb_b);
            set_rgb_led(root_rgb_r, root_rgb_g, root_rgb_b);
            ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu (odd) - LED RGB(%d,%d,%d)",
                     (unsigned long)cnt, root_rgb_r, root_rgb_g, root_rgb_b);
        } else {
            /* Default to MESH_LIGHT_BLUE if no RGB command has been received */
            mesh_light_set_colour(MESH_LIGHT_BLUE);
            set_rgb_led(0, 0, 155);  /* Match MESH_LIGHT_BLUE RGB values */
            ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu (odd) - LED BLUE (default)", (unsigned long)cnt);
        }
    }
```

**Child Node Heartbeat Handler**:

```104:122:lyktparad-espidf/src/mesh_child.c
            } else if (!(hb%2)) {
                /* even heartbeat: turn off light */
                mesh_light_set_colour(0);
                set_rgb_led(0, 0, 0);
                ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (even) - LED OFF", (unsigned long)hb);
            } else {
                /* odd heartbeat: turn on light using last RGB color or default to MESH_LIGHT_BLUE */
                if (rgb_has_been_set) {
                    /* Use the color from the latest MESH_CMD_SET_RGB command */
                    mesh_light_set_rgb(last_rgb_r, last_rgb_g, last_rgb_b);
                    set_rgb_led(last_rgb_r, last_rgb_g, last_rgb_b);
                    ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (odd) - LED RGB(%d,%d,%d)",
                             (unsigned long)hb, last_rgb_r, last_rgb_g, last_rgb_b);
                } else {
                    /* Default to MESH_LIGHT_BLUE if no RGB command has been received */
                    mesh_light_set_colour(MESH_LIGHT_BLUE);
                    set_rgb_led(0, 0, 155);  /* Match MESH_LIGHT_BLUE RGB values */
                    ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (odd) - LED BLUE (default)", (unsigned long)hb);
                }
            }
```

### Color Command Support

Root nodes now respond to RGB color commands just like child nodes. When an RGB color command is received via the mesh network, the root node stores the RGB values and uses them for subsequent odd heartbeats.

**RGB Command Handler**: `lyktparad-espidf/src/mesh_root.c`

```290:323:lyktparad-espidf/src/mesh_root.c
/**
 * @brief Handle RGB command received via mesh network (for unified behavior)
 *
 * This function allows the root node to receive RGB commands via the mesh network,
 * enabling unified behavior where the root node can respond to color commands
 * just like child nodes.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void mesh_root_handle_rgb_command(uint8_t r, uint8_t g, uint8_t b)
{
    if (!esp_mesh_is_root()) {
        return;
    }

    /* Store RGB values for use in heartbeat handler */
    root_rgb_r = r;
    root_rgb_g = g;
    root_rgb_b = b;
    root_rgb_has_been_set = true;

    /* Update root node's LED immediately */
    esp_err_t err = mesh_light_set_rgb(r, g, b);
    if (err != ESP_OK) {
        ESP_LOGE(mesh_common_get_tag(), "[RGB] failed to set root node LED: 0x%x", err);
    }
    set_rgb_led(r, g, b);

    /* Stop sequence playback if active */
    mode_sequence_root_stop();

    ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] RGB command received via mesh: R:%d G:%d B:%d", r, g, b);
}
```

**RGB Values Storage**: Root nodes store the last received RGB values in static variables (`root_rgb_r`, `root_rgb_g`, `root_rgb_b`, `root_rgb_has_been_set`), similar to how child nodes store RGB values.

### Sequence Compatibility

When a sequence is active, heartbeat-based LED changes are skipped. The sequence controls the LED directly, ensuring sequences work correctly on root nodes just like child nodes.

**Sequence Check**: The heartbeat handler checks if a sequence is active before applying heartbeat-based LED changes:

```174:175:lyktparad-espidf/src/mesh_root.c
    if (mode_sequence_root_is_active()) {
        ESP_LOGD(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu - skipping LED change (sequence active)", (unsigned long)cnt);
```

## Root Status LED

### Purpose

The root status LED is a separate single-color LED that indicates whether the current node is the root node. This LED is independent of RGB LED behavior and provides clear visual identification of the root node.

**Behavior**:
- **ON**: Node is the root node
- **OFF**: Node is a non-root node (child node)
- **Updates Immediately**: LED state updates instantly when mesh role changes

### GPIO Configuration

The status LED GPIO pin is configurable via `mesh_device_config.h`:

**Configuration File**: `lyktparad-espidf/include/config/mesh_device_config.h.example`

```c
#define ROOT_STATUS_LED_GPIO      3
```

**Default**: GPIO 3 (if not defined in config)

**Implementation**: `lyktparad-espidf/src/root_status_led.c`

```24:27:lyktparad-espidf/src/root_status_led.c
/* GPIO pin for root status LED - configurable via mesh_device_config.h */
#ifndef ROOT_STATUS_LED_GPIO
#define ROOT_STATUS_LED_GPIO 3  /* Default GPIO 3 */
#endif
```

### Module Structure

The root status LED module provides three main functions:

1. **`root_status_led_init()`**: Initialize GPIO and set initial state
2. **`root_status_led_set_root(bool is_root)`**: Set LED state based on root status
3. **`root_status_led_update()`**: Update LED state based on current mesh role

**Header File**: `lyktparad-espidf/include/root_status_led.h`

```26:51:lyktparad-espidf/include/root_status_led.h
/**
 * @brief Initialize the root status LED GPIO
 *
 * This function initializes the GPIO pin for the root status LED and sets
 * the initial state based on the current mesh role.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t root_status_led_init(void);

/**
 * @brief Set the root status LED state
 *
 * This function sets the LED ON if the node is root, OFF if not root.
 *
 * @param is_root True if node is root, false otherwise
 */
void root_status_led_set_root(bool is_root);

/**
 * @brief Update the root status LED based on current mesh role
 *
 * This function checks the current mesh role and updates the LED state accordingly.
 * It should be called when the mesh role might have changed.
 */
void root_status_led_update(void);
```

### Initialization

The status LED is initialized during mesh initialization, after mesh configuration is complete:

**Location**: `lyktparad-espidf/src/mesh_common.c`

```842:846:lyktparad-espidf/src/mesh_common.c
    /* Initialize root status LED */
    esp_err_t status_led_err = root_status_led_init();
    if (status_led_err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "Failed to initialize root status LED: %s", esp_err_to_name(status_led_err));
    }
```

**Initialization Implementation**: `lyktparad-espidf/src/root_status_led.c`

```34:60:lyktparad-espidf/src/root_status_led.c
esp_err_t root_status_led_init(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "Root status LED already initialized");
        return ESP_OK;
    }

    /* Reset GPIO pin to default state before configuration */
    gpio_reset_pin(ROOT_STATUS_LED_GPIO);

    /* Configure GPIO pin as output */
    esp_err_t err = gpio_set_direction(ROOT_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO direction: %s", esp_err_to_name(err));
        return err;
    }

    /* Set initial state based on current mesh role */
    bool is_root = esp_mesh_is_root();
    gpio_set_level(ROOT_STATUS_LED_GPIO, is_root ? 1 : 0);

    is_initialized = true;
    ESP_LOGI(TAG, "Root status LED initialized on GPIO %d (current state: %s)",
             ROOT_STATUS_LED_GPIO, is_root ? "ON (root)" : "OFF (non-root)");

    return ESP_OK;
}
```

**Initial State**: The LED is set based on the current mesh role at initialization time. If the node is root at initialization, the LED is ON. If the node is not root, the LED is OFF.

### Role Change Updates

The status LED updates immediately when the mesh role changes. This is handled in multiple mesh event handlers:

**Layer Change Event**: `lyktparad-espidf/src/mesh_common.c`

```517:545:lyktparad-espidf/src/mesh_common.c
        /* Handle heartbeat, state updates, broadcast listener, and API listener on role change */
        if (was_root_before && !is_root_now) {
            /* Node lost root status - stop heartbeat, state updates, broadcast listener, and API listener */
            mesh_udp_bridge_stop_heartbeat();
            mesh_udp_bridge_stop_state_updates();
            mesh_udp_bridge_broadcast_listener_stop();
            mesh_udp_bridge_api_listener_stop();
            /* Update status LED to OFF */
            root_status_led_set_root(false);
        } else if (!was_root_before && is_root_now) {
            /* Node became root - register with external server if discovered */
            if (mesh_udp_bridge_is_server_discovered()) {
                /* Create task for non-blocking registration */
                BaseType_t task_err = xTaskCreate(registration_task, "reg_role_chg", 4096, NULL, 1, NULL);
                if (task_err != pdPASS) {
                    ESP_LOGW(MESH_TAG, "[REGISTRATION] Failed to create registration task on role change");
                }
            }
            /* Node is root and registered - start heartbeat and state updates */
            if (mesh_udp_bridge_is_registered()) {
                mesh_udp_bridge_start_heartbeat();
                mesh_udp_bridge_start_state_updates();
            }
            /* Broadcast listener will be started in mesh_root_ip_callback when IP is obtained */
            /* Update status LED to ON */
            root_status_led_set_root(true);
        } else {
            /* Update status LED based on current role */
            root_status_led_update();
        }
```

**Root Switch Event**: `lyktparad-espidf/src/mesh_common.c`

```590:618:lyktparad-espidf/src/mesh_common.c
        /* Handle heartbeat, state updates, broadcast listener, and API listener on root switch */
        if (!is_root_now) {
            /* Node is no longer root - stop heartbeat, state updates, broadcast listener, and API listener */
            mesh_udp_bridge_stop_heartbeat();
            mesh_udp_bridge_stop_state_updates();
            mesh_udp_bridge_broadcast_listener_stop();
            mesh_udp_bridge_api_listener_stop();
            /* Update status LED to OFF */
            root_status_led_set_root(false);
        } else if (is_root_now) {
            /* Node is root - register with external server if discovered */
            if (mesh_udp_bridge_is_server_discovered()) {
                /* Create task for non-blocking registration */
                BaseType_t task_err = xTaskCreate(registration_task, "reg_switch", 4096, NULL, 1, NULL);
                if (task_err != pdPASS) {
                    ESP_LOGW(MESH_TAG, "[REGISTRATION] Failed to create registration task on root switch");
                }
            }
            /* Node is root and registered - start heartbeat and state updates */
            if (mesh_udp_bridge_is_registered()) {
                mesh_udp_bridge_start_heartbeat();
                mesh_udp_bridge_start_state_updates();
            }
            /* Update status LED to ON */
            root_status_led_set_root(true);
        } else {
            /* Update status LED based on current role */
            root_status_led_update();
        }
```

**IP Event (Root Got IP)**: `lyktparad-espidf/src/mesh_common.c`

```752:758:lyktparad-espidf/src/mesh_common.c
        if (is_root) {
            /* Root node: router connected */
            /* Root node: router connected */
            is_router_connected = true;

            /* Update status LED to ON (root node got IP) */
            root_status_led_set_root(true);
```

**Mesh Started/Stopped Events**: `lyktparad-espidf/src/mesh_common.c`

```325:326:lyktparad-espidf/src/mesh_common.c
        /* Update status LED based on current mesh role (mesh role is now known) */
        root_status_led_update();
```

```335:336:lyktparad-espidf/src/mesh_common.c
        /* Update status LED - mesh stopped, node is no longer root */
        root_status_led_update();
```

**LED Control Functions**: `lyktparad-espidf/src/root_status_led.c`

```65:88:lyktparad-espidf/src/root_status_led.c
void root_status_led_set_root(bool is_root)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "Root status LED not initialized, skipping set");
        return;
    }

    gpio_set_level(ROOT_STATUS_LED_GPIO, is_root ? 1 : 0);
    ESP_LOGI(TAG, "Root status LED set to %s", is_root ? "ON (root)" : "OFF (non-root)");
}

/**
 * @brief Update the root status LED based on current mesh role
 */
void root_status_led_update(void)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "Root status LED not initialized, skipping update");
        return;
    }

    bool is_root = esp_mesh_is_root();
    root_status_led_set_root(is_root);
}
```

## Router Status LED Removal

### Removed Behavior

The previous implementation used RGB LEDs to indicate router connection status on root nodes:
- **ORANGE**: Router not connected
- **GREEN**: Router connected
- **WHITE blink**: Heartbeat indicator

This behavior has been completely removed. Root nodes now use the same heartbeat-based LED behavior as child nodes.

### Changes Made

**Removed Code**:
1. Router connection status LED logic from heartbeat handler
2. ORANGE/GREEN color logic from root node heartbeat
3. WHITE blink logic from heartbeat handler
4. Router connection status checks from event handlers

**Location**: `lyktparad-espidf/src/mesh_root.c` (previous implementation removed)

**Current Implementation**: The heartbeat handler now uses unified behavior (see Unified LED Behavior section above).

**Router Connection Status**: Router connection status is still tracked internally (`is_router_connected` variable) for other purposes, but it no longer affects LED behavior.

## Configuration

### GPIO Pin Configuration

The root status LED GPIO pin is configured via `mesh_device_config.h`:

**Configuration File**: `lyktparad-espidf/include/config/mesh_device_config.h.example`

```c
#define ROOT_STATUS_LED_GPIO      3
```

**Steps to Configure**:
1. Copy `mesh_device_config.h.example` to `mesh_device_config.h`
2. Modify `ROOT_STATUS_LED_GPIO` to your desired GPIO pin
3. Ensure the GPIO pin is available (not used by other peripherals)
4. Connect the LED to the configured GPIO pin (with appropriate current-limiting resistor)

**Default**: If `ROOT_STATUS_LED_GPIO` is not defined, the code uses GPIO 3 as the default.

**GPIO Requirements**:
- Must support GPIO output mode
- Must not conflict with other peripherals (e.g., flash, PSRAM, RMT, LEDC)
- Should be an available GPIO pin on your ESP32 variant

### GPIO Pin Selection Guide

**Common GPIO Restrictions**:
- **GPIO 0**: Boot mode selection (may affect startup)
- **GPIO 1**: TX pin (UART, typically used for debugging)
- **GPIO 2**: Diagnostic pin (may be used by some configurations)
- **GPIO 3**: RX pin (UART, typically used for debugging)
- **GPIO 6-11**: Flash/PSRAM (reserved, do not use)
- **GPIO 12**: Boot mode selection (may affect startup)

**Recommended GPIO Pins**:
- **GPIO 3**: Default (if UART is not used)
- **GPIO 4**: Available (if not used for other purposes)
- **GPIO 5**: Available (if not used for other purposes)
- **GPIO 13**: Available (if not used for other purposes)
- **GPIO 14**: Available (if not used for other purposes)

**Note**: Check your ESP32 variant's datasheet for specific GPIO restrictions.

## Implementation Details

### File Structure

**New Files Created**:
- `lyktparad-espidf/include/root_status_led.h`: Header file for root status LED module
- `lyktparad-espidf/src/root_status_led.c`: Implementation of root status LED module

**Files Modified**:
- `lyktparad-espidf/src/mesh_root.c`: Removed router status LED code, added unified behavior
- `lyktparad-espidf/src/mesh_common.c`: Added status LED initialization and role change updates
- `lyktparad-espidf/include/config/mesh_device_config.h.example`: Added `ROOT_STATUS_LED_GPIO` configuration
- `lyktparad-espidf/src/CMakeLists.txt`: Added `root_status_led.c` to build system

### Build System Integration

**CMakeLists.txt**: The root status LED module is included in the build:

```cmake
idf_component_register(
    SRCS "mesh.c" ... "root_status_led.c" ...
    ...
)
```

### Module Dependencies

**Root Status LED Module Dependencies**:
- `driver/gpio.h`: GPIO driver for LED control
- `esp_mesh.h`: Mesh API for role checking
- `esp_log.h`: Logging
- `config/mesh_device_config.h`: GPIO pin configuration

**Mesh Common Integration**:
- Includes `root_status_led.h` header
- Calls `root_status_led_init()` during mesh initialization
- Calls `root_status_led_set_root()` and `root_status_led_update()` on role changes

### LED Control Functions

**Root Status LED Functions**:

1. **`root_status_led_init()`**:
   - Initializes GPIO pin as output
   - Sets initial state based on current mesh role
   - Called once during mesh initialization

2. **`root_status_led_set_root(bool is_root)`**:
   - Sets LED ON if `is_root` is true, OFF if false
   - Called when mesh role is known (e.g., on role change events)

3. **`root_status_led_update()`**:
   - Checks current mesh role using `esp_mesh_is_root()`
   - Updates LED state accordingly
   - Called when mesh role might have changed

**RGB LED Functions** (unchanged, used by both root and child nodes):
- `mesh_light_set_colour()`: Set LED to predefined color
- `mesh_light_set_rgb()`: Set LED to custom RGB color
- `set_rgb_led()`: Direct RGB LED control

## Integration Points

### Mesh Initialization

**Location**: `lyktparad-espidf/src/mesh_common.c`

The status LED is initialized during mesh initialization, after mesh configuration:

```842:846:lyktparad-espidf/src/mesh_common.c
    /* Initialize root status LED */
    esp_err_t status_led_err = root_status_led_init();
    if (status_led_err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "Failed to initialize root status LED: %s", esp_err_to_name(status_led_err));
    }
```

**Initialization Order**:
1. Mesh configuration (`esp_mesh_set_max_layer()`, etc.)
2. Root status LED initialization
3. Mesh PS function (if enabled)
4. Mesh start

### Role Change Events

The status LED updates on the following mesh events:

1. **`MESH_EVENT_LAYER_CHANGE`**: Layer change (may indicate role change)
2. **`MESH_EVENT_ROOT_SWITCH_ACK`**: Root switch acknowledgment
3. **`IP_EVENT_STA_GOT_IP`**: Root node obtained IP address
4. **`IP_EVENT_STA_LOST_IP`**: Root node lost IP address
5. **`MESH_EVENT_STARTED`**: Mesh started
6. **`MESH_EVENT_STOPPED`**: Mesh stopped

**Implementation**: See "Role Change Updates" section above for code references.

### Heartbeat Integration

The unified heartbeat behavior is integrated into the root node heartbeat timer callback:

**Location**: `lyktparad-espidf/src/mesh_root.c`

The heartbeat handler checks sequence status, heartbeat parity, and RGB color settings before updating the LED.

### Color Command Integration

RGB color commands are handled by the root node through the `mesh_root_handle_rgb_command()` function:

**Location**: `lyktparad-espidf/src/mesh_root.c`

The function stores RGB values and updates the LED immediately, then uses those values for subsequent odd heartbeats.

### Sequence Integration

When a sequence is active, heartbeat-based LED changes are skipped:

**Location**: `lyktparad-espidf/src/mesh_root.c`

The heartbeat handler checks `mode_sequence_root_is_active()` before applying heartbeat-based LED changes.

## API Reference

### Root Status LED Functions

#### `root_status_led_init()`

**Description**: Initialize the root status LED GPIO pin and set initial state based on current mesh role.

**Function Signature**:
```c
esp_err_t root_status_led_init(void);
```

**Parameters**: None

**Returns**:
- `ESP_OK`: Initialization successful
- Other error codes: Initialization failed (GPIO configuration error)

**Example**:
```c
esp_err_t err = root_status_led_init();
if (err != ESP_OK) {
    ESP_LOGE("app", "Failed to initialize root status LED: %s", esp_err_to_name(err));
}
```

**Notes**:
- Should be called once during mesh initialization
- Sets initial LED state based on current mesh role
- Safe to call multiple times (returns `ESP_OK` if already initialized)

#### `root_status_led_set_root()`

**Description**: Set the root status LED state based on whether the node is root.

**Function Signature**:
```c
void root_status_led_set_root(bool is_root);
```

**Parameters**:
- `is_root`: `true` if node is root (LED ON), `false` if not root (LED OFF)

**Returns**: None

**Example**:
```c
if (esp_mesh_is_root()) {
    root_status_led_set_root(true);
} else {
    root_status_led_set_root(false);
}
```

**Notes**:
- Should be called when mesh role is known
- LED updates immediately
- Logs LED state change

#### `root_status_led_update()`

**Description**: Update the root status LED state based on current mesh role.

**Function Signature**:
```c
void root_status_led_update(void);
```

**Parameters**: None

**Returns**: None

**Example**:
```c
// Update status LED after role might have changed
root_status_led_update();
```

**Notes**:
- Checks current mesh role using `esp_mesh_is_root()`
- Updates LED state accordingly
- Useful when mesh role might have changed but exact change is unknown

### Configuration Constants

#### `ROOT_STATUS_LED_GPIO`

**Description**: GPIO pin number for root status LED.

**Definition**: `#define ROOT_STATUS_LED_GPIO 3` (default if not defined in config)

**Location**: `mesh_device_config.h` or `root_status_led.c` (fallback)

**Configuration**: Set in `mesh_device_config.h`:
```c
#define ROOT_STATUS_LED_GPIO      3
```

**Range**: Valid GPIO pin numbers for your ESP32 variant

**Default**: GPIO 3

## Error Handling

### GPIO Initialization Errors

If GPIO initialization fails, the error is logged but does not prevent mesh operation:

**Implementation**: `lyktparad-espidf/src/mesh_common.c`

```843:846:lyktparad-espidf/src/mesh_common.c
    esp_err_t status_led_err = root_status_led_init();
    if (status_led_err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "Failed to initialize root status LED: %s", esp_err_to_name(status_led_err));
    }
```

**Error Handling**: The mesh continues to operate normally even if status LED initialization fails. The status LED functions check if initialization was successful before attempting to control the LED.

### Uninitialized LED Access

If LED control functions are called before initialization, a warning is logged and the function returns without action:

**Implementation**: `lyktparad-espidf/src/root_status_led.c`

```67:70:lyktparad-espidf/src/root_status_led.c
    if (!is_initialized) {
        ESP_LOGW(TAG, "Root status LED not initialized, skipping set");
        return;
    }
```

**Error Handling**: Functions gracefully handle uninitialized state by logging a warning and returning without action.

### GPIO Configuration Errors

If GPIO configuration fails (e.g., invalid GPIO pin, GPIO in use), the error is returned from `root_status_led_init()`:

**Implementation**: `lyktparad-espidf/src/root_status_led.c`

```45:49:lyktparad-espidf/src/root_status_led.c
    esp_err_t err = gpio_set_direction(ROOT_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO direction: %s", esp_err_to_name(err));
        return err;
    }
```

**Error Handling**: Errors are logged and returned to the caller. The caller should check the return value and handle errors appropriately.

---

**Related Documentation**:
- [Mode Sequence Guide](mode-sequence.md) - Sequence playback and LED control
- [Mesh Force Node Status](mesh-force-node-status.md) - Mesh node status management
