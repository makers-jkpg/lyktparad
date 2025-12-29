# ESP-IDF Mesh Network with LED Control

A WiFi mesh network implementation using ESP-IDF on ESP32/ESP32-C3 devices. The mesh network enables communication between nodes with visual feedback provided through LEDs. The project supports two LED driver types: WS2812/Neopixel LED strips (using RMT) and common-cathode RGB LEDs (using LEDC PWM). The root node periodically sends heartbeat messages to all connected nodes, and nodes respond by changing LED colors based on the mesh state.

## Quick Start

Before building, you need to configure two files:

1. **`include/mesh_config.h`** - Copy from `include/mesh_config.h.example`:
   - Set WiFi router SSID and password (for root node)
   - Configure Mesh ID (6-byte identifier, must match all nodes)
   - Set Mesh channel (1-11 for 2.4GHz, or 0 for auto)
   - Set Mesh AP password (for node-to-node connections)
   - Configure authentication tokens

2. **`include/mesh_device_config.h`** - Copy from `include/mesh_device_config.h.example`:
   - **For WS2812/Neopixel LEDs**: Set LED GPIO pin (default: 10), number of pixels (default: 1), and RMT resolution (default: 10 MHz)
   - **For common-cathode RGB LEDs**: Uncomment `RGB_ENABLE` and configure GPIO pins, LEDC timer, channels, and PWM settings

**⚠️ Important**: These config files are in `.gitignore` - never commit them with real credentials!

## Features

- **ESP-MESH Networking**: Self-organizing WiFi mesh network with tree topology
- **Visual Feedback**: LED indicators provide real-time mesh status indication
- **Dual LED Support**: Supports both WS2812/Neopixel LED strips (RMT) and common-cathode RGB LEDs (LEDC PWM)
- **Web Interface**: Root node hosts a web server for monitoring and control
- **Heartbeat System**: Periodic synchronization messages from root to all nodes
- **RGB Color Control**: Web-based color picker to set LED colors across the mesh
- **Layer-Based Indicators**: Different LED colors for different mesh layers
- **Serial Logging**: Comprehensive logging for debugging and monitoring

## Hardware Requirements

- **ESP32** or **ESP32-C3** development board
- **LED Hardware** (choose one):
  - **WS2812/Neopixel LED strip** (or single WS2812 LED) - Recommended for addressable LED strips
  - **Common-cathode RGB LED** - For simple RGB LEDs with separate R, G, B pins
- **WiFi Router** (for root node connection)
- **USB cable** for programming and serial communication

### LED Hardware Options

#### Option 1: WS2812/Neopixel LEDs (Default)
- **LED GPIO Pin**: GPIO 10 (configurable)
- **Number of LEDs**: 1 (configurable)
- **RMT Resolution**: 10 MHz (configurable)
- **Power**: Requires 5V power supply
- **Best for**: Addressable LED strips, multiple LEDs, complex patterns

#### Option 2: Common-Cathode RGB LEDs
- **RGB GPIO Pins**: GPIO 0 (Green), GPIO 1 (Red), GPIO 2 (Blue) - configurable
- **LEDC Timer**: Timer 0 (configurable)
- **PWM Frequency**: 5000 Hz (configurable)
- **PWM Resolution**: 8-bit (256 levels, configurable)
- **Power**: Typically 3.3V or 5V depending on LED
- **Best for**: Simple RGB LEDs, single-color control, lower power consumption

## Software Requirements

- **PlatformIO** (with VS Code extension)
- **ESP-IDF Framework** (v5.0+)
- **Python 3.x** (for build tools)

## Installation

1. **Clone the repository**:
   ```bash
   git clone <repository-url>
   cd lyktparad-espidf
   ```

2. **Install PlatformIO**:
   - Install the PlatformIO IDE extension in VS Code
   - PlatformIO will automatically install required dependencies

3. **Configure Mesh Settings**:
   ```bash
   cp include/mesh_config.h.example include/mesh_config.h
   ```
   Edit `include/mesh_config.h` and update:
   - WiFi router SSID and password
   - Mesh ID (6-byte identifier)
   - Mesh channel
   - Mesh AP password
   - Authentication tokens

4. **Configure LED Hardware**:
   ```bash
   cp include/mesh_device_config.h.example include/mesh_device_config.h
   ```
   Edit `include/mesh_device_config.h`:
   - **For WS2812/Neopixel** (default): Update GPIO pin, number of pixels, and RMT resolution if needed
   - **For common-cathode RGB LEDs**: Uncomment `#define RGB_ENABLE` and configure GPIO pins, LEDC timer, channels, and PWM settings

## Building and Flashing

This project **must be built within VS Code** using PlatformIO. Do not use command-line builds.

1. **Open the project** in VS Code
2. **Build the project**:
   - Click the PlatformIO icon in the sidebar
   - Click "Build" (checkmark icon) or use `Ctrl+Alt+B`
3. **Flash to device**:
   - Connect your ESP32 via USB
   - Click "Upload" (arrow icon) or use `Ctrl+Alt+U`
4. **Monitor serial output**:
   - Click "Monitor" (plug icon) or use `Ctrl+Alt+S`
   - Default baud rate: 115200

## Configuration

### Mesh Configuration (`include/mesh_config.h`)

**⚠️ WARNING**: Never commit `mesh_config.h` to version control! It contains sensitive credentials.

Required settings:
- `MESH_CONFIG_ROUTER_SSID`: WiFi network name for root node
- `MESH_CONFIG_ROUTER_PASSWORD`: WiFi password
- `MESH_CONFIG_MESH_ID`: 6-byte unique identifier (must match across all nodes)
- `MESH_CONFIG_MESH_CHANNEL`: WiFi channel (1-11 for 2.4GHz, or 0 for auto)
- `MESH_CONFIG_MESH_AP_PASSWORD`: Password for mesh access point
- `MESH_CONFIG_MESH_AP_AUTHMODE`: Authentication mode (3 = WPA2_PSK recommended)
- `MESH_CONFIG_TOKEN_ID`: Token ID for command authentication
- `MESH_CONFIG_TOKEN_VALUE`: Token value for command authentication

### LED Configuration (`include/mesh_device_config.h`)

**⚠️ WARNING**: Never commit `mesh_device_config.h` to version control if it contains site-specific settings.

The project supports two LED driver types. Configure the one that matches your hardware:

#### WS2812/Neopixel LED Configuration (Default)

These settings are always active and control the WS2812/Neopixel LED driver:

- `MESH_LED_GPIO`: GPIO pin connected to WS2812 data line (default: 10)
- `MESH_LED_NUM_PIXELS`: Number of LEDs in strip (default: 1)
- `MESH_LED_RMT_RES_HZ`: RMT resolution in Hz (default: 10000000)

#### Common-Cathode RGB LED Configuration (Optional)

To enable common-cathode RGB LED support, uncomment `#define RGB_ENABLE` in `mesh_device_config.h`:

- `RGB_ENABLE`: Uncomment to enable common-cathode RGB LED support
- `RGB_LEDC_TIMER`: LEDC timer number (default: LEDC_TIMER_0)
- `RGB_LEDC_MODE`: LEDC speed mode (default: LEDC_LOW_SPEED_MODE)
- `RGB_LEDC_RESOLUTION`: PWM resolution in bits (default: LEDC_TIMER_8_BIT)
- `RGB_LEDC_FREQUENCY_HZ`: PWM frequency in Hz (default: 5000)
- `RGB_CHANNEL_R/G/B`: LEDC channel assignments (default: R=CHANNEL_1, G=CHANNEL_0, B=CHANNEL_2)
- `RGB_GPIO_R/G/B`: GPIO pins for Red, Green, Blue (default: R=GPIO 1, G=GPIO 0, B=GPIO 2)

**Note**: Both LED drivers can be enabled simultaneously. The WS2812 driver is used for mesh status indicators, while the RGB LED driver provides additional visual feedback when `RGB_ENABLE` is defined.

## Usage

### Network Setup

1. **Root Node**: Flash firmware to one ESP32 device
   - This node will connect to your WiFi router
   - Once connected, it will obtain an IP address
   - The web server will start automatically

2. **Mesh Nodes**: Flash the same firmware to additional ESP32 devices
   - These nodes will automatically join the mesh network
   - They will connect to the root node or other mesh nodes
   - No additional configuration needed (uses same Mesh ID)

### Web Interface

The root node hosts a web server accessible at `http://<root-node-ip>/`:

- **Node Count**: Displays the number of nodes in the mesh network
- **Heartbeat Counter**: Live updates showing heartbeat messages sent
- **Color Display**: Shows current LED color (or default blue if not set)
- **Color Picker**: Click the color box to open a color wheel
- **Apply Color**: Sends the selected RGB color to all mesh nodes

### LED Visual Indicators

#### Heartbeat Response
- **Root node**: Green (on) / Off (off) based on heartbeat parity
- **Non-root nodes**: Blue (on) / Off (off) based on heartbeat parity, or last RGB color if set

#### Connection States (Layer-Based)
- **Layer 1**: Pink (RGB: 155, 0, 155) - Direct child of root
- **Layer 2**: Yellow (RGB: 155, 155, 0)
- **Layer 3**: Red (RGB: 155, 0, 0)
- **Layer 4**: Blue (RGB: 0, 0, 155)
- **Layer 5**: Green (RGB: 0, 155, 0)
- **Layer 6+**: Warning/White (RGB: 155, 155, 155)
- **Disconnected**: Warning/White (RGB: 155, 155, 155)
- **Initial**: Cyan (RGB: 0, 155, 155)

### Serial Monitoring

The firmware includes extensive serial logging:
- Startup information (node type, IP address, heap size)
- Root node actions (heartbeat sends)
- Non-root node actions (heartbeat receives, color changes)
- Status changes (layer changes, root switches)

Monitor at 115200 baud to see real-time device activity.

## Project Structure

```
lyktparad-espidf/
├── include/
│   ├── mesh_config.h.example      # Mesh configuration template
│   ├── mesh_device_config.h.example  # Device configuration template
│   ├── light_neopixel.h           # WS2812/Neopixel LED control API
│   ├── light_common_cathode.h     # Common-cathode RGB LED control API
│   └── mesh_web.h                 # Web server API
├── src/
│   ├── mesh.c                     # Core mesh network logic
│   ├── light_neopixel.c           # WS2812/Neopixel LED driver implementation
│   ├── light_common_cathode.c     # Common-cathode RGB LED driver implementation
│   ├── mesh_web.c                 # Web server implementation
│   └── CMakeLists.txt             # Build configuration
├── platformio.ini                 # PlatformIO project configuration
├── PROJECT_OUTLINE.md             # Detailed project documentation
└── README.md                      # This file
```

## Communication Protocol

### Message Types

- **`MESH_CMD_HEARTBEAT` (0x01)**: Heartbeat message from root node
  - Format: `[0x01][4-byte counter]`
  - Sent every 500ms to all nodes

- **`MESH_CMD_LIGHT_ON_OFF` (0x02)**: Light on/off control
  - Format: `[0x02][token_id][token_value][on/off]`
  - Requires authentication tokens

- **`MESH_CMD_SET_RGB` (0x03)**: RGB color control
  - Format: `[0x03][R][G][B]`
  - Sets LED color to specified RGB values

### Heartbeat System

- Root node sends heartbeat counter every 500ms
- Even heartbeats: LED off
- Odd heartbeats: LED on (with color based on state)
- Counter increments with each heartbeat

## Troubleshooting

### Device won't connect to mesh
- Verify `MESH_CONFIG_MESH_ID` matches across all nodes
- Check `MESH_CONFIG_MESH_AP_PASSWORD` is correct
- Ensure WiFi router is accessible (for root node)
- Check serial output for error messages

### LED not working

**For WS2812/Neopixel LEDs:**
- Verify GPIO pin in `mesh_device_config.h` matches hardware
- Check LED power supply (WS2812 requires 5V)
- Verify data line connection
- Check serial output for initialization errors

**For common-cathode RGB LEDs:**
- Verify `RGB_ENABLE` is uncommented in `mesh_device_config.h`
- Check that GPIO pins match your hardware connections
- Verify LED polarity (must be common-cathode, not common-anode)
- Ensure GPIO pins support LEDC (check ESP32 datasheet)
- Check LED power supply (typically 3.3V or 5V depending on LED)
- Verify PWM frequency and resolution settings are appropriate
- Check serial output for initialization errors

### Web interface not accessible
- Ensure device is root node (check serial output)
- Verify root node has obtained IP address
- Check firewall settings on your network
- Try accessing via IP address shown in serial output

### Build errors
- Ensure PlatformIO is installed and up to date
- Check that all example config files are copied to actual config files
- Verify ESP-IDF framework version compatibility
- Check serial output for runtime errors

## Development

### Building in VS Code

This project uses PlatformIO and **must be built within VS Code**. The build process:
1. Compiles ESP-IDF components
2. Links managed components (led_strip for WS2812 support)
3. Generates partition table
4. Creates firmware binary

**LED Driver Selection:**
- WS2812/Neopixel driver (`light_neopixel.c`) is always compiled and active
- Common-cathode RGB LED driver (`light_common_cathode.c`) is always compiled but only active when `RGB_ENABLE` is defined
- Both drivers can operate simultaneously if both LED types are connected

### Adding New Features

- **New message types**: Add command prefix in `include/light_neopixel.h` and handle in `src/mesh.c` and `src/light_neopixel.c`
- **New LED patterns**: Extend `src/light_neopixel.c` with new color functions
- **Web API endpoints**: Add handlers in `src/mesh_web.c` and register in `mesh_web_server_start()`

## License

Copyright (c) 2025 the_louie

## Acknowledgments

- **ESP-IDF**: Espressif IoT Development Framework
- **ESP-MESH**: Espressif's WiFi mesh networking solution
- **led_strip component**: Espressif managed component for WS2812/Neopixel control

