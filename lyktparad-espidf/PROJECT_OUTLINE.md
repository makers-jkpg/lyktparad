# Project Outline: ESP-IDF Mesh Network with LED Control

## Overview
This project implements a WiFi mesh network using ESP-IDF on ESP32/ESP32-C3 devices. The mesh network enables communication between nodes, with visual feedback provided through WS2812 LED strips. The root node periodically sends heartbeat messages to all connected nodes, and nodes respond by changing LED colors based on the mesh state.

## Architecture

### Core Components

#### 1. **Mesh Network Layer** (`src/mesh.c`)
- Implements ESP-MESH protocol for WiFi mesh networking
- Supports tree topology (configurable)
- Maximum 6 layers deep
- Root node connects to WiFi router; child nodes connect through mesh
- Handles mesh events: connection, disconnection, layer changes, routing table updates

#### 2. **LED Control Layer**
- **WS2812/Neopixel Driver** (`src/light_neopixel.c`): Provides abstraction for WS2812 LED strip control using RMT (Remote Control) peripheral for precise timing
- **Common-Cathode RGB LED Driver** (`src/light_common_cathode.c`): Provides LEDC PWM control for common-cathode RGB LEDs (optional, enabled via `RGB_ENABLE`)
- Implements color states for different mesh conditions:
  - Red, Green, Blue, Yellow, Pink, Orange, White
  - Warning state (white/gray)

#### 3. **Web Frontend** (`src/mesh_web.c`)
- HTTP web server running exclusively on root node
- Provides web-based control interface for mesh network
- Displays live heartbeat counter updates
- Shows current RGB color with color picker interface
- Sends RGB commands to all mesh nodes via web interface

#### 4. **Main Application** (`src/main.c`)
- Currently contains a simple WS2812 blink example
- Demonstrates basic LED strip initialization and control
- Uses GPIO 10 for LED data line
- Includes diagnostic GPIO (GPIO 2) for debugging

## Key Features

### Mesh Network Configuration
- **Mesh ID**: `0x77:0x77:0x77:0x77:0x77:0x77`
- **Router Connection**: Connects to WiFi router (configurable SSID/password)
- **Channel**: Fixed to channel 6 (configurable)
- **Topology**: Tree structure (configurable)
- **Power Saving**: Optional mesh power saving mode
- **Max Connections**: 6 mesh AP connections per node

### Communication Protocol

#### Heartbeat System
- Root node sends 4-byte heartbeat counter every 500ms
- Heartbeat sent to all nodes in routing table via unicast
- **Root node LED behavior**:
  - Base color indicates router connection status: ORANGE (not connected) or GREEN (connected)
  - Router not connected: Steady ORANGE
  - Router connected, no mesh nodes: Steady GREEN
  - Router connected with mesh nodes: Steady GREEN base with 100ms WHITE blink on each heartbeat (base color always maintained)
- **Non-root node LED behavior**:
  - Even heartbeat counts: LED off
  - Odd heartbeat counts: LED on (BLUE by default, or custom RGB if set)

#### Light Control Messages
- Uses `mesh_light_ctl_t` structure for control commands
- Token-based authentication (`MESH_CONFIG_TOKEN_ID`, `MESH_CONFIG_TOKEN_VALUE` - defined in `include/config/mesh_config.h`)
- Commands: ON/OFF control
- Point-to-point (P2P) communication

### LED Visual Indicators

#### Connection States

The LED provides visual feedback about the mesh network connection status through color-coded states. Each state corresponds to a specific mesh condition.

**Initialization/Unconnected State**

- **Root Node Unconnected**: **Orange** (RGB: 255, 165, 0)
  - Set when mesh starts and root node is not connected to router
  - Indicates root node is starting up or not connected to router
  - Also set when mesh stops or root disconnects from router

- **Non-Root Node Unconnected**: **Red** (RGB: 155, 0, 0)
  - Set during LED initialization (`mesh_light_init()`) and when mesh starts
  - Indicates non-root node is starting up or not connected to mesh
  - Also set when mesh stops or node disconnects from parent

**Root Node LED Behavior**

The root node LED base color indicates router connection status, and white blink indicates heartbeat activity:
- **Not Connected to Router**: Steady ORANGE (RGB: 255, 165, 0) - Base color indicates router is not connected
- **Connected to Router (no mesh nodes)**: Steady GREEN (RGB: 0, 155, 0) - Base color indicates router is connected
- **Connected to Router with Mesh Nodes**: Steady GREEN base with 100ms WHITE (RGB: 255, 255, 255) blink on each heartbeat
  - The GREEN base color is always maintained to show router connection status
  - The white blink occurs every 500ms (synchronized with heartbeat) when sending heartbeats to mesh nodes
  - Base color does not change during heartbeat - it always reflects router connection state

**Non-Root Node LED Behavior**

Non-root nodes use heartbeat-based color changes:
- **Unconnected**: Steady RED (RGB: 155, 0, 0) - Not connected to mesh
- **Connected**: LED turns OFF, then heartbeat system takes control
- **Heartbeat Response**: Alternates between OFF and BLUE (RGB: 0, 0, 155) based on heartbeat parity
  - Even heartbeats: LED OFF
  - Odd heartbeats: LED ON (BLUE by default, or custom RGB if set via web interface)

This creates a dynamic visual effect where non-root node LEDs alternate between off and on, synchronized with the heartbeat counter from the root node.

#### Heartbeat Response Summary
- **Root node**:
  - Router not connected: Steady ORANGE (base color indicates router status)
  - Router connected, no mesh nodes: Steady GREEN (base color indicates router connection)
  - Router connected with mesh nodes: Steady GREEN base with 100ms WHITE blink per heartbeat (base color always maintained)
- **Non-root nodes**: Blue (on) / Off (off) based on heartbeat parity, or custom RGB if set

## Hardware Configuration

### LED Strip
- **GPIO**: 10 (WS2812 data line)
- **Number of Pixels**: 1
- **Model**: WS2812
- **Color Format**: GRB
- **RMT Resolution**: 10 MHz

### Additional Hardware
- **Diagnostic GPIO**: GPIO 2 (for debugging/MCU activity verification)
- **Common-Cathode RGB LED** (optional, in `light_common_cathode.c`): GPIO 0 (Green), GPIO 1 (Red), GPIO 2 (Blue) - using LEDC PWM (enabled via `RGB_ENABLE` in `include/config/mesh_device_config.h`)

## Software Architecture

### Task Structure
1. **Mesh Event Handler**: Processes all mesh network events
2. **P2P Receive Task** (`esp_mesh_p2p_rx_main`): Receives and processes mesh messages
3. **P2P Transmit Task** (`esp_mesh_p2p_tx_main`): Sends control messages (currently disabled)
4. **Heartbeat Timer**: Periodic timer (500ms) for root node heartbeat
5. **HTTP Server Task**: Handles web interface requests (root node only)

### Initialization Flow
1. Initialize LED strip hardware
2. Initialize NVS (Non-Volatile Storage)
3. Initialize TCP/IP stack
4. Initialize event loop
5. Create WiFi mesh network interfaces
6. Initialize WiFi
7. Initialize ESP-MESH
8. Configure mesh topology and parameters
9. Configure automatic root election (self-organization enabled)
10. Start mesh network
11. Initialize WS2812/Neopixel LED strip
12. Initialize common-cathode RGB LED (if `RGB_ENABLE` is defined)
13. Start heartbeat timer (root node only)
14. Start HTTP web server when root node gets IP address

## Configuration

### Build Configuration
- **Platform**: ESP-IDF
- **Target**: ESP32-C3 or ESP32
- **Partition Table**: Custom `huge_app.csv` (large app partition)
- **Component**: Uses managed component `espressif/led_strip` v3.0.1

### Mesh Configuration

Site-specific configuration is stored in `include/config/mesh_config.h`. This file is not committed to version control for security reasons. To set up the configuration:

1. Copy `include/config/mesh_config.h.example` to `include/config/mesh_config.h`
2. Update the values in `mesh_config.h` with your site-specific settings:
   - Router SSID and password
   - Mesh ID (6-byte identifier)
   - Mesh channel
   - Mesh AP password
   - Authentication tokens

**WARNING**: Never commit `mesh_config.h` to version control as it contains credentials!

### Mesh Configuration Defaults (from example file)
- Router SSID: "YOUR_ROUTER_SSID" (must be configured)
- Router Password: "YOUR_ROUTER_PASSWORD" (must be configured)
- Mesh AP Password: "YOUR_MESH_PASSWORD" (must be configured)
- Mesh AP Auth: WPA2_PSK (default: 3)
- Mesh ID: { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } (must be configured)
- Mesh Channel: 6 (configurable)
- Max Layer: 6
- Route Table Size: 50
- Power Saving: Enabled (10% duty cycle)

## Data Flow

### Root Node
1. Root connects to WiFi router
2. Gets IP address via DHCP
3. LED changes from ORANGE to GREEN (base color indicates router connection)
4. Starts HTTP web server on port 80
5. Maintains routing table of all mesh nodes
6. Sends periodic heartbeat (4-byte counter) to all nodes every 500ms
7. Updates LED based on router and mesh state:
   - Router not connected: Steady ORANGE
   - Router connected, no mesh nodes: Steady GREEN
   - Router connected with mesh nodes: Steady GREEN base with 100ms WHITE blink on each heartbeat
8. Serves web interface for mesh control and monitoring

### Non-Root Nodes
1. Scan for mesh network (LED: RED)
2. Connect to parent node
3. Join mesh network at assigned layer
4. LED turns OFF when connected
5. Receive heartbeat messages from root
6. Update LED based on heartbeat:
   - Even heartbeats: LED OFF
   - Odd heartbeats: LED ON (BLUE by default, or custom RGB if set)
7. Can receive and process light control commands

## Event Handling

### Mesh Events
- `MESH_EVENT_STARTED`: Mesh network started
- `MESH_EVENT_PARENT_CONNECTED`: Connected to parent
- `MESH_EVENT_PARENT_DISCONNECTED`: Disconnected from parent
- `MESH_EVENT_LAYER_CHANGE`: Mesh layer changed
- `MESH_EVENT_CHILD_CONNECTED`: Child node connected
- `MESH_EVENT_ROUTING_TABLE_ADD/REMOVE`: Routing table changes
- Various other mesh lifecycle events

### IP Events
- `IP_EVENT_STA_GOT_IP`: Root node received IP address (triggers web server start, sets LED to GREEN)
- `IP_EVENT_STA_LOST_IP`: Root node lost IP address (sets LED to ORANGE)

### Web Server Events
- Web server automatically starts when root node receives IP address
- Web server stops if root status changes (node is no longer root)

## Current State

### Active Implementation
- Mesh network initialization and management
- Heartbeat system (root sends, all nodes receive)
- LED control based on heartbeat and mesh state
- Web frontend for root node (HTTP server on port 80)
- Live heartbeat counter display
- RGB color picker and control interface

### Disabled/Commented Features
- P2P transmit task (light control sending)
- Some mesh light processing functions
- Detailed logging in receive task

## Dependencies
- **ESP-IDF**: Core framework
- **espressif/led_strip**: WS2812 LED strip driver (v3.0.1)
- **ESP-MESH**: Built-in mesh networking stack
- **esp_http_server**: HTTP server component for web interface

## Web Interface

### Accessing the Web Interface

The web interface is only available on the root node. To access it:

1. Ensure the root node has connected to the WiFi router and obtained an IP address
2. Check the serial monitor for the IP address (logged when `IP_EVENT_STA_GOT_IP` occurs)
3. Open a web browser and navigate to `http://<root-node-ip>` (default port 80)
4. The web interface will display:
   - **Heartbeat Counter**: Live-updating counter showing current heartbeat value
   - **Current Color**: Large color box showing the current RGB color (or MESH_LIGHT_BLUE default)
   - **Color Picker**: Click the color box to open a color picker and apply new colors

### Web API Endpoints

- `GET /api/heartbeat` - Returns current heartbeat counter as JSON: `{"heartbeat": <counter>}`
- `GET /api/color` - Returns current RGB color as JSON: `{"r": <r>, "g": <g>, "b": <b>, "is_set": <bool>}`
- `POST /api/color` - Accepts RGB values and applies them to all mesh nodes
  - Request body: `{"r": <0-255>, "g": <0-255>, "b": <0-255>}`
  - Response: `{"success": true}` or `{"success": false, "error": "<message>"}`

### Web Server Lifecycle

- The web server automatically starts when the root node receives an IP address (`IP_EVENT_STA_GOT_IP`)
- The web server automatically stops if the node is no longer root (e.g., `MESH_EVENT_ROOT_SWITCH_ACK`, `MESH_EVENT_LAYER_CHANGE`)
- The web server only runs on the root node - non-root nodes do not start the server

## Use Cases
- Distributed lighting control systems
- Mesh network monitoring and visualization
- Network topology demonstration
- IoT device mesh networking examples
- Real-time mesh health monitoring

