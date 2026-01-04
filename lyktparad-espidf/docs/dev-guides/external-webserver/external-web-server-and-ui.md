# External Web Server and UI - Development Guide

**Last Updated:** 2025-01-15

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Web UI Extraction](#web-ui-extraction)
4. [External Web Server Infrastructure](#external-web-server-infrastructure)
5. [OTA Web UI Features](#ota-web-ui-features)
6. [Static File Serving](#static-file-serving)
7. [CORS Configuration](#cors-configuration)
8. [API Proxy Integration](#api-proxy-integration)
9. [Error Handling and Graceful Degradation](#error-handling-and-graceful-degradation)
10. [Implementation Details](#implementation-details)
11. [API Reference](#api-reference)
12. [Integration Points](#integration-points)

## Overview

### Purpose

The External Web Server and UI system provides an optional external web server (`lyktparad-server`) that hosts the web UI and provides enhanced user experience features. The system consists of three main components:

1. **Web UI Extraction**: Extracts embedded HTML/CSS/JavaScript from the ESP32 root node's embedded web server into separate files for use by the external server.
2. **External Web Server Infrastructure**: Provides a Node.js/Express.js-based web server that serves static files, handles CORS, and provides a foundation for API proxying.
3. **OTA Web UI Features**: Implements a dedicated "Firmware Update" section with controls for initiating OTA updates, monitoring progress, and viewing update status for all mesh nodes.

The external web server is completely optional - ESP32 root nodes always run their embedded web server and can be accessed directly via their IP address. The external server provides a better user experience if available, but is never required for mesh functionality.

### Design Decisions

**Optional Infrastructure**: The external web server is completely optional. ESP32 devices must continue operating normally even if the external server is unavailable or communication fails. The embedded web server (`mesh_web_server_start()`) MUST ALWAYS run regardless of external server status.

**Separation of Concerns**: The embedded web UI remains unchanged in `mesh_web.c` and continues to work. The extraction creates separate files for use by the external server, allowing both to coexist.

**Static File Serving**: The external server serves static HTML, CSS, and JavaScript files from the `web-ui/` directory, providing a clean separation between frontend and backend code.

**CORS Configuration**: CORS is configured to allow all origins for development, enabling the web UI to work across different domains and network configurations. This should be restricted in production.

**API Proxy Layer**: The external server proxies API requests from the web UI to the ESP32 root node via UDP, enabling the web UI to work through the external server while maintaining compatibility with the embedded server.

**Real-Time Updates**: OTA progress tracking uses polling (1-2 second intervals) to provide real-time updates without requiring WebSocket infrastructure.

**User-Friendly Error Handling**: The OTA UI provides clear error messages, progress indicators, and retry functionality to guide users through the update process.

## Architecture

### Overall System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  ESP32 Root Node                                                 │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Embedded Web Server (MUST ALWAYS RUN)                   │  │
│  │  (mesh_web_server_start() on port 80)                    │  │
│  │                                                           │  │
│  │  ┌────────────────────────────────────────────────────┐  │  │
│  │  │  Embedded HTML/CSS/JavaScript                      │  │  │
│  │  │  (html_page[] in mesh_web.c)                       │  │  │
│  │  │  - Lines ~39-1590                                   │  │  │
│  │  │  - Unchanged and functional                         │  │  │
│  │  └────────────────────────────────────────────────────┘  │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  OTA API Endpoints (Already Implemented)                 │  │
│  │  - /api/ota/version                                      │  │
│  │  - /api/ota/download                                     │  │
│  │  - /api/ota/status                                       │  │
│  │  - /api/ota/cancel                                       │  │
│  │  - /api/ota/distribute                                   │  │
│  │  - /api/ota/distribution/status                          │  │
│  │  - /api/ota/distribution/progress                        │  │
│  │  - /api/ota/distribution/cancel                          │  │
│  │  - /api/ota/reboot                                       │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                        │
                        │ (Extraction Process)
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│  External Web Server (lyktparad-server)                          │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Static File Serving                                     │  │
│  │  - web-ui/index.html                                     │  │
│  │  - web-ui/css/styles.css                                 │  │
│  │  - web-ui/js/app.js                                      │  │
│  │  - web-ui/js/ota-api.js                                  │  │
│  │  - web-ui/js/ota-ui.js                                   │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  API Proxy Layer                                         │  │
│  │  - HTTP → UDP Translation                                │  │
│  │  - UDP → HTTP Translation                                │  │
│  │  - Routes: /api/*                                        │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  OTA UI Features                                         │  │
│  │  - Firmware Update Section                               │  │
│  │  - Progress Tracking (Polling)                           │  │
│  │  - Status Display                                        │  │
│  │  - Error Handling                                        │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                        │
                        │ HTTP Requests
                        │ /api/*
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│  Web UI (Browser)                                                │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Main UI (app.js)                                        │  │
│  │  - Grid Control                                          │  │
│  │  - Color Picker                                          │  │
│  │  - Sequence Control                                      │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  OTA UI (ota-ui.js)                                      │  │
│  │  - Version Display                                       │  │
│  │  - URL Input & Validation                                │  │
│  │  - Progress Bars                                         │  │
│  │  - Status Messages                                       │  │
│  │  - Node List                                             │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  OTA API (ota-api.js)                                    │  │
│  │  - API Functions                                         │  │
│  │  - Error Handling                                        │  │
│  │  - Request/Response Management                           │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Technology Stack

**Server Side**:
- **Node.js** - JavaScript runtime environment
- **Express.js** - Web application framework for serving static files and handling routes
- **CORS** - Cross-Origin Resource Sharing middleware for development
- **Bonjour** - mDNS/Bonjour service advertisement (optional)

**Client Side**:
- **HTML5** - Standard markup language
- **CSS3** - Styling with responsive design support
- **JavaScript (ES6+)** - Client-side logic and API integration

**Communication**:
- **HTTP** - Standard web protocol for client-server communication
- **UDP** - Lightweight protocol for server-to-ESP32 communication (via API proxy)

## Web UI Extraction

### Overview

The Web UI Extraction process extracts embedded HTML, CSS, and JavaScript from the ESP32 root node's embedded web server (`mesh_web.c`) into separate files for use by the external server. The embedded web UI remains unchanged and continues to function normally.

### Extraction Process

#### HTML Extraction

**Location in Source**: `lyktparad-espidf/src/mesh_web.c` (lines ~39-1590)

**Embedded HTML String**:
```c
static const char html_page[] =
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
// ... HTML content ...
"</html>";
```

**Extracted File**: `lyktparad-server/web-ui/index.html`

**Extraction Steps**:
1. Extract HTML content from `<html>` start tag to `</html>` end tag
2. Remove embedded `<style>` tag and CSS content
3. Replace with external CSS link: `<link rel="stylesheet" href="css/styles.css">`
4. Remove embedded `<script>` tag and JavaScript content
5. Replace with external JavaScript link: `<script src="js/app.js"></script>`
6. Preserve all HTML structure, IDs, classes, and attributes
7. Update any hardcoded paths to relative paths

#### CSS Extraction

**Location in Source**: Within `html_page[]` string, `<style>` tag (lines ~46-406)

**Embedded CSS**:
```html
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { /* ... */ }
/* ... CSS rules ... */
</style>
```

**Extracted File**: `lyktparad-server/web-ui/css/styles.css`

**Extraction Steps**:
1. Extract all CSS content from `<style>` start tag to `</style>` end tag
2. Remove `<style>` and `</style>` tags (keep only CSS content)
3. Format CSS with proper indentation
4. Organize CSS into logical sections:
   - Reset/Base styles
   - Layout styles
   - Component styles
   - Grid styles
   - Sequence control styles
   - OTA section styles
   - Responsive styles (media queries)
5. Preserve all CSS rules, selectors, and properties exactly
6. Add section comments for major CSS blocks

#### JavaScript Extraction

**Location in Source**: Within `html_page[]` string, `<script>` tag (lines ~767-1587)

**Embedded JavaScript**:
```html
<script>
// Global variables
// ... JavaScript code ...
// Event listeners
// ... JavaScript code ...
</script>
```

**Extracted File**: `lyktparad-server/web-ui/js/app.js`

**Extraction Steps**:
1. Extract all JavaScript content from `<script>` start tag to `</script>` end tag
2. Remove `<script>` and `</script>` tags (keep only JavaScript content)
3. Format JavaScript with proper indentation
4. Organize JavaScript into logical sections:
   - Global variables
   - Utility functions
   - API functions
   - Event handlers
   - Initialization code
5. Verify all API calls use relative paths (starting with `/api/`)
6. Update any hardcoded URLs to relative paths if needed
7. Preserve all functionality exactly

### File Structure

```
lyktparad-server/
├── web-ui/
│   ├── index.html          # Extracted HTML (root page)
│   ├── css/
│   │   └── styles.css      # Extracted CSS
│   ├── js/
│   │   ├── app.js          # Extracted JavaScript (main UI)
│   │   ├── ota-api.js      # OTA API integration module
│   │   └── ota-ui.js       # OTA UI logic module
│   └── API.md              # API documentation
└── ...
```

### API Endpoint Extraction

The extraction process also documents all API endpoints used by the JavaScript:

**Main UI Endpoints**:
- `GET /api/nodes` - Get node count
- `GET /api/color` - Get current color
- `POST /api/color` - Set color
- `POST /api/sequence` - Sync sequence
- `GET /api/sequence/pointer` - Get sequence pointer
- `GET /api/sequence/status` - Get sequence status
- `POST /api/sequence/start` - Start sequence
- `POST /api/sequence/stop` - Stop sequence
- `POST /api/sequence/reset` - Reset sequence

**OTA Endpoints** (used by OTA UI):
- `GET /api/ota/version` - Get current firmware version
- `POST /api/ota/download` - Start OTA download
- `GET /api/ota/status` - Get download status and progress
- `POST /api/ota/cancel` - Cancel ongoing download
- `GET /api/ota/distribution/status` - Get distribution status
- `GET /api/ota/distribution/progress` - Get distribution progress
- `POST /api/ota/distribution/cancel` - Cancel distribution
- `POST /api/ota/reboot` - Initiate coordinated reboot

All endpoints use relative paths and are documented in `web-ui/API.md`.

### Preservation Requirements

**Critical Requirements**:
1. Embedded web UI in `mesh_web.c` MUST remain unchanged
2. Embedded web server MUST continue to function normally
3. All extracted functionality MUST be preserved exactly
4. No modifications to ESP32 code are required

**Verification**:
- Embedded HTML/CSS/JavaScript remains intact in `mesh_web.c`
- Embedded web server serves the same UI as before
- Extracted files contain all functionality from embedded code
- No functionality is lost during extraction

## External Web Server Infrastructure

### Overview

The External Web Server Infrastructure provides a Node.js/Express.js-based web server that serves static files, handles CORS, provides health check endpoints, and sets up the foundation for API proxying.

### Server Setup

#### Technology Stack

**Runtime**: Node.js (version 14 or higher recommended)

**Framework**: Express.js - Web application framework for serving static files and handling routes

**Middleware**:
- **CORS** - Cross-Origin Resource Sharing middleware for development
- **express.static** - Static file serving middleware

**Optional**:
- **Bonjour** - mDNS/Bonjour service advertisement (optional, graceful degradation if unavailable)

#### Server File Structure

```
lyktparad-server/
├── server.js              # Main server file
├── package.json           # Node.js dependencies
├── .gitignore            # Git ignore file
├── README.md             # Server documentation
├── mdns.js               # mDNS service advertisement (optional)
├── lib/                  # Utility modules
│   ├── registration.js
│   ├── state-storage.js
│   ├── udp-client.js
│   ├── udp-broadcast.js
│   └── disconnection-detection.js
├── routes/               # API route handlers
│   └── proxy.js          # API proxy handler
└── web-ui/               # Static web UI files
    ├── index.html
    ├── css/
    │   └── styles.css
    ├── js/
    │   ├── app.js
    │   ├── ota-api.js
    │   └── ota-ui.js
    └── API.md
```

#### Server Configuration

**Port Configuration**:
- Default port: `8080`
- Configurable via `PORT` environment variable
- UDP port: `8081` (configurable via `UDP_PORT` environment variable)
- Broadcast port: `5353` (configurable via `BROADCAST_PORT` environment variable)

**CORS Configuration**:
```javascript
app.use(cors({
    origin: '*',                    // Allow all origins (development)
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['Content-Type']
}));
```

**Static File Serving**:
```javascript
app.use(express.static(path.join(__dirname, 'web-ui'), {
    index: 'index.html'  // Serve index.html for root path
}));
```

### Server Initialization

#### Startup Sequence

1. **Load Dependencies**: Load Express, CORS, and optional modules (mDNS, etc.)
2. **Create Express App**: Initialize Express application instance
3. **Configure Middleware**: Set up CORS, JSON parsing, static file serving
4. **Register Routes**: Set up API proxy routes and health check endpoint
5. **Start UDP Server**: Initialize UDP socket for ESP32 communication
6. **Start mDNS Service** (optional): Advertise server via mDNS/Bonjour
7. **Start HTTP Server**: Listen on configured port

#### Health Check Endpoint

**Endpoint**: `GET /health`

**Response Format**:
```json
{
  "status": "ok",
  "server": {
    "port": 8080,
    "uptime": 1234,
    "timestamp": "2025-01-15T12:34:56.789Z"
  }
}
```

**Purpose**: Provides server status information for monitoring and health checks.

### Static File Serving

#### File Structure

The server serves static files from the `web-ui/` directory:

- **Root Path** (`/`): Serves `web-ui/index.html`
- **CSS Files** (`/css/styles.css`): Serves `web-ui/css/styles.css`
- **JavaScript Files** (`/js/app.js`, `/js/ota-api.js`, `/js/ota-ui.js`): Serves respective files from `web-ui/js/`

#### MIME Type Handling

Express automatically sets appropriate MIME types:
- `text/html` for HTML files
- `text/css` for CSS files
- `application/javascript` for JavaScript files
- `application/json` for JSON files

#### Path Resolution

All paths are relative to the `web-ui/` directory. The server uses Express's `express.static()` middleware with `index: 'index.html'` to serve `index.html` at the root path.

### CORS Configuration

#### Development Settings

**Current Configuration** (Development):
- **Origin**: `*` (allows all origins)
- **Methods**: `GET`, `POST`, `OPTIONS`
- **Headers**: `Content-Type`

**Purpose**: Enables the web UI to work across different domains and network configurations during development.

#### Production Considerations

**Security Note**: CORS should be restricted in production environments. Recommended production configuration:

```javascript
app.use(cors({
    origin: 'https://yourdomain.com',  // Restrict to specific origin
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['Content-Type'],
    credentials: true  // Enable credentials if needed
}));
```

#### Preflight Request Handling

Express CORS middleware automatically handles preflight OPTIONS requests, so no additional configuration is needed.

### Error Handling

#### Static File Errors

If a requested file doesn't exist, Express returns a 404 error automatically. No custom error handling is needed for static files.

#### Server Errors

The server handles errors gracefully:
- Port conflicts: Logs error and exits
- Missing files: Returns 404
- API errors: Handled by proxy layer (see API Proxy Integration section)

### Graceful Degradation

**Optional Features**:
- **mDNS Service**: If mDNS/Bonjour is unavailable, the server continues to run normally. ESP32 devices can still connect via IP address.
- **External Server**: If the external server is unavailable, ESP32 devices continue to function normally via their embedded web server.

**No Blocking Dependencies**: The server never blocks on optional features. All optional features degrade gracefully if unavailable.

## OTA Web UI Features

### Overview

The OTA Web UI Features provide a dedicated "Firmware Update" section in the web UI with controls for initiating OTA updates, monitoring progress, and viewing update status for all mesh nodes. The UI provides real-time progress tracking, user-friendly error messages, and comprehensive status display.

### UI Components

#### Firmware Update Section

**Location**: Integrated into main web UI (`index.html`)

**Section ID**: `firmware-update-section`

**Components**:
1. **Version Display**: Shows current firmware version
2. **URL Input**: Text input for firmware URL with validation
3. **Action Buttons**: "Check for Updates", "Start Update", "Cancel Update"
4. **Progress Indicators**: Download and distribution progress bars
5. **Status Messages**: Success, error, warning, and info messages
6. **Node List**: List of nodes with update status

#### Version Display

**Element ID**: `ota-current-version`

**Functionality**:
- Displays current firmware version on page load
- Updates automatically when version changes
- Fetches version from `/api/ota/version` endpoint

**Implementation**:
```javascript
async function updateVersionDisplay() {
    try {
        const version = await api.getOtaVersion();
        elements.versionDisplay.textContent = version;
    } catch (error) {
        elements.versionDisplay.textContent = 'unknown';
        showErrorMessage('Failed to load firmware version');
    }
}
```

#### URL Input and Validation

**Element ID**: `ota-firmware-url`

**Input Type**: `url` or `text`

**Validation**:
- Validates URL format on input change
- Checks for HTTP/HTTPS protocol
- Shows validation feedback
- Enables/disables buttons based on validation

**Validation Function**:
```javascript
function validateFirmwareUrl(url) {
    if (!url || typeof url !== 'string') {
        return { valid: false, message: 'URL is required' };
    }

    try {
        const urlObj = new URL(url);
        if (urlObj.protocol !== 'http:' && urlObj.protocol !== 'https:') {
            return { valid: false, message: 'URL must use HTTP or HTTPS protocol' };
        }
        return { valid: true };
    } catch (error) {
        return { valid: false, message: 'Invalid URL format' };
    }
}
```

#### Action Buttons

**Buttons**:
1. **"Check for Updates"** (`ota-check-button`): Validates firmware URL and optionally checks for new version
2. **"Start Update"** (`ota-start-button`): Initiates OTA download and distribution
3. **"Cancel Update"** (`ota-cancel-button`): Cancels ongoing download or distribution

**Button State Management**:
- Buttons are enabled/disabled based on update state (idle, downloading, distributing)
- Button visibility changes based on current stage
- Buttons are disabled during API calls to prevent duplicate requests

**Implementation**:
```javascript
function updateButtonStates(stage) {
    switch (stage) {
        case 'idle':
            elements.checkButton.disabled = false;
            elements.startButton.disabled = false;
            elements.cancelButton.style.display = 'none';
            break;
        case 'downloading':
        case 'distributing':
            elements.checkButton.disabled = true;
            elements.startButton.disabled = true;
            elements.cancelButton.style.display = 'block';
            elements.cancelButton.disabled = false;
            break;
        // ... more states ...
    }
}
```

#### Progress Indicators

**Download Progress**:
- **Element ID**: `ota-download-progress`
- **Progress Bar**: Visual progress bar with percentage
- **Status Text**: Current download status ("Downloading... X%", "Download completed", etc.)
- **Updates**: Real-time updates via polling (1-2 second intervals)

**Distribution Progress**:
- **Element ID**: `ota-distribution-progress`
- **Progress Bar**: Visual progress bar with percentage
- **Status Text**: Current distribution status with block information
- **Node Information**: Shows nodes complete/total and failed nodes
- **Updates**: Real-time updates via polling (1-2 second intervals)

**Progress Bar Implementation**:
```javascript
function updateDownloadProgress(status) {
    const progress = Math.max(0, Math.min(1, status.progress || 0));
    const percentage = Math.round(progress * 100);

    // Update progress bar
    const progressFill = elements.downloadProgress.querySelector('.progress-fill');
    if (progressFill) {
        progressFill.style.width = `${percentage}%`;
    }
    elements.downloadProgress.setAttribute('aria-valuenow', percentage);

    // Update percentage text
    elements.downloadPercentage.textContent = `${percentage}%`;

    // Update status text
    if (status.downloading) {
        elements.downloadStatus.textContent = `Downloading... ${percentage}%`;
    } else if (progress >= 1.0) {
        elements.downloadStatus.textContent = 'Download completed';
        if (progressFill) {
            progressFill.classList.add('success');
        }
    }
}
```

#### Status Messages

**Container ID**: `ota-status-messages`

**Message Types**:
- **Success** (green): Operation completed successfully
- **Error** (red): Operation failed with error
- **Warning** (yellow): Warning message
- **Info** (blue): Informational message

**Implementation**:
```javascript
function showStatusMessage(message, type, timeout = 0) {
    const messageDiv = document.createElement('div');
    messageDiv.className = `status-message status-${type}`;
    messageDiv.textContent = message;
    elements.statusMessages.appendChild(messageDiv);

    if (timeout > 0) {
        setTimeout(() => {
            messageDiv.remove();
        }, timeout);
    }
}
```

#### Node List Display

**Element ID**: `ota-node-list`

**Functionality**:
- Displays list of nodes with update status
- Shows per-node status (pending, in-progress, completed, failed)
- Updates from distribution status API
- Integrates with periodic state updates (if available)

**Status Indicators**:
- **Pending** (gray): Node waiting for update
- **In-Progress** (blue): Node currently receiving update
- **Completed** (green): Node successfully updated
- **Failed** (red): Node update failed

### OTA API Integration

#### API Module

**File**: `web-ui/js/ota-api.js`

**Purpose**: Provides functions for interacting with OTA API endpoints

**Functions**:
- `getOtaVersion()` - Get current firmware version
- `checkForUpdates(url)` - Validate firmware URL
- `startOtaDownload(url)` - Start OTA download
- `getOtaDownloadStatus()` - Get download status and progress
- `cancelOtaDownload()` - Cancel ongoing download
- `startOtaDistribution()` - Start firmware distribution to mesh nodes
- `getOtaDistributionStatus()` - Get distribution status
- `getOtaDistributionProgress()` - Get distribution progress
- `cancelOtaDistribution()` - Cancel distribution
- `rebootOta()` - Initiate coordinated reboot

**Implementation**:
```javascript
async function getOtaVersion() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/version`);
        if (!response.ok) {
            if (response.status === 503) {
                const data = await response.json().catch(() => ({}));
                throw new Error(data.message || 'Root node is currently offline');
            }
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const data = await response.json();
        return data.version || 'unknown';
    } catch (error) {
        throw new Error(`Failed to get firmware version: ${error.message}`);
    }
}
```

#### Error Handling

**Network Errors**:
- Connection failures: Show user-friendly error message
- Timeout errors: Show timeout message with retry option
- Root node disconnection: Show disconnection message (HTTP 503)

**OTA-Specific Errors**:
- **409 Conflict**: Downgrade prevented - Show warning with option to proceed
- **500 Internal Server Error**: Server error - Show error message
- **503 Service Unavailable**: Root node offline - Show disconnection message

**Error Display**:
```javascript
function showErrorMessage(error) {
    let message = 'An error occurred';
    if (typeof error === 'string') {
        message = error;
    } else if (error.message) {
        message = error.message;
    }
    showStatusMessage(message, 'error', 0); // No auto-hide for errors
}
```

### Progress Tracking

#### Polling Mechanism

**Polling Interval**: 1-2 seconds (1.5 seconds default)

**Polling Targets**:
- During download: Polls `/api/ota/status` endpoint
- During distribution: Polls `/api/ota/distribution/status` endpoint

**Polling Implementation**:
```javascript
function startProgressPolling() {
    stopProgressPolling(); // Clear any existing polling

    updateState.pollingInterval = setInterval(async () => {
        try {
            const api = getOtaApi();
            if (updateState.stage === 'downloading') {
                const status = await api.getOtaDownloadStatus();
                updateDownloadProgress(status);

                // Check if download completed
                if (!status.downloading && status.progress >= 1.0) {
                    showSuccessMessage('Download completed successfully');
                    await startDistribution();
                }
            } else if (updateState.stage === 'distributing') {
                const status = await api.getOtaDistributionStatus();
                updateDistributionProgress(status);

                // Check if distribution completed
                if (!status.distributing && status.overall_progress >= 1.0) {
                    showSuccessMessage('Distribution completed successfully');
                    stopProgressPolling();
                    updateState.stage = 'idle';
                    updateButtonStates('idle');
                }
            }
        } catch (error) {
            updateState.pollingFailureCount++;
            if (updateState.pollingFailureCount >= 5) {
                showErrorMessage('Failed to update progress. Connection may be lost.');
                stopProgressPolling();
            }
        }
    }, 1500); // Poll every 1.5 seconds
}
```

#### State Management

**Update State Object**:
```javascript
let updateState = {
    stage: 'idle',              // idle, downloading, distributing, rebooting
    currentUrl: null,           // Current firmware URL
    pollingInterval: null,      // Polling interval ID
    stateUpdateInterval: null,  // State update interval ID
    downloadProgress: 0.0,      // Download progress (0.0-1.0)
    distributionProgress: 0.0,  // Distribution progress (0.0-1.0)
    pollingFailureCount: 0,     // Consecutive polling failures
    lastPollingError: null      // Last polling error message
};
```

**Stage Transitions**:
1. **idle** → **downloading**: When download starts
2. **downloading** → **distributing**: When download completes
3. **distributing** → **idle**: When distribution completes or is canceled
4. **any** → **idle**: When update is canceled

### Update Flow

#### Start Update Flow

1. **User enters firmware URL**: URL is validated
2. **User clicks "Start Update"**: Confirmation dialog shown (optional)
3. **API call**: `POST /api/ota/download` with URL
4. **Response handling**: Success or error response
5. **Progress polling starts**: Polls `/api/ota/status` every 1.5 seconds
6. **Progress updates**: UI updates with download progress
7. **Download completes**: Automatically starts distribution
8. **Distribution polling starts**: Polls `/api/ota/distribution/status` every 1.5 seconds
9. **Distribution updates**: UI updates with distribution progress
10. **Distribution completes**: Returns to idle state

#### Cancel Update Flow

1. **User clicks "Cancel Update"**: Confirmation dialog shown
2. **Determine operation**: Check if download or distribution is active
3. **API call**: `POST /api/ota/cancel` or `POST /api/ota/distribution/cancel`
4. **Response handling**: Success or error response
5. **Progress polling stops**: Clear polling interval
6. **State reset**: Return to idle state
7. **UI update**: Update buttons and status display

### Accessibility Features

#### ARIA Labels

**Progress Bars**:
```html
<div id="ota-download-progress" class="progress-bar" role="progressbar"
     aria-valuenow="0" aria-valuemin="0" aria-valuemax="100"
     aria-label="Download progress">
```

**Status Messages**:
```html
<div id="ota-status-messages" class="status-messages"
     aria-live="polite" aria-atomic="true"></div>
```

#### Keyboard Navigation

- All buttons are keyboard accessible
- Tab order is logical and follows UI flow
- Focus indicators are visible

#### Screen Reader Support

- ARIA labels provide context for interactive elements
- ARIA live regions announce status changes
- Progress bars are announced with current values

## Static File Serving

### File Organization

```
lyktparad-server/web-ui/
├── index.html          # Main HTML page (served at /)
├── css/
│   └── styles.css      # CSS stylesheet (served at /css/styles.css)
├── js/
│   ├── app.js          # Main UI JavaScript (served at /js/app.js)
│   ├── ota-api.js      # OTA API module (served at /js/ota-api.js)
│   └── ota-ui.js       # OTA UI logic (served at /js/ota-ui.js)
└── API.md              # API documentation (not served, reference only)
```

### Path Configuration

**HTML File Links**:
```html
<link rel="stylesheet" href="css/styles.css">
<script src="js/app.js"></script>
<script src="js/ota-api.js"></script>
<script src="js/ota-ui.js"></script>
```

**Server Configuration**:
```javascript
app.use(express.static(path.join(__dirname, 'web-ui'), {
    index: 'index.html'
}));
```

**Result**:
- `/` → `web-ui/index.html`
- `/css/styles.css` → `web-ui/css/styles.css`
- `/js/app.js` → `web-ui/js/app.js`
- `/js/ota-api.js` → `web-ui/js/ota-api.js`
- `/js/ota-ui.js` → `web-ui/js/ota-ui.js`

### MIME Type Handling

Express automatically sets appropriate MIME types based on file extensions:

- `.html` → `text/html`
- `.css` → `text/css`
- `.js` → `application/javascript`
- `.json` → `application/json`
- `.md` → `text/markdown`

No manual MIME type configuration is needed for standard file types.

### Caching Considerations

**Development**: No caching headers are set, allowing for live reloading during development.

**Production**: Consider adding cache headers for static assets:

```javascript
app.use(express.static(path.join(__dirname, 'web-ui'), {
    index: 'index.html',
    maxAge: '1d',  // Cache static assets for 1 day
    etag: true     // Enable ETag support
}));
```

## CORS Configuration

### Development Configuration

**Current Settings**:
```javascript
app.use(cors({
    origin: '*',                    // Allow all origins
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['Content-Type']
}));
```

**Purpose**: Enables the web UI to work across different domains and network configurations during development.

### Production Configuration

**Recommended Settings**:
```javascript
app.use(cors({
    origin: 'https://yourdomain.com',  // Restrict to specific origin
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['Content-Type'],
    credentials: true  // Enable credentials if needed
}));
```

**Security Considerations**:
- Restrict `origin` to specific allowed domains
- Only allow necessary HTTP methods
- Only allow necessary headers
- Use `credentials: true` only if needed (enables cookies/authentication)

### Preflight Request Handling

Express CORS middleware automatically handles preflight OPTIONS requests:

1. Browser sends OPTIONS request with CORS headers
2. Express CORS middleware responds with appropriate CORS headers
3. Browser allows actual request if preflight succeeds

No additional configuration is needed for preflight handling.

## API Proxy Integration

### Overview

The API Proxy Integration allows the external web server to proxy API requests from the web UI to the ESP32 root node via UDP. This enables the web UI to work through the external server while maintaining compatibility with the embedded server.

### Proxy Flow

```
Web UI → HTTP Request → External Server → UDP Command → ESP32 Root Node
                                                              │
Web UI ← HTTP Response ← External Server ← UDP Response ←────┘
```

### Route Configuration

**Route Pattern**: `/api/*`

**Handler**: `proxyHandler` (from `routes/proxy.js`)

**Implementation**:
```javascript
// API proxy routes (before static file serving)
app.all('/api/*', proxyHandler);
```

**Method Support**: `GET`, `POST`, `OPTIONS` (handled by proxy)

### Request Translation

**HTTP to UDP Translation**:
1. Extract API endpoint path from HTTP request
2. Map HTTP method and path to UDP command ID
3. Convert HTTP request body to UDP payload format
4. Add sequence number for request/response matching
5. Send UDP command to ESP32 root node

**Example**: `POST /api/ota/download` with `{"url": "http://..."}` → UDP command `0xE7` with payload

### Response Translation

**UDP to HTTP Translation**:
1. Receive UDP response from ESP32 root node
2. Match response to request using sequence number
3. Parse UDP payload to JSON format
4. Convert to HTTP response with appropriate status code
5. Send HTTP response to web UI

**Example**: UDP response with `{"success": true}` → HTTP 200 with JSON body

### Error Handling

**Root Node Disconnection**:
- Returns HTTP 503 (Service Unavailable)
- Response body: `{"message": "Root node is currently offline or unreachable"}`

**Request Timeout**:
- Returns HTTP 504 (Gateway Timeout)
- Response body: `{"error": "Request timeout"}`

**Network Errors**:
- Returns HTTP 500 (Internal Server Error)
- Response body: `{"error": "Network error: ..."}`

### Timeout Configuration

**Default Timeout**: 5 seconds

**Configurable**: Can be adjusted via environment variable or configuration

**Implementation**: Uses Promise timeout or request timeout mechanism

## Error Handling and Graceful Degradation

### Server-Side Error Handling

#### Port Conflicts

**Error**: Port already in use

**Handling**:
- Log error message with port number
- Exit process with error code
- User can change port via `PORT` environment variable

**Message**: `Error: Port ${PORT} is already in use. Please use a different port.`

#### Missing Files

**Error**: Requested file doesn't exist

**Handling**:
- Express returns 404 automatically
- No custom error handling needed for static files

#### API Errors

**Error**: API proxy request fails

**Handling**:
- Handled by proxy layer (see API Proxy Integration section)
- Returns appropriate HTTP status code
- Includes error message in response body

### Client-Side Error Handling

#### Network Errors

**Error**: Failed to fetch API endpoint

**Handling**:
- Show user-friendly error message
- Provide retry option
- Log error for debugging

**Implementation**:
```javascript
async function handleApiCall(apiFunction) {
    try {
        return await apiFunction();
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            showErrorMessage('Root node is currently offline. Please check the connection.');
        } else {
            showErrorMessage(`Operation failed: ${error.message}`);
        }
        throw error;
    }
}
```

#### Validation Errors

**Error**: Invalid URL format

**Handling**:
- Show validation feedback immediately
- Disable action buttons
- Show specific error message

#### OTA Errors

**Error**: OTA operation fails

**Handling**:
- Show user-friendly error message
- Provide actionable suggestions
- Allow retry or cancel
- Handle downgrade warnings (409 Conflict)

**Downgrade Warning**:
```javascript
if (response.status === 409) {
    const proceed = confirm('Downgrade detected. Firmware version is older than current version. Do you want to proceed?');
    if (proceed) {
        // Proceed with downgrade
    } else {
        // Cancel operation
    }
}
```

### Graceful Degradation

#### Optional Features

**mDNS Service**:
- If mDNS/Bonjour is unavailable, server continues to run normally
- ESP32 devices can still connect via IP address
- Logs warning message but doesn't fail

**External Server**:
- If external server is unavailable, ESP32 devices continue to function normally
- Embedded web server always runs regardless of external server status
- No dependency on external server for mesh functionality

#### Feature Detection

**API Availability**:
- Web UI checks for API availability on page load
- Shows appropriate UI state based on API availability
- Provides fallback behavior when API is unavailable

**Root Node Connection**:
- UI detects root node disconnection via HTTP 503 responses
- Shows connection status indicator
- Provides retry functionality
- Continues to function when connection is restored

## Implementation Details

### Server File Structure

```
lyktparad-server/
├── server.js              # Main server file
├── package.json           # Node.js dependencies
├── .gitignore            # Git ignore file
├── README.md             # Server documentation
├── mdns.js               # mDNS service advertisement (optional)
├── lib/                  # Utility modules
│   ├── registration.js
│   ├── state-storage.js
│   ├── udp-client.js
│   ├── udp-broadcast.js
│   └── disconnection-detection.js
├── routes/               # API route handlers
│   └── proxy.js          # API proxy handler
└── web-ui/               # Static web UI files
    ├── index.html
    ├── css/
    │   └── styles.css
    ├── js/
    │   ├── app.js
    │   ├── ota-api.js
    │   └── ota-ui.js
    └── API.md
```

### Server Initialization Code

```javascript
const express = require('express');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 8080;

// Configure CORS
app.use(cors({
    origin: '*',
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['Content-Type']
}));

// Configure JSON body parsing
app.use(express.json());

// API proxy routes (before static file serving)
app.all('/api/*', proxyHandler);

// Configure static file serving
app.use(express.static(path.join(__dirname, 'web-ui'), {
    index: 'index.html'
}));

// Health check endpoint
app.get('/health', (req, res) => {
    res.json({
        status: 'ok',
        server: {
            port: PORT,
            uptime: Math.floor((Date.now() - serverStartTime) / 1000),
            timestamp: new Date().toISOString()
        }
    });
});

// Start server
app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});
```

### OTA UI Initialization Code

```javascript
function initOtaUI() {
    // Get DOM element references
    elements = {
        versionDisplay: document.getElementById('ota-current-version'),
        urlInput: document.getElementById('ota-firmware-url'),
        checkButton: document.getElementById('ota-check-button'),
        startButton: document.getElementById('ota-start-button'),
        cancelButton: document.getElementById('ota-cancel-button'),
        // ... more elements ...
    };

    // Set up event listeners
    setupEventListeners();

    // Load initial version
    updateVersionDisplay();
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    initOtaUI();
});
```

### Progress Polling Implementation

```javascript
function startProgressPolling() {
    stopProgressPolling();

    updateState.pollingInterval = setInterval(async () => {
        try {
            const api = getOtaApi();
            if (updateState.stage === 'downloading') {
                const status = await api.getOtaDownloadStatus();
                updateDownloadProgress(status);

                if (!status.downloading && status.progress >= 1.0) {
                    showSuccessMessage('Download completed');
                    await startDistribution();
                }
            } else if (updateState.stage === 'distributing') {
                const status = await api.getOtaDistributionStatus();
                updateDistributionProgress(status);

                if (!status.distributing && status.overall_progress >= 1.0) {
                    showSuccessMessage('Distribution completed');
                    stopProgressPolling();
                    updateState.stage = 'idle';
                    updateButtonStates('idle');
                }
            }
        } catch (error) {
            updateState.pollingFailureCount++;
            if (updateState.pollingFailureCount >= 5) {
                showErrorMessage('Failed to update progress');
                stopProgressPolling();
            }
        }
    }, 1500);
}
```

## API Reference

### Server Endpoints

#### Health Check

**Endpoint**: `GET /health`

**Response**:
```json
{
  "status": "ok",
  "server": {
    "port": 8080,
    "uptime": 1234,
    "timestamp": "2025-01-15T12:34:56.789Z"
  }
}
```

#### Static Files

**Endpoints**:
- `GET /` - Serves `web-ui/index.html`
- `GET /css/styles.css` - Serves CSS stylesheet
- `GET /js/app.js` - Serves main UI JavaScript
- `GET /js/ota-api.js` - Serves OTA API module
- `GET /js/ota-ui.js` - Serves OTA UI logic

#### API Proxy

**Endpoint Pattern**: `/api/*`

**Method**: `GET`, `POST`, `OPTIONS`

**Behavior**: Proxies requests to ESP32 root node via UDP (see API Proxy Integration section)

### OTA API Endpoints

All OTA endpoints are proxied to the ESP32 root node. See the API Proxy Integration section for details.

**Endpoints**:
- `GET /api/ota/version` - Get current firmware version
- `POST /api/ota/download` - Start OTA download
- `GET /api/ota/status` - Get download status and progress
- `POST /api/ota/cancel` - Cancel ongoing download
- `POST /api/ota/distribute` - Start firmware distribution
- `GET /api/ota/distribution/status` - Get distribution status
- `GET /api/ota/distribution/progress` - Get distribution progress
- `POST /api/ota/distribution/cancel` - Cancel distribution
- `POST /api/ota/reboot` - Initiate coordinated reboot

## Integration Points

### Embedded Web Server

**File**: `lyktparad-espidf/src/mesh_web.c`

**Function**: `mesh_web_server_start()`

**Integration**:
- Embedded web server always runs on root nodes (port 80)
- External server is completely optional
- Both servers can coexist without conflicts
- Embedded server provides fallback if external server is unavailable

### OTA API Implementation

**Files**:
- `lyktparad-espidf/src/mesh_web.c` - OTA API endpoint handlers
- `lyktparad-espidf/src/mesh_ota.c` - OTA implementation

**Integration**:
- OTA API endpoints are already implemented on ESP32 root node
- External server proxies OTA requests to root node
- OTA UI communicates with root node through external server
- Direct access to root node still works via embedded server

### API Proxy Layer

**File**: `lyktparad-server/routes/proxy.js`

**Integration**:
- Handles all `/api/*` requests
- Translates HTTP to UDP and vice versa
- Manages request/response matching with sequence numbers
- Handles timeouts and errors

### State Updates

**Integration**:
- Periodic state updates provide mesh network state to external server
- OTA UI can integrate with state updates for node list display
- State updates are completely optional and don't affect OTA functionality

---

**End of Document**
