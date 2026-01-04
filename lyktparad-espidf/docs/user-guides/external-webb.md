# External Web Server - User Guide

**Last Updated:** 2026-01-04

## Table of Contents

1. [Introduction](#introduction)
2. [What is the External Web Server?](#what-is-the-external-web-server)
3. [Prerequisites](#prerequisites)
4. [Step-by-Step Setup Guide](#step-by-step-setup-guide)
5. [Understanding Root Nodes and Mesh Nodes](#understanding-root-nodes-and-mesh-nodes)
6. [Communication and Discovery](#communication-and-discovery)
7. [Registration and Heartbeat](#registration-and-heartbeat)
8. [Periodic State Updates](#periodic-state-updates)
9. [Using the Web Interface](#using-the-web-interface)
10. [OTA Updates via External Server](#ota-updates-via-external-server)
11. [Connection Status](#connection-status)
12. [Troubleshooting](#troubleshooting)
13. [FAQ](#faq)

## Introduction

The External Web Server is an **optional** enhancement that provides a better web interface experience for controlling your mesh network. It's completely optional - your mesh network works perfectly fine without it, using the root node's built-in web server. However, the external server offers some advantages:

- **Better UI experience** - Enhanced web interface with more features
- **Easier access** - Access from any device on your network without needing the root node's IP
- **Automatic discovery** - Root nodes can automatically find and connect to the server
- **Centralized control** - Manage your mesh network from one central location
- **OTA updates** - Convenient firmware update interface (see [OTA Updates](#ota-updates-via-external-server))

> **Important**: The external web server is completely optional. Your ESP32 mesh devices will continue to work normally even if the server is unavailable. The root node's embedded web server always runs on port 80 and provides full functionality.

## What is the External Web Server?

The External Web Server is a Node.js-based web server that runs on your computer or network device (like a Raspberry Pi). It provides:

1. **Web UI Hosting** - Serves an enhanced web interface for controlling your mesh network
2. **API Proxy** - Proxies commands from the web UI to ESP32 root nodes via UDP
3. **Automatic Discovery** - Advertises itself on the network so root nodes can find it automatically
4. **State Monitoring** - Receives periodic updates from root nodes about mesh network status
5. **Connection Management** - Tracks which root nodes are connected and monitors their health

The server runs independently and communicates with ESP32 root nodes via UDP protocol. It does NOT interfere with the root node's built-in web server - both can run simultaneously.

## Prerequisites

Before setting up the external web server, ensure you have:

1. **Node.js installed** - Version 14 or higher (download from [nodejs.org](https://nodejs.org/))
2. **Network access** - Server must be on the same network as your ESP32 devices
3. **Mesh network running** - At least one ESP32 root node should be active on the mesh network
4. **Firewall configured** - Ports 8080 (HTTP) and 8081 (UDP) should be accessible on your local network

### Checking Node.js Installation

Open a terminal/command prompt and check:

```bash
node --version
npm --version
```

Both commands should show version numbers. If not, install Node.js from [nodejs.org](https://nodejs.org/).

## Step-by-Step Setup Guide

### Step 1: Install Dependencies

1. Navigate to the `lyktparad-server` directory in a terminal/command prompt:

```bash
cd lyktparad-server
```

2. Install required dependencies:

```bash
npm install
```

This will download and install Express.js, CORS middleware, and Bonjour (for mDNS service discovery). The installation may take a minute or two.

**Troubleshooting**: If you see errors during installation:
- Ensure you have internet connection
- Check Node.js version is 14 or higher (`node --version`)
- On some systems, you may need administrator/sudo privileges

### Step 2: Start the Server

Start the server using npm:

```bash
npm start
```

Or run directly with Node.js:

```bash
node server.js
```

**Expected Output**:
```
lyktparad-server listening on port 8080
Web UI available at http://localhost:8080
Health check available at http://localhost:8080/health
UDP server listening on port 8081
```

The server is now running and ready to accept connections from ESP32 root nodes.

### Step 3: Verify Server is Running

Open your web browser and navigate to:

```
http://localhost:8080
```

You should see the mesh network control interface. If you see the interface, the server is working correctly.

You can also check the health endpoint:

```
http://localhost:8080/health
```

This should return a JSON response showing server status.

### Step 4: Configure Ports (Optional)

By default, the server uses:
- **Port 8080** for HTTP (web interface)
- **Port 8081** for UDP (ESP32 communication)

If these ports are already in use, you can change them using environment variables:

**Linux/macOS**:
```bash
PORT=3000 UDP_PORT=9000 npm start
```

**Windows Command Prompt**:
```cmd
set PORT=3000 && set UDP_PORT=9000 && npm start
```

**Windows PowerShell**:
```powershell
$env:PORT=3000; $env:UDP_PORT=9000; npm start
```

### Step 5: Access from Other Devices

Once the server is running, you can access it from any device on your local network:

1. Find your computer's IP address:
   - **Linux/macOS**: Run `ifconfig` or `ip addr`
   - **Windows**: Run `ipconfig`

2. Access from another device using:
   ```
   http://<your-computer-ip>:8080
   ```

For example, if your computer's IP is `192.168.1.100`, use:
```
http://192.168.1.100:8080
```

### Step 6: Automatic Startup (Optional)

To have the server start automatically when your computer boots:

**Linux (systemd)**:
Create `/etc/systemd/system/lyktparad-server.service`:
```ini
[Unit]
Description=Lyktparad External Web Server
After=network.target

[Service]
Type=simple
User=your-username
WorkingDirectory=/path/to/lyktparad-server
ExecStart=/usr/bin/node server.js
Restart=always

[Install]
WantedBy=multi-user.target
```

Then enable and start:
```bash
sudo systemctl enable lyktparad-server
sudo systemctl start lyktparad-server
```

**Windows (Task Scheduler)**:
1. Open Task Scheduler
2. Create Basic Task
3. Set trigger to "When the computer starts"
4. Set action to start a program: `node.exe`
5. Add arguments: `server.js`
6. Set start in: path to `lyktparad-server` directory

**macOS (LaunchAgent)**:
Create `~/Library/LaunchAgents/com.lyktparad.server.plist`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.lyktparad.server</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/node</string>
        <string>/path/to/lyktparad-server/server.js</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
```

Then load:
```bash
launchctl load ~/Library/LaunchAgents/com.lyktparad.server.plist
```

## Understanding Root Nodes and Mesh Nodes

### Root Node

The **root node** is the ESP32 device that connects directly to your Wi-Fi router. It acts as the gateway between the mesh network and your local network/internet.

**Characteristics**:
- Has direct internet access via Wi-Fi router
- Runs a built-in web server on port 80
- Can download firmware updates from the internet
- Can register with external web servers
- Distributes firmware to other mesh nodes
- Coordinates mesh network operations

**Access**:
- Built-in web server: `http://<root-node-ip>` (port 80)
- External web server: Automatically connects if available
- Serial console: 115200 baud for debugging

### Mesh Nodes (Leaf Nodes)

**Mesh nodes** (also called leaf nodes) are ESP32 devices that connect to the mesh network through the root node or other mesh nodes. They don't have direct internet access.

**Characteristics**:
- Connect to mesh network only (no direct Wi-Fi)
- Receive firmware updates from root node
- Follow root node's commands
- Report status to root node
- Cannot register with external web server directly

**Access**:
- Controlled via root node
- Serial console: 115200 baud for debugging
- No direct web interface (all control goes through root node)

### Communication Flow

```
┌─────────────────┐
│  Your Computer  │
│  (Web Browser)  │
└────────┬────────┘
         │
         │ HTTP
         ▼
┌─────────────────┐
│ External Server │
│  (Port 8080)    │
└────────┬────────┘
         │
         │ UDP (Port 8081)
         ▼
┌─────────────────┐
│   Root Node     │
│  (ESP32)        │
│  - Web Server   │
│  - Mesh Gateway │
└────────┬────────┘
         │
         │ Mesh Network
         ▼
┌─────────────────┐
│  Mesh Nodes     │
│  (ESP32)        │
│  - Leaf Nodes   │
└─────────────────┘
```

## Communication and Discovery

### How Root Nodes Find the Server

ESP32 root nodes use two methods to discover the external web server:

#### Method 1: mDNS Discovery (Primary)

**What it is**: mDNS (multicast DNS) is a zero-configuration networking protocol that allows devices to find services automatically on the local network.

**How it works**:
1. External server advertises itself using mDNS service type `_lyktparad-web._tcp`
2. Root nodes query the network for this service type
3. When found, root nodes receive server IP address and port information
4. Root nodes automatically connect to the discovered server

**Advantages**:
- Zero configuration - no IP addresses needed
- Automatic discovery
- Works on most local networks

**Requirements**:
- Network must support mDNS (most home/office networks do)
- Server must have Bonjour/mDNS support (included with server installation)

#### Method 2: UDP Broadcast (Runtime Fallback)

**What it is**: UDP broadcast is used as a runtime fallback when mDNS discovery fails (server not found). The server broadcasts its location via UDP packets on port 5353.

**How it works**:
1. External server sends UDP broadcast packets every 30 seconds
2. Root nodes listen for these broadcasts (UDP broadcast listener runs in background)
3. When a broadcast is received, root nodes extract server information
4. Root nodes connect to the discovered server

**Advantages**:
- Works when mDNS discovery fails (server not found)
- Simple and reliable
- Works across most network configurations

**When used**:
- If mDNS discovery fails or times out (server not found)
- As a runtime fallback mechanism

### Discovery Priority

Root nodes use mDNS as the primary discovery method, with UDP broadcast as a runtime fallback:
1. **mDNS tried first** - mDNS discovery is the primary method and is attempted first via discovery task
2. **UDP broadcast as fallback** - If mDNS discovery fails (server not found), UDP broadcast listener (running in background) will discover the server
3. **No configuration needed** - All discovery happens automatically

### Manual Connection (IP Address)

If automatic discovery isn't working, you can configure root nodes to connect to a specific IP address:
1. Connect to root node via serial console (115200 baud)
2. Root node logs will show discovery status
3. Manual configuration can be set in firmware configuration (advanced users only)

## Registration and Heartbeat

### Root Node Registration

When a root node discovers the external web server, it **registers** itself with the server.

**What happens during registration**:
1. Root node sends registration packet to external server
2. Packet contains:
   - Root node IP address
   - Mesh network ID
   - Node count (number of nodes in mesh)
   - Firmware version
   - Timestamp
3. Server acknowledges registration
4. Server stores root node information
5. Server starts monitoring root node health

**Registration timing**:
- Happens automatically after discovery
- Repeats if registration fails (with backoff)
- Re-registers if server connection is lost and restored

**Benefits of registration**:
- Server knows which root nodes are available
- Server can track mesh network status
- Server can route API requests to correct root nodes
- Server can detect when root nodes go offline

### Heartbeat Messages

After registration, root nodes send **heartbeat messages** to the external server to indicate they're still online and connected.

**Heartbeat characteristics**:
- **Frequency**: Every 10 seconds
- **Purpose**: Indicate root node is alive and connected
- **Automatic**: Sends automatically once registered
- **Lightweight**: Small UDP packets (minimal network usage)

**What the server does**:
- Records last heartbeat timestamp for each root node
- Detects if heartbeat stops (indicating disconnection)
- Marks root nodes as offline after 30 seconds of no heartbeat
- Updates connection status for web UI

**Benefits**:
- Real-time connection status
- Automatic disconnection detection
- Helps troubleshoot connection issues
- Ensures server only routes requests to online root nodes

### IP Address Caching

Root nodes cache the external server's IP address in non-volatile storage (NVS).

**Why caching helps**:
- Faster reconnection after reboot
- Works even if discovery fails temporarily
- Reduces network discovery traffic
- More reliable connection

**How it works**:
1. First time: Root node discovers server via mDNS or UDP broadcast
2. Cache saved: Server IP and port saved to NVS
3. On reboot: Root node checks cache first
4. If cached IP works: Connects immediately (skips discovery)
5. If cached IP fails: Falls back to discovery methods

**Cache persistence**:
- Survives reboots
- Survives power cycles
- Only cleared if manually cleared or firmware update

**When cache is updated**:
- After successful registration
- If discovery finds a different server IP
- When connection is restored after failure

### Registration Lifecycle

```
Root Node Boot
     │
     ▼
Embedded Web Server Starts (Port 80)
     │
     ▼
Discovery Task Starts
     │
     ├──► Try mDNS Discovery (20s timeout)
     │
     └──► Try UDP Broadcast (parallel)
     │
     ▼
Server Found?
     │
     ├──► Yes ──► Register with Server
     │           │
     │           ├──► Save IP to Cache
     │           │
     │           └──► Start Heartbeat (every 10s)
     │
     └──► No ──► Use Cached IP (if available)
                 │
                 ├──► Cached IP Works? ──► Register
                 │
                 └──► Cached IP Fails? ──► Continue without external server
                                          (embedded server still works)
```

## Periodic State Updates

Root nodes send **periodic state updates** to the external server containing information about the mesh network.

### What Information is Sent

State updates include:
- **Mesh network status** - Active nodes, total nodes
- **Node list** - List of all connected mesh nodes
- **Route table** - Network routing information
- **Connection status** - Which nodes are connected
- **Timestamp** - When the state was collected

### Update Frequency

- **Interval**: Every 3 seconds (approximately)
- **Automatic**: Sends automatically once registered
- **Fire-and-forget**: No acknowledgment required (lightweight)

### How Server Uses State Updates

The external server:
1. **Stores state** - Keeps the most recent state update for each root node
2. **Provides API** - Web UI can query `/api/mesh/state` to get current mesh status
3. **Displays in UI** - Shows node count, connection status, etc.
4. **Updates OTA UI** - OTA update interface can show which nodes need updates

### State Update Flow

```
Root Node
     │
     ├──► Every 3 seconds
     │
     ├──► Collect mesh network state
     │    - Node count
     │    - Route table
     │    - Connection status
     │
     ├──► Build UDP packet
     │
     └──► Send to External Server (fire-and-forget)
           │
           ▼
External Server
     │
     ├──► Receive state update
     │
     ├──► Store in memory
     │
     └──► Serve via API (/api/mesh/state)
           │
           ▼
Web UI
     │
     └──► Display mesh status
```

## Using the Web Interface

### Accessing the Web Interface

1. **From the same computer**: Open `http://localhost:8080`
2. **From another device**: Open `http://<server-ip>:8080`
   - Replace `<server-ip>` with the IP address of the computer running the server

### Web Interface Features

The web interface provides:

1. **Mesh Network Status**
   - Node count display
   - Connection status indicator
   - Root node status

2. **Grid Control**
   - Visual grid interface for creating color sequences
   - Click squares to set colors
   - Row and column operations

3. **Color Picker**
   - Set RGB color values
   - Color display
   - Apply to mesh network

4. **Sequence Control**
   - Create and edit sequences
   - Set playback tempo
   - Start/stop/reset sequences
   - CSV import/export

5. **OTA Updates** (see [OTA Updates via External Server](#ota-updates-via-external-server))
   - Firmware update interface
   - Progress tracking
   - Status display

### Connection Status Indicator

The web interface shows connection status:
- **🟢 Green**: Root node is connected and online
- **🟡 Yellow**: Root node was connected but heartbeat stopped
- **🔴 Red**: No root node is registered or all are offline
- **⚪ Gray**: External server not connected to any root nodes

The status updates automatically as root nodes connect/disconnect.

## OTA Updates via External Server

The external web server provides a convenient interface for performing Over-The-Air (OTA) firmware updates on your mesh network.

> **For detailed OTA documentation**: See [OTA Updates User Guide](ota-updates.md) for comprehensive information about the OTA update process, including troubleshooting, FAQ, and advanced features.

### Accessing OTA Interface

The OTA update interface is integrated into the main web interface:

1. Open the web interface: `http://localhost:8080` (or server IP)
2. Scroll to the "Firmware Update" section
3. You'll see:
   - Current firmware version
   - URL input field for firmware file
   - Action buttons (Check, Start, Cancel)
   - Progress bars for download and distribution
   - Status messages

### OTA Update Process via External Server

The external server acts as a proxy between the web UI and ESP32 root node:

1. **Web UI** → Sends HTTP request to external server
2. **External Server** → Converts HTTP to UDP and sends to root node
3. **Root Node** → Processes OTA command and sends response
4. **External Server** → Converts UDP response to HTTP
5. **Web UI** → Receives response and updates display

This allows you to use the enhanced web UI while commands are still processed by the ESP32 root node.

### Quick Start Guide

**Step 1: Check Current Version**
- Current firmware version is displayed automatically
- Version is fetched from root node via API

**Step 2: Enter Firmware URL**
- Enter HTTP or HTTPS URL to firmware binary
- URL is validated automatically
- Example: `https://example.com/firmware-v1.2.3.bin`

**Step 3: Start Update**
1. Click "Start Update" button
2. Confirm the update (optional dialog)
3. Download progress is shown in real-time
4. After download completes, distribution starts automatically
5. Distribution progress is shown with node status

**Step 4: Monitor Progress**
- Download progress bar shows percentage (0-100%)
- Distribution progress bar shows percentage and node status
- Status messages provide updates
- Progress updates every 1-2 seconds automatically

**Step 5: Complete Update**
- After distribution completes, initiate coordinated reboot
- All nodes reboot simultaneously
- Nodes switch to new firmware
- Network reconnects with new firmware

### OTA Features via External Server

**Real-Time Progress Tracking**:
- Download progress updates every 1-2 seconds
- Distribution progress shows per-node status
- Visual progress bars with percentages
- Status messages for each stage

**Error Handling**:
- User-friendly error messages
- Network error detection
- Root node disconnection detection
- Retry functionality

**Status Display**:
- Current firmware version
- Download status (downloading, complete, failed)
- Distribution status (distributing, complete, failed)
- Per-node update status
- Connection status indicator

### OTA Commands Available

All OTA commands work through the external server:
- `GET /api/ota/version` - Get current firmware version
- `POST /api/ota/download` - Start firmware download
- `GET /api/ota/status` - Get download status and progress
- `POST /api/ota/cancel` - Cancel ongoing download
- `POST /api/ota/distribute` - Start firmware distribution
- `GET /api/ota/distribution/status` - Get distribution status
- `POST /api/ota/distribution/cancel` - Cancel distribution
- `POST /api/ota/reboot` - Initiate coordinated reboot

> **Note**: The external server proxies these commands to the ESP32 root node. The root node processes all OTA operations. See [OTA Updates User Guide](ota-updates.md) for complete documentation.

## Connection Status

### Understanding Connection Status

The external server tracks connection status for each registered root node:

**Online (Connected)**:
- Root node is registered
- Heartbeat received within last 10 seconds
- API requests will be routed to this root node
- Status indicator: 🟢 Green

**Offline (Disconnected)**:
- No heartbeat received for 30+ seconds
- Root node may be powered off or network disconnected
- API requests will fail with "503 Service Unavailable"
- Status indicator: 🔴 Red

**Stale (Connection Lost)**:
- Heartbeat stopped but within timeout window
- Root node may be temporarily unreachable
- API requests may fail or timeout
- Status indicator: 🟡 Yellow

### Connection Status API

You can check connection status via API:

```bash
curl http://localhost:8080/api/mesh/state
```

Response:
```json
{
  "connected": true,
  "root_node": {
    "ip": "192.168.1.100",
    "mesh_id": "...",
    "node_count": 5,
    "firmware_version": "1.0.0",
    "last_heartbeat": "2025-01-15T12:34:56.789Z",
    "last_state_update": "2025-01-15T12:34:53.789Z"
  }
}
```

### Disconnection Detection

The server automatically detects when root nodes disconnect:

1. **Heartbeat Monitoring** - Tracks last heartbeat timestamp
2. **Timeout Detection** - Marks offline after 30 seconds without heartbeat
3. **API Error Detection** - Detects UDP communication failures
4. **Status Updates** - Web UI automatically reflects disconnection

**What happens on disconnection**:
- Root node marked as offline
- API requests return "503 Service Unavailable"
- Web UI shows connection status as offline
- Server continues running (ready for reconnection)
- Root node automatically re-registers when reconnected

## Troubleshooting

### Server Won't Start

**Problem**: Server fails to start or exits immediately.

**Solutions**:
1. **Check Node.js version**: `node --version` should show 14 or higher
2. **Check port availability**: Port 8080 may be in use
   - Change port: `PORT=3000 npm start`
   - Or stop the application using port 8080
3. **Check dependencies**: Run `npm install` to ensure all packages are installed
4. **Check file permissions**: Ensure you have read/write permissions in the `lyktparad-server` directory
5. **Check logs**: Look for error messages in the console output

### Port Already in Use

**Problem**: Error message "Port 8080 is already in use".

**Solutions**:
- **Change port**: Use a different port
  ```bash
  PORT=3000 npm start
  ```
- **Find and stop**: Find what's using port 8080 and stop it
  - Linux/macOS: `lsof -i :8080` then `kill <PID>`
  - Windows: `netstat -ano | findstr :8080` then `taskkill /PID <PID> /F`

### Root Nodes Don't Connect

**Problem**: Root nodes don't discover or connect to the external server.

**Solutions**:
1. **Check network**: Ensure server and root nodes are on the same network
2. **Check firewall**: Ensure ports 8080 (HTTP) and 8081 (UDP) are not blocked
3. **Check mDNS**: If mDNS isn't working:
   - Server will still work (uses UDP broadcast fallback)
   - Check server logs for mDNS warnings
4. **Check server logs**: Look for registration attempts in console output
5. **Check root node logs**: Connect to root node serial console (115200 baud) and look for discovery/registration messages
6. **Wait for discovery**: Discovery can take 20-30 seconds, wait before troubleshooting

### Web Interface Not Loading

**Problem**: Browser shows "can't reach this page" or similar error.

**Solutions**:
1. **Check server is running**: Verify server started successfully (look for "listening on port 8080" message)
2. **Check URL**: Ensure you're using correct port (default: 8080)
3. **Check firewall**: Ensure local firewall allows connections on port 8080
4. **Try localhost**: If accessing from same computer, use `http://localhost:8080`
5. **Try IP address**: If accessing from another device, use `http://<server-ip>:8080`

### API Requests Fail

**Problem**: Web interface shows errors or API requests fail.

**Solutions**:
1. **Check root node connection**: Verify root node is registered and online
2. **Check connection status**: Look at connection status indicator in web UI
3. **Check root node IP**: Verify root node is accessible via its embedded web server
4. **Check UDP port**: Ensure port 8081 (UDP) is not blocked by firewall
5. **Check server logs**: Look for error messages in console output
6. **Retry**: Some operations may fail temporarily, try again

### Heartbeat Stopped

**Problem**: Root node was connected but heartbeat stopped (status shows offline).

**Solutions**:
1. **Check root node power**: Verify root node is powered on
2. **Check network connection**: Verify root node has network connectivity
3. **Check UDP port**: Ensure port 8081 (UDP) is accessible
4. **Check root node logs**: Connect to serial console and look for heartbeat errors
5. **Wait for reconnection**: Root node will automatically try to reconnect
6. **Restart root node**: Power cycle the root node if needed

### State Updates Not Received

**Problem**: Mesh network status doesn't update in web UI.

**Solutions**:
1. **Check root node connection**: Ensure root node is registered and online
2. **Check state API**: Try accessing `/api/mesh/state` directly
3. **Wait for updates**: State updates are sent every 3 seconds, wait a few seconds
4. **Check root node logs**: Verify root node is sending state updates
5. **Refresh web UI**: Try refreshing the browser page

### OTA Updates Not Working

**Problem**: OTA update interface doesn't work or shows errors.

**Solutions**:
1. **Check root node connection**: Verify root node is online (green status indicator)
2. **Check root node logs**: Connect to serial console and look for OTA errors
3. **Check firmware URL**: Ensure URL is accessible from root node's network
4. **See OTA guide**: Refer to [OTA Updates User Guide](ota-updates.md) for detailed troubleshooting
5. **Try direct access**: Try accessing root node's embedded web server directly for OTA operations

## FAQ

### Q: Is the external web server required?

A: No, the external web server is completely optional. ESP32 mesh devices work perfectly without it. The root node's embedded web server always runs on port 80 and provides full functionality. The external server is an enhancement for better UI experience and centralized management.

### Q: Can I use both the embedded server and external server?

A: Yes! Both can run simultaneously without conflicts. The embedded server runs on port 80 (on the root node), and the external server runs on port 8080 (on your computer). You can access either one independently.

### Q: What happens if the external server goes down?

A: If the external server stops or becomes unavailable:
- ESP32 devices continue to work normally
- Root node's embedded web server continues to work
- Mesh network operations continue normally
- Root nodes will automatically reconnect when server comes back online

### Q: Do I need to configure root nodes to use the external server?

A: No configuration is needed! Root nodes automatically discover the external server using mDNS or UDP broadcast. If the server is available on the network, root nodes will find it and connect automatically.

### Q: Can multiple root nodes connect to one external server?

A: Yes, the external server can handle multiple root nodes. Each root node registers independently, and the server tracks them separately. The web UI can show status for all connected root nodes.

### Q: What ports does the external server use?

A: The server uses:
- **Port 8080** (HTTP) - Web interface and API
- **Port 8081** (UDP) - Communication with ESP32 root nodes
- **Port 5353** (UDP) - UDP broadcast for discovery (fallback method)

These ports can be changed using environment variables if needed.

### Q: Can I access the external server from outside my local network?

A: By default, the server only accepts connections from your local network. To access from outside:
1. Configure port forwarding on your router
2. Consider security implications (use HTTPS, authentication)
3. Ensure firewall rules allow external connections

For security reasons, external access is not recommended for production use without proper security measures.

### Q: How do I know if a root node is connected?

A: The web interface shows a connection status indicator:
- 🟢 Green = Connected and online
- 🟡 Yellow = Connection lost (within timeout)
- 🔴 Red = Offline or not registered
- ⚪ Gray = No root nodes registered

You can also check the `/api/mesh/state` endpoint or look at server logs.

### Q: How often do root nodes send heartbeat messages?

A: Root nodes send heartbeat messages every 10 seconds once registered. The server marks a root node as offline if no heartbeat is received for 30 seconds.

### Q: How often do root nodes send state updates?

A: Root nodes send state updates approximately every 3 seconds. These updates contain mesh network status, node count, and routing information.

### Q: Can I clear the root node's cached server IP?

A: Yes, but it requires accessing the root node via serial console or resetting NVS storage. In most cases, the cache will update automatically when a different server IP is discovered. The cache is only a performance optimization - root nodes can discover servers without it.

### Q: Does the external server store any data?

A: The server stores:
- Root node registration information (in memory, lost on restart)
- Last heartbeat timestamp (in memory)
- Last state update (in memory)
- Connection status (in memory)

No data is permanently stored on disk. All data is in-memory and cleared when the server restarts.

### Q: Can I run the external server on a Raspberry Pi?

A: Yes! The external server can run on any device with Node.js installed, including Raspberry Pi, other Linux systems, Windows, or macOS. Just install Node.js and run `npm install` and `npm start`.

### Q: How do I stop the external server?

A: In the terminal where the server is running, press `Ctrl+C` (or `Cmd+C` on macOS). The server will stop gracefully. If you set up automatic startup, you can stop it using your system's service management tools.

### Q: Why should I use the external server instead of the embedded server?

A: The external server offers:
- **Better UI experience** - Enhanced interface with more features
- **Easier access** - Access from any device without needing root node IP
- **OTA updates** - Convenient firmware update interface
- **Centralized management** - Manage multiple root nodes from one location
- **Automatic discovery** - Root nodes find server automatically

However, the embedded server works perfectly fine for basic operations. Choose based on your needs!

### Q: What should I do if mDNS doesn't work?

A: That's fine! The server automatically falls back to UDP broadcast discovery. mDNS is just a convenience feature - the server works perfectly without it. Root nodes will still discover the server using UDP broadcast on port 5353.

### Q: Can I change the discovery method?

A: The root node automatically tries both mDNS and UDP broadcast. There's no user configuration needed. If one method fails, the other is used automatically.

---

**For More Information**:
- **OTA Updates**: See [OTA Updates User Guide](ota-updates.md) for detailed OTA documentation
- **Developer Guides**: See [Developer Guides](../dev-guides/) for technical implementation details
- **Troubleshooting**: Check the [Troubleshooting](#troubleshooting) section above for common issues
