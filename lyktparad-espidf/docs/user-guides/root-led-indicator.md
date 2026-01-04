# Root LED Status Indicator

**Last Updated:** 2026-01-04

## Table of Contents

1. [Introduction](#introduction)
2. [Overview](#overview)
3. [Configuration](#configuration)
4. [Blinking Patterns](#blinking-patterns)
5. [Understanding the Patterns](#understanding-the-patterns)
6. [Troubleshooting](#troubleshooting)
7. [FAQ](#faq)

## Introduction

The Root LED Status Indicator is an **optional** feature that provides visual feedback about your root node's network status using a single-color LED. The LED blinks in different patterns to indicate whether the router is connected and how many mesh nodes are connected.

> **For Developers**: See [Developer Guide](../dev-guides/root-node-rgb-led-behavior.md) for technical implementation details.

### What Can the Root LED Indicator Do?

- **Visual status indication** - See at a glance if your root node is connected to the router
- **Node count feedback** - Know how many mesh nodes are connected
- **Network diagnostics** - Quickly identify network connection issues
- **Optional feature** - Disabled by default (zero overhead when disabled)

### Feature Status

The Root LED Status Indicator is an **optional feature** that is **disabled by default**. This means:

- **No code is compiled** when disabled (zero overhead)
- **No GPIO pin is used** when disabled
- **No impact on performance** when disabled
- You must explicitly enable it in the configuration file

## Overview

The Root LED Status Indicator uses a single-color LED (non-RGB) connected to a GPIO pin to display different blinking patterns. Each pattern provides information about the root node's status:

- **Router connection status** (connected or not connected)
- **Mesh node count** (number of child nodes connected)

The LED is **always OFF** on child nodes (non-root nodes). Only root nodes use the LED indicator.

**Key Points**:
- LED only blinks on root nodes
- Child nodes: LED is always OFF
- Patterns update automatically when status changes
- All patterns repeat every 1 second (1000ms cycle)

## Configuration

### Enabling the Feature

To enable the Root LED Status Indicator:

1. **Locate the configuration file**: `lyktparad-espidf/include/config/mesh_device_config.h.example`
2. **Copy to your config**: Copy the example file to `mesh_device_config.h` (if you haven't already)
3. **Uncomment the GPIO pin**: Find this line:
   ```c
   // #define ROOT_STATUS_LED_GPIO      3  // Optional: GPIO pin for root status LED (disabled by default)
   ```
4. **Set your GPIO pin**: Uncomment and change the pin number if needed:
   ```c
   #define ROOT_STATUS_LED_GPIO      3  // GPIO pin for root status LED
   ```
5. **Rebuild the firmware**: Rebuild your firmware to compile the feature
6. **Flash the firmware**: Flash the updated firmware to your device

### Choosing a GPIO Pin

**Important**: Choose a GPIO pin that:

- Is available (not used by other peripherals)
- Supports GPIO output mode
- Is not reserved for flash/PSRAM (typically GPIO 6-11)
- Is accessible on your ESP32 variant

**Common choices**:
- GPIO 3 (default)
- GPIO 4, GPIO 5
- GPIO 12, GPIO 13, GPIO 14
- GPIO 25, GPIO 26, GPIO 27
- GPIO 32, GPIO 33 (input-capable on some ESP32 variants)

**Example configuration**:
```c
#define ROOT_STATUS_LED_GPIO      4  // Using GPIO 4 instead of GPIO 3
```

### Hardware Connection

Connect your LED to the configured GPIO pin:

1. **LED connection**: Connect the LED anode (longer leg) to the GPIO pin through a current-limiting resistor (typically 220Ω - 1kΩ)
2. **Resistor**: Connect the other side of the resistor to the GPIO pin
3. **Ground**: Connect the LED cathode (shorter leg) to GND
4. **Power**: The LED is powered by the GPIO pin (no external power needed)

**Wiring diagram**:
```
ESP32 GPIO Pin → [220Ω Resistor] → LED Anode → LED Cathode → GND
```

**Note**: If your LED already has a built-in resistor, you can connect it directly (still recommended to use a small resistor for protection).

### Disabling the Feature

To disable the Root LED Status Indicator (default):

- **Leave the line commented out** in your `mesh_device_config.h` file:
  ```c
  // #define ROOT_STATUS_LED_GPIO      3  // Optional: GPIO pin for root status LED (disabled by default)
  ```
- **Or remove the line entirely** - the feature will remain disabled
- No code is compiled, no GPIO pin is used, zero overhead

## Blinking Patterns

When enabled, the LED blinks in different patterns based on the root node's status. All patterns repeat every 1 second (1000ms cycle).

### Pattern 1: STARTUP

**Timing**: 250ms ON - 750ms OFF

**Visual**: Single blink with long pause

**When you see this**:
- Router is **not connected**
- No child nodes are connected (mesh network is just the root node)

**What it means**:
- Root node is running but not connected to Wi-Fi router
- Mesh network exists but has no child nodes yet
- This is the initial state when the mesh starts

### Pattern 2: ROUTER_ONLY

**Timing**: 125ms ON - 125ms OFF - 125ms ON - 625ms OFF

**Visual**: Two short blinks (double blink), then longer pause

**When you see this**:
- Router **is connected**
- No child nodes are connected (only the root node in the mesh)

**What it means**:
- Root node has internet access via Wi-Fi router
- Mesh network is running but no child nodes have joined yet
- Root node can communicate with external servers

### Pattern 3: NODES_ONLY

**Timing**: 125ms ON - 375ms OFF - 125ms ON - 375ms OFF

**Visual**: Two blinks with medium pause between (slower double blink)

**When you see this**:
- Router is **not connected**
- One or more child nodes **are connected** to the mesh

**What it means**:
- Root node lost Wi-Fi router connection
- Mesh network is still functioning with child nodes
- Root node cannot communicate with external servers
- Mesh nodes can still communicate with each other

### Pattern 4: ROUTER_AND_NODES

**Timing**: 125ms ON - 125ms OFF (repeated 4 times)

**Visual**: Four rapid blinks (quadruple blink)

**When you see this**:
- Router **is connected**
- One or more child nodes **are connected** to the mesh

**What it means**:
- Root node has internet access via Wi-Fi router
- Mesh network is fully operational with child nodes
- Root node can communicate with external servers
- All mesh nodes can communicate with each other
- **This is the ideal/optimal state**

### Pattern 5: OFF

**LED is completely OFF (no blinking)**

**When you see this**:
- Node is a **child node** (not the root node)

**What it means**:
- This node is not the root node
- LED indicator only works on root nodes
- Child nodes do not use the LED indicator

## Understanding the Patterns

### Pattern Logic

The LED pattern is determined by two factors:

1. **Router connection status**: Is the root node connected to the Wi-Fi router?
2. **Node count**: How many child nodes are connected to the mesh?

**Decision table**:

| Router Connected? | Child Nodes? | Pattern |
|------------------|--------------|---------|
| No | No | STARTUP |
| Yes | No | ROUTER_ONLY |
| No | Yes (1+) | NODES_ONLY |
| Yes | Yes (1+) | ROUTER_AND_NODES |
| N/A (not root) | N/A | OFF |

### Pattern Changes

The LED pattern **updates automatically** when:

- **Router connects**: Pattern changes from STARTUP/NODES_ONLY to ROUTER_ONLY/ROUTER_AND_NODES
- **Router disconnects**: Pattern changes from ROUTER_ONLY/ROUTER_AND_NODES to STARTUP/NODES_ONLY
- **Child node connects**: Pattern changes from STARTUP/ROUTER_ONLY to NODES_ONLY/ROUTER_AND_NODES
- **Child node disconnects**: Pattern changes from NODES_ONLY/ROUTER_AND_NODES to STARTUP/ROUTER_ONLY
- **Node becomes root**: Pattern starts based on current router/node status
- **Node loses root status**: LED turns OFF (becomes child node)

### Pattern Timing

All patterns use a **1-second (1000ms) cycle**:

- Pattern repeats every second
- Timing is consistent across all patterns
- Easy to identify by counting blinks
- Visual distinction between patterns

**Example timing breakdown**:
- **STARTUP**: 250ms blink, 750ms pause = 1000ms total
- **ROUTER_ONLY**: 125+125+125+625 = 1000ms total
- **NODES_ONLY**: 125+375+125+375 = 1000ms total
- **ROUTER_AND_NODES**: 125×8 = 1000ms total

## Troubleshooting

### LED Not Blinking

**If the LED is not blinking at all**:

1. **Check if feature is enabled**: Verify `ROOT_STATUS_LED_GPIO` is defined in `mesh_device_config.h`
2. **Check GPIO pin**: Ensure the GPIO pin number is correct
3. **Check wiring**: Verify LED and resistor are connected correctly
4. **Check if root node**: LED only blinks on root nodes (OFF on child nodes)
5. **Check firmware**: Ensure firmware was rebuilt and flashed after enabling the feature
6. **Check serial logs**: Look for "Root status LED initialized" message in serial logs

### LED Always ON or Always OFF

**If LED is always ON**:
- Check wiring (LED might be connected backwards)
- Check GPIO pin configuration
- Verify resistor value (too high resistance)

**If LED is always OFF**:
- Verify you're on a root node (LED is OFF on child nodes)
- Check if feature is enabled in configuration
- Check wiring connections
- Check if LED is functional (test with multimeter)

### Wrong Pattern Displayed

**If pattern doesn't match expected status**:

1. **Router connection**: Check if router is actually connected (check serial logs for IP address)
2. **Node count**: Verify actual node count matches pattern (check serial logs for routing table size)
3. **Pattern timing**: All patterns repeat every 1 second - if timing seems wrong, check hardware connections
4. **Status updates**: Patterns update automatically - wait a few seconds after status changes

### LED Flickering or Unstable

**If LED flickers or behaves erratically**:

1. **Check resistor value**: Resistor might be too small (LED draws too much current)
2. **Check wiring**: Loose connections can cause flickering
3. **Check GPIO pin**: Verify GPIO pin is not shared with other peripherals
4. **Check power supply**: Insufficient power can cause unstable behavior
5. **Check serial logs**: Look for error messages about GPIO or timer

### GPIO Pin Conflicts

**If GPIO pin conflicts with other features**:

1. **Choose different pin**: Change `ROOT_STATUS_LED_GPIO` to an unused GPIO pin
2. **Check pin restrictions**: Some GPIOs are reserved for flash/PSRAM (GPIO 6-11)
3. **Check other features**: Verify RGB LED, sequence mode, or other features don't use the same pin
4. **Rebuild firmware**: Rebuild after changing GPIO pin

## FAQ

### Q: Do I need the Root LED Indicator?

A: No, it's completely optional. The feature is disabled by default and has zero overhead when disabled. It's useful for visual diagnostics and quick status checks, but not required for mesh network operation.

### Q: Can I use any GPIO pin?

A: Most GPIO pins work, but avoid:
- GPIO 6-11 (typically reserved for flash/PSRAM)
- GPIO pins used by other features (RGB LED, etc.)
- Input-only pins on some ESP32 variants

Common safe choices: GPIO 3, 4, 5, 12, 13, 14, 25, 26, 27, 32, 33.

### Q: What resistor value should I use?

A: Typical values are 220Ω to 1kΩ. Start with 220Ω for standard LEDs. If LED is too bright, use 470Ω or 1kΩ. If LED is too dim, use 150Ω (but check current limits).

### Q: Can I use an RGB LED instead?

A: No, the Root LED Indicator uses a single-color LED. RGB LEDs are used for the main mesh communication LED (separate feature). You can use a single-color LED from an RGB LED strip by connecting just one color channel.

### Q: Does the LED work on child nodes?

A: No, the LED is always OFF on child nodes. Only root nodes use the LED indicator to show network status.

### Q: How do I know which pattern I'm seeing?

A: Count the blinks and note the timing:
- **1 blink per second**: STARTUP (250ms ON)
- **2 blinks, fast**: ROUTER_ONLY (125ms each, close together)
- **2 blinks, slow**: NODES_ONLY (125ms each, further apart)
- **4 blinks, rapid**: ROUTER_AND_NODES (125ms each, very close together)
- **No blinks**: OFF (child node or disabled)

### Q: Can I disable the feature after enabling it?

A: Yes, simply comment out or remove the `ROOT_STATUS_LED_GPIO` definition in your `mesh_device_config.h` file and rebuild the firmware. No code will be compiled and the GPIO pin will not be used.

### Q: Does enabling the feature affect performance?

A: No significant performance impact. The LED blinking uses a low-priority timer and minimal CPU resources. When disabled, there's zero overhead (no code compiled).

### Q: What happens if I configure an invalid GPIO pin?

A: The firmware may fail to initialize the LED, but this won't crash the device. Check serial logs for error messages. The mesh network will continue to function normally.

### Q: Can I use multiple LEDs?

A: No, the feature supports only one LED connected to one GPIO pin. If you need multiple status indicators, you would need to modify the code (advanced users only).

### Q: How accurate is the timing?

A: Timing is accurate to within a few milliseconds. The ESP32 timer provides precise timing control. All patterns repeat every 1 second (1000ms cycle).

### Q: Does the LED work during firmware updates?

A: The LED continues to work during normal operation, but behavior during firmware updates depends on the update process. LED may turn off during reboot or update procedures.

### Q: Can I change the blinking patterns?

A: Pattern timing is fixed in the firmware. To change patterns, you would need to modify the source code (advanced users only). See the developer guide for details.

### Q: What if my LED is too bright or too dim?

A: Adjust the resistor value:
- **Too bright**: Use larger resistor (470Ω, 1kΩ)
- **Too dim**: Use smaller resistor (150Ω, 100Ω) - but check current limits

### Q: Does the feature work with external power supplies?

A: Yes, as long as the GPIO pin can drive the LED (most ESP32 GPIOs can source/sink 12-40mA). For high-power LEDs, you may need a transistor driver circuit (advanced).
