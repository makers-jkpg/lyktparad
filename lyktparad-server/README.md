# lyktparad-server

Optional external web server for the lyktparad mesh network control interface.

## Overview

This is an **optional** external web server that hosts the web UI and provides a foundation for future API proxying. The server is an enhancement that provides a better UI experience, but **mesh devices MUST function completely independently without it**. The root node's embedded web server continues to run and provide full functionality regardless of this external server's status.

## Technology Stack

- **Node.js** - JavaScript runtime
- **Express.js** - Web application framework
- **CORS** - Cross-Origin Resource Sharing middleware

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

## License

Copyright (c) 2025 the_louie
