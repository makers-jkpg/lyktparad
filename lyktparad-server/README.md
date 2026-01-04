# lyktparad-server

Optional external web server for the lyktparad mesh network control interface.

## Overview

This is an **optional** external web server that hosts the web UI and provides a foundation for future API proxying. The server is an enhancement that provides a better UI experience, but **mesh devices MUST function completely independently without it**. The root node's embedded web server continues to run and provide full functionality regardless of this external server's status.

## Technology Stack

- **Node.js** - JavaScript runtime
- **Express.js** - Web application framework
- **CORS** - Cross-Origin Resource Sharing middleware
- **Bonjour** - mDNS/Bonjour service advertisement (optional)

## Installation

1. Ensure Node.js is installed (version 14 or higher recommended)
2. Navigate to the `lyktparad-server` directory
3. Install dependencies:

```bash
npm install
```

## Running the Server

Start the server using npm:

```bash
npm start
```

Or run directly with Node.js:

```bash
node server.js
```

The server will start on port 8080 by default, or the port specified by the `PORT` environment variable.

### Port Configuration

Set the `PORT` environment variable to use a different port:

```bash
# Linux/macOS
PORT=3000 npm start

# Windows
set PORT=3000 && npm start
```

### UDP Port Configuration

The server advertises a UDP port for ESP32 root node communication via mDNS. Set the `UDP_PORT` environment variable to use a different UDP port (default: 8081):

```bash
# Linux/macOS
UDP_PORT=9000 npm start

# Windows
set UDP_PORT=9000 && npm start
```

## mDNS Service Discovery

The server advertises itself on the local network using mDNS/Bonjour with the service type `_lyktparad-web._tcp`. This enables zero-configuration discovery by ESP32 root nodes.

### Service Information

- **Service Type**: `_lyktparad-web._tcp`
- **Service Name**: "Lyktparad Web Server"
- **HTTP Port**: 8080 (or value from `PORT` environment variable)
- **UDP Port**: 8081 (or value from `UDP_PORT` environment variable)

### TXT Records

The service includes the following TXT records:
- `version`: Server version (e.g., "1.0.0")
- `protocol`: "udp" (indicates UDP protocol for root node communication)
- `udp_port`: UDP port number for root node communication

### Graceful Degradation

If mDNS is unavailable or fails to initialize, the server continues to run normally. ESP32 devices can still connect via IP address. mDNS is purely a convenience feature for automatic discovery.

## Endpoints

### Static Files

- **GET /** - Serves `web-ui/index.html` (main web UI)
- **GET /css/styles.css** - CSS stylesheet
- **GET /js/app.js** - JavaScript application code

### Health Check

- **GET /health** - Returns server status information

Example response:
```json
{
  "status": "ok",
  "server": {
    "port": 8080,
    "uptime": 1234,
    "timestamp": "2025-01-03T15:26:00.000Z"
  }
}
```

## Directory Structure

```
lyktparad-server/
├── server.js          # Main server file
├── mdns.js            # mDNS service advertisement module
├── package.json       # Node.js dependencies
├── .gitignore        # Git ignore file
├── routes/           # Future API routes (placeholder)
├── web-ui/           # Static web UI files
│   ├── index.html   # Main HTML page
│   ├── css/
│   │   └── styles.css
│   ├── js/
│   │   ├── app.js
│   │   ├── ota-api.js
│   │   └── ota-ui.js
│   └── API.md        # API documentation
└── README.md         # This file
```

## Configuration

### CORS

CORS is configured to allow all origins for development. This should be restricted in production environments.

Current CORS settings:
- **Origin**: `*` (allows all origins)
- **Methods**: `GET`, `POST`, `OPTIONS`
- **Headers**: `Content-Type`

### Static File Serving

Static files are served from the `web-ui/` directory. The server uses Express's `express.static()` middleware to serve files, with `index.html` automatically served at the root path `/`.

## Future Expansion

This server provides the foundation for future features:

- **API Proxy Layer** - Proxy API requests to ESP32 root node
- **mDNS Discovery** - Automatic discovery of ESP32 root nodes
- **Mesh Command Bridge** - Forward commands to mesh network
- **State Synchronization** - Periodic state updates from root node

The `routes/` directory is reserved for future API route implementations.

## Development Notes

- **Standalone Operation**: Server can run without ESP32 connection initially
- **Optional Infrastructure**: ESP32 devices work perfectly without this server
- **Simple Focus**: Current implementation focuses on infrastructure only; API proxying will be implemented in future tasks

## Security Considerations

- **CORS**: Currently configured for development (allows all origins). Should be restricted in production.
- **Static Files**: Only files in `web-ui/` directory are served. No path traversal vulnerabilities.
- **Error Handling**: Errors don't expose sensitive information or file system structure.

## Troubleshooting

### Port Already in Use

If you see an error about the port being in use, set a different port using the `PORT` environment variable:

```bash
PORT=3000 npm start
```

### Files Not Loading

Ensure that:
1. The `web-ui/` directory exists with all required files
2. File paths in `index.html` are relative (e.g., `css/styles.css`, `js/app.js`)
3. The server is running and accessible

### mDNS Not Working

If mDNS service discovery is not working:

1. **Check bonjour package**: Ensure `bonjour` package is installed:
   ```bash
   npm install bonjour
   ```

2. **Check permissions**: On some systems, mDNS requires network permissions. Check system firewall settings.

3. **Check network**: Ensure you're on a network that supports mDNS (most local networks do).

4. **Fallback**: The server works perfectly without mDNS. ESP32 devices can always connect via IP address.

5. **Check logs**: Look for mDNS-related warnings in the server console output.

## License

Copyright (c) 2025 the_louie
