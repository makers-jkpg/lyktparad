# Root Node Disconnection Recovery - Development Guide

**Last Updated:** 2025-01-15

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Disconnection Detection](#disconnection-detection)
4. [Stale Registration Cleanup](#stale-registration-cleanup)
5. [Error Response Handling](#error-response-handling)
6. [Connection Status API](#connection-status-api)
7. [Web UI Error Display](#web-ui-error-display)
8. [Re-registration Handling](#re-registration-handling)
9. [Automatic Recovery](#automatic-recovery)
10. [Error Handling and Graceful Degradation](#error-handling-and-graceful-degradation)
11. [Implementation Details](#implementation-details)
12. [API Reference](#api-reference)
13. [Integration Points](#integration-points)

## Overview

### Purpose

The Root Node Disconnection Recovery system provides comprehensive error handling for scenarios where the ESP32 root node disconnects from the external web server. This system detects disconnections, handles errors gracefully, provides user feedback through the web UI, and automatically recovers when the root node reconnects.

**Critical Principle**: Disconnection from the external web server is NOT an error condition for the ESP32 root node. The root node continues operating normally with its embedded web server. This error handling is for the external web server's UI only, not for mesh device operation. The external web server should handle UDP communication failures gracefully and inform users they can access the root node directly via its IP address.

### Design Decisions

**ESP32 Unaffected**: The ESP32 root node is completely unaffected by disconnection from the external server. The embedded web server (`mesh_web_server_start()`) MUST ALWAYS run regardless of external server communication status. Disconnection detection and error handling are server-side only.

**Graceful Degradation**: All disconnection scenarios are handled gracefully without affecting the external server's operation. When the root node is offline, the external server continues operating, returns appropriate HTTP error codes (503 Service Unavailable), and provides helpful error messages to users.

**Automatic Recovery**: When a root node re-registers after disconnection (e.g., network restart, IP change), the system automatically recovers and resumes normal communication without manual intervention. The registration status is updated, failure counters are reset, and the UI reflects the recovered state.

**User-Friendly Error Messages**: Error messages are clear, helpful, and actionable. When the root node is offline, users are informed and given suggestions for direct access via the root node's IP address.

**Multiple Detection Methods**: Disconnection is detected through multiple mechanisms for reliability:
1. **Heartbeat Timeout**: No heartbeat or state update received within timeout period (default: 3 minutes)
2. **UDP Communication Failures**: Consecutive UDP communication failures exceed threshold (default: 3 failures)
3. **IP Change Detection**: New registration with same mesh ID but different IP address triggers re-registration handling

**Server-Side Only**: All disconnection detection and recovery logic runs on the external web server. The ESP32 root node continues normal operation regardless of external server status.

### Key Features

1. **Heartbeat Timeout Detection**: Monitors last heartbeat/state update timestamp and detects timeouts
2. **UDP Failure Tracking**: Tracks consecutive UDP communication failures and marks root offline after threshold
3. **Stale Registration Cleanup**: Automatically removes registrations that have been offline for extended periods
4. **Error Response Implementation**: Returns appropriate HTTP status codes (503, 404) with helpful error messages
5. **Connection Status API**: Provides real-time connection status information for the web UI
6. **Web UI Error Display**: Visual connection status indicator, error messages, and retry functionality
7. **Re-registration Handling**: Automatically updates registration when root node re-registers with new IP
8. **Automatic Recovery**: System recovers automatically when root node reconnects

## Architecture

### Overall Disconnection Recovery Flow

```
┌─────────────────────────────────────┐
│  ESP32 Root Node                     │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  Embedded Web Server            │ │
│  │  (MUST ALWAYS RUN)              │ │
│  │  - Port 80                      │ │
│  │  - Direct HTTP Access           │ │
│  │  - Unaffected by external       │ │
│  └─────────────────────────────────┘ │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  Heartbeat/State Updates        │ │
│  │  (Optional, if registered)      │ │
│  │  - May stop/fail                │ │
│  └──────────────┬──────────────────┘ │
└─────────────────┼────────────────────┘
                  │
                  │ UDP Communication
                  │ (May fail/timeout)
                  ▼
┌─────────────────────────────────────┐
│  External Web Server                 │
│  (lyktparad-server)                  │
│                                       │
│  ┌─────────────────────────────────┐ │
│  │  Disconnection Detection        │ │
│  │  - Heartbeat timeout monitor    │ │
│  │  - UDP failure tracker          │ │
│  │  - Periodic checks (30s)        │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  Stale Registration Cleanup     │ │
│  │  - Remove offline registrations │ │
│  │  - After 2x timeout (6 min)     │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  Error Response Handler         │ │
│  │  - Return 503 (offline)         │ │
│  │  - Return 404 (not registered)  │ │
│  │  - Include helpful messages     │ │
│  └──────────────┬──────────────────┘ │
│                 │                    │
│                 ▼                    │
│  ┌─────────────────────────────────┐ │
│  │  Connection Status API          │ │
│  │  - GET /api/connection/status   │ │
│  │  - Real-time status info        │ │
│  └──────────────┬──────────────────┘ │
└─────────────────┼────────────────────┘
                  │
                  │ HTTP Response
                  │ 503 Service Unavailable
                  │ Error Messages
                  ▼
┌─────────────────────────────────────┐
│  Web UI (Browser)                    │
│  - Connection status indicator       │
│  - Error message display             │
│  - Retry functionality               │
│  - Direct access suggestion          │
└─────────────────────────────────────┘
```

### Disconnection Detection Flow

```
Periodic Check (every 30 seconds)
    │
    ├─► Check Heartbeat Timeout
    │   │
    │   ├─► Last heartbeat > 3 minutes ago?
    │   │   └─► Mark as offline
    │   │
    │   └─► Last state update > 3 minutes ago?
    │       └─► Mark as offline
    │
    ├─► Check UDP Failure Count
    │   │
    │   └─► UDP failures >= 3?
    │       └─► Mark as offline
    │
    └─► Cleanup Stale Registrations
        │
        └─► Offline > 6 minutes (2x timeout)?
            └─► Remove registration
```

### Error Response Flow

```
API Request from Web UI
    │
    ├─► Check Registration Status
    │   │
    │   ├─► No registration?
    │   │   └─► Return 404 Not Found
    │   │       "No root node registered"
    │   │
    │   └─► Registered but offline?
    │       └─► Return 503 Service Unavailable
    │           "Root node unavailable"
    │           Include: IP address for direct access
    │
    ├─► Attempt UDP Communication
    │   │
    │   ├─► Success?
    │   │   └─► Reset failure count
    │   │   └─► Mark as online
    │   │   └─► Return normal response
    │   │
    │   └─► Failure (timeout/network error)?
    │       └─► Increment failure count
    │       └─► Check threshold (3 failures)
    │       └─► Mark offline if threshold exceeded
    │       └─► Return 503 Service Unavailable
```

## Disconnection Detection

### Heartbeat Timeout Detection

The system monitors the last heartbeat and state update timestamps for each registered root node. If no activity is received within the timeout period, the root node is marked as offline.

**Configuration**:
- **Default Timeout**: 3 minutes (180,000 ms)
- **Configurable**: Via environment variable `HEARTBEAT_TIMEOUT_MS`
- **Check Interval**: Every 30 seconds (configurable via `CLEANUP_CHECK_INTERVAL_MS`)

**Detection Logic**:
1. Check if registration has `last_heartbeat` or `last_state_update` timestamp
2. Calculate time since last activity
3. If time exceeds timeout threshold, mark registration as offline
4. If no heartbeat/state update ever received and registration is older than timeout, mark as offline

**Implementation**: `lyktparad-server/lib/disconnection-detection.js`

```javascript
function isHeartbeatTimeout(registration) {
    if (!registration.last_heartbeat && !registration.last_state_update) {
        // No heartbeat or state update ever received
        const registeredAgo = Date.now() - registration.registered_at;
        return registeredAgo > HEARTBEAT_TIMEOUT_MS;
    }

    // Use last_heartbeat if available, otherwise use last_state_update
    const lastActivity = registration.last_heartbeat || registration.last_state_update;
    const timeSinceActivity = Date.now() - lastActivity;
    return timeSinceActivity > HEARTBEAT_TIMEOUT_MS;
}
```

### UDP Communication Failure Detection

The system tracks consecutive UDP communication failures for each registered root node. When the failure count exceeds the threshold, the root node is marked as offline.

**Configuration**:
- **Default Threshold**: 3 consecutive failures
- **Configurable**: Via environment variable `UDP_FAILURE_THRESHOLD`
- **Failure Tracking**: Incremented on UDP timeout, network errors, or communication failures

**Detection Logic**:
1. Track UDP failure count per registration
2. Increment count on each UDP communication failure
3. Reset count to 0 on successful UDP communication
4. If count exceeds threshold, mark registration as offline

**Implementation**: `lyktparad-server/routes/proxy.js` and `lyktparad-server/lib/disconnection-detection.js`

```javascript
function isUdpFailureThresholdExceeded(registration) {
    const failureCount = registration.udp_failure_count || 0;
    return failureCount >= UDP_FAILURE_THRESHOLD;
}
```

**Failure Tracking**:
- UDP send failures
- UDP timeout errors (no response within timeout period)
- Network errors (ENETUNREACH, EHOSTUNREACH)
- Response validation failures

### IP Change Detection

The system detects when a root node re-registers with a different IP address (e.g., DHCP renewal, network reconnection). The registration is updated with the new IP address, and the system automatically recovers.

**Detection Logic**:
1. Root node sends registration packet with same mesh ID but different IP
2. System checks if mesh ID already exists in registration storage
3. If exists, update registration with new IP address and UDP port
4. Reset failure count and offline status (automatic recovery)
5. Log IP change event

**Implementation**: `lyktparad-server/lib/registration.js`

```javascript
function registerRootNode(root_ip, mesh_id, node_count, firmware_version, timestamp, udp_port) {
    const meshIdHex = mesh_id.toString('hex');
    const existing = registrations.get(meshIdHex);

    if (existing) {
        // Re-registration: Update IP and reset failure status
        const newRootIpStr = `${root_ip[0]}.${root_ip[1]}.${root_ip[2]}.${root_ip[3]}`;
        if (existing.root_ip !== newRootIpStr) {
            // IP changed - automatic recovery
            updateRegistrationIp(meshIdHex, root_ip, udp_port);
        }
    }
    // ... registration logic
}
```

### Periodic Monitoring

The system runs periodic monitoring tasks to detect disconnections and clean up stale registrations.

**Monitoring Tasks**:
1. **Heartbeat Timeout Check**: Every 30 seconds, check all registrations for timeout
2. **Stale Registration Cleanup**: Every 30 seconds, remove registrations that have been offline for extended periods
3. **Automatic Start**: Monitoring starts automatically when the server starts

**Implementation**: `lyktparad-server/lib/disconnection-detection.js`

```javascript
function startMonitoring() {
    monitoringInterval = setInterval(() => {
        // Monitor heartbeat timeouts
        monitorHeartbeatTimeout();

        // Clean up stale registrations
        cleanupStaleRegistrations();
    }, CLEANUP_CHECK_INTERVAL_MS);
}
```

## Stale Registration Cleanup

### Cleanup Logic

The system automatically removes stale registrations that have been offline for extended periods. This prevents accumulation of inactive registrations and ensures the registration storage remains clean.

**Configuration**:
- **Cleanup Timeout**: 2x heartbeat timeout (default: 6 minutes)
- **Cleanup Interval**: Every 30 seconds (same as monitoring interval)
- **Force Cleanup**: Optional parameter to force cleanup regardless of timing

**Cleanup Conditions**:
1. Registration is marked as offline
2. Last activity timestamp exists
3. Time since last activity exceeds cleanup timeout (2x heartbeat timeout)
4. Registration has no activity and registration time exceeds cleanup timeout

**Implementation**: `lyktparad-server/lib/disconnection-detection.js`

```javascript
function cleanupStaleRegistrations(forceCleanup = false) {
    const registrations = getAllRegistrations();
    const cleanedUp = [];

    for (const registration of registrations) {
        const cleanupTimeout = HEARTBEAT_TIMEOUT_MS * 2; // 2x heartbeat timeout
        const isOffline = registration.is_offline || false;
        const lastActivity = registration.last_heartbeat || registration.last_state_update;

        if (isOffline && lastActivity) {
            const timeSinceActivity = Date.now() - lastActivity;
            if (timeSinceActivity > cleanupTimeout || forceCleanup) {
                // Remove stale registration
                removeRegistration(registration.mesh_id);
                cleanedUp.push(registration.mesh_id);
            }
        } else if (!lastActivity && (Date.now() - registration.registered_at) > cleanupTimeout) {
            // Registration with no activity - remove it
            removeRegistration(registration.mesh_id);
            cleanedUp.push(registration.mesh_id);
        }
    }

    return cleanedUp;
}
```

**Logging**: All cleanup actions are logged with details about the removed registration (mesh ID, IP address, offline duration).

## Error Response Handling

### HTTP Error Codes

The system returns appropriate HTTP status codes to indicate different error conditions:

**503 Service Unavailable**: Root node is registered but currently offline or unreachable
- **Use Cases**: Heartbeat timeout, UDP failure threshold exceeded, UDP communication timeout
- **Response Format**: JSON with error details and suggestions

**404 Not Found**: No root node is registered
- **Use Cases**: No registration exists, registration was cleaned up
- **Response Format**: JSON with error message and direct access suggestion

**413 Payload Too Large**: Request payload exceeds maximum size
- **Use Cases**: UDP packet size exceeds MTU limit
- **Response Format**: JSON with error message

**500 Internal Server Error**: Unexpected server error
- **Use Cases**: Unhandled exceptions, unexpected errors
- **Response Format**: JSON with error message

### Error Response Format

All error responses follow a consistent JSON format:

```json
{
  "error": "Error type",
  "message": "Human-readable error message",
  "code": 503,
  "suggestion": "Helpful suggestion for user (optional)",
  "last_seen": "ISO timestamp (optional)"
}
```

**Example 503 Response** (Root node offline):

```json
{
  "error": "Root node unavailable",
  "message": "The root node is currently offline or unreachable.",
  "code": 503,
  "suggestion": "You can access the root node directly via its IP address: http://192.168.1.100",
  "last_seen": "2025-01-15T10:30:00.000Z"
}
```

**Example 404 Response** (No registration):

```json
{
  "error": "No root node registered",
  "message": "External web server has no registered root node. Please access the root node directly via its IP address.",
  "code": 404
}
```

### Error Response Implementation

**Location**: `lyktparad-server/routes/proxy.js`

**Error Handling Flow**:
1. Check if root node is registered
2. If not registered, return 404 Not Found
3. If registered but offline, return 503 Service Unavailable
4. If registered and online, attempt UDP communication
5. On UDP failure, track failure and return 503 with error details

```javascript
async function processProxyRequest(req, res) {
    let rootNode = null;
    try {
        // Check if root node is registered
        rootNode = getRegisteredRootNode();
        if (!rootNode) {
            const registration = getFirstRegisteredRootNode();
            if (registration && registration.is_offline) {
                // Root node is registered but offline
                res.status(503).json({
                    error: 'Root node unavailable',
                    message: 'The root node is currently offline or unreachable.',
                    code: 503,
                    suggestion: `You can access the root node directly via its IP address: http://${registration.root_ip}`,
                    last_seen: registration.last_heartbeat || registration.last_state_update || null
                });
            } else {
                // No root node registered
                res.status(404).json({
                    error: 'No root node registered',
                    message: 'External web server has no registered root node. Please access the root node directly via its IP address.',
                    code: 404
                });
            }
            return;
        }

        // Attempt UDP communication
        // ... UDP communication logic ...

    } catch (error) {
        // Track UDP communication failures
        if (rootNode && rootNode.registration) {
            incrementUdpFailureCount(rootNode.registration.mesh_id);

            // Check if failure threshold exceeded
            if (isUdpFailureThresholdExceeded(rootNode.registration)) {
                markRegistrationOffline(rootNode.registration.mesh_id);
            }
        }

        // Handle specific error types
        if (error.message.includes('timeout')) {
            res.status(503).json({
                error: 'Root node unavailable',
                message: 'Root node did not respond within timeout period. The node may be offline or unreachable.',
                code: 503,
                suggestion: rootNode ? `You can access the root node directly via its IP address: http://${rootNode.root_ip}` : 'Please check if the root node is online.'
            });
        } else if (error.message.includes('ENETUNREACH') || error.message.includes('EHOSTUNREACH')) {
            res.status(503).json({
                error: 'Root node unavailable',
                message: 'Cannot reach root node. The node may be offline or on a different network.',
                code: 503,
                suggestion: rootNode ? `You can access the root node directly via its IP address: http://${rootNode.root_ip}` : 'Please check if the root node is online.'
            });
        } else {
            // Other errors
            res.status(500).json({
                error: 'Internal server error',
                message: error.message,
                code: 500
            });
        }
    }
}
```

## Connection Status API

### Endpoint: GET /api/connection/status

The Connection Status API provides real-time connection status information for the web UI. This endpoint allows the UI to poll for connection status and display appropriate indicators.

**Request**: `GET /api/connection/status`

**Response Format**:

```json
{
  "connected": false,
  "status": "offline",
  "last_seen": "2025-01-15T10:30:00.000Z",
  "last_seen_ms": 1736943000000,
  "time_since_last_seen_ms": 180000,
  "root_node_ip": "192.168.1.100",
  "mesh_id": "aa:bb:cc:dd:ee:ff",
  "udp_port": 8081,
  "node_count": 5,
  "firmware_version": "1.0.0",
  "registered_at": "2025-01-15T10:00:00.000Z",
  "udp_failure_count": 3
}
```

**Status Values**:
- `"connected"`: Root node is online and reachable
- `"offline"`: Root node is offline (timeout or failure threshold exceeded)
- `"not_registered"`: No root node is registered
- `"unknown"`: Registration exists but no activity yet
- `"stale"`: Last activity was more than 1 minute ago but not yet offline

**Implementation**: `lyktparad-server/server.js`

```javascript
app.get('/api/connection/status', (req, res) => {
    const registration = getFirstRegisteredRootNode();

    if (!registration) {
        res.json({
            connected: false,
            status: 'not_registered',
            last_seen: null,
            root_node_ip: null,
            mesh_id: null
        });
        return;
    }

    // Determine connection status
    const isOffline = registration.is_offline || false;
    const lastHeartbeat = registration.last_heartbeat || null;
    const lastStateUpdate = registration.last_state_update || null;
    const lastSeen = lastHeartbeat || lastStateUpdate || null;

    // Calculate time since last activity
    let timeSinceLastSeen = null;
    if (lastSeen) {
        timeSinceLastSeen = Date.now() - lastSeen;
    }

    // Determine status string
    let status = 'connected';
    if (isOffline) {
        status = 'offline';
    } else if (!lastSeen) {
        status = 'unknown';
    } else if (timeSinceLastSeen > 60000) { // More than 1 minute
        status = 'stale';
    }

    res.json({
        connected: !isOffline && lastSeen !== null,
        status: status,
        last_seen: lastSeen ? new Date(lastSeen).toISOString() : null,
        last_seen_ms: lastSeen,
        time_since_last_seen_ms: timeSinceLastSeen,
        root_node_ip: registration.root_ip,
        mesh_id: registration.mesh_id,
        udp_port: registration.udp_port,
        node_count: registration.node_count,
        firmware_version: registration.firmware_version,
        registered_at: new Date(registration.registered_at).toISOString(),
        udp_failure_count: registration.udp_failure_count || 0
    });
});
```

## Web UI Error Display

### Connection Status Indicator

The web UI displays a visual connection status indicator that shows the current connection state. The indicator updates periodically by polling the `/api/connection/status` endpoint.

**Visual Indicators**:
- **Green Dot (●)**: Connected and online
- **Red Dot (●)**: Offline or unreachable
- **Yellow Dot (●)**: Unknown or stale
- **Gray Dot (●)**: Not registered

**Display Elements**:
- **Icon**: Color-coded dot showing connection status
- **Text**: Human-readable status message (e.g., "Connected", "Offline", "Not registered")
- **Details**: Additional information (e.g., last seen time, root node IP)

**Implementation**: `lyktparad-server/web-ui/js/app.js`

```javascript
function updateConnectionStatus() {
    fetch('/api/connection/status')
        .then(response => response.json())
        .then(status => {
            const icon = document.getElementById('connection-status-icon');
            const text = document.getElementById('connection-status-text');
            const details = document.getElementById('connection-status-details');

            // Update icon class based on status
            icon.className = 'connection-status-icon';
            if (status.status === 'connected') {
                icon.className += ' connected';
                text.textContent = 'Connected';
            } else if (status.status === 'offline') {
                icon.className += ' offline';
                text.textContent = 'Offline';
                if (status.last_seen) {
                    details.textContent = `Last seen: ${new Date(status.last_seen).toLocaleString()}`;
                }
            } else if (status.status === 'not_registered') {
                icon.className += ' unknown';
                text.textContent = 'Not registered';
                details.textContent = '';
            } else {
                icon.className += ' stale';
                text.textContent = 'Stale connection';
                if (status.last_seen) {
                    details.textContent = `Last seen: ${new Date(status.last_seen).toLocaleString()}`;
                }
            }
        })
        .catch(err => {
            console.error('Failed to fetch connection status:', err);
        });
}

// Poll connection status every 5 seconds
setInterval(updateConnectionStatus, 5000);
```

**CSS Styling**: `lyktparad-server/web-ui/css/styles.css`

```css
.connection-status-icon.connected {
    color: #28a745; /* Green */
}

.connection-status-icon.offline {
    color: #dc3545; /* Red */
}

.connection-status-icon.unknown {
    color: #6c757d; /* Gray */
}

.connection-status-icon.stale {
    color: #ffc107; /* Yellow */
}
```

### Error Message Display

When API requests fail due to root node disconnection, the web UI displays user-friendly error messages with suggestions for direct access.

**Error Handling**: `lyktparad-server/web-ui/js/app.js`

```javascript
function handleApiError(error, feedbackElement, retryCallback) {
    // Check if error is a root node disconnection error (503)
    const isDisconnectionError =
        error.message.includes('503') ||
        error.message.includes('offline') ||
        error.message.includes('unreachable') ||
        error.message.includes('Root node unavailable');

    if (isDisconnectionError) {
        // Get connection status to find root node IP for direct access suggestion
        fetch('/api/connection/status')
            .then(response => response.json())
            .then(status => {
                let errorMsg = 'Root node is offline or unreachable.';
                if (status.root_node_ip) {
                    errorMsg += ` You can access it directly at http://${status.root_node_ip}`;
                }
                feedbackElement.textContent = errorMsg;
                feedbackElement.className = 'sequence-control-feedback-error';

                // Add retry button if retry callback provided
                if (retryCallback && typeof retryCallback === 'function') {
                    const retryBtn = document.createElement('button');
                    retryBtn.className = 'btn btn-secondary retry-btn';
                    retryBtn.textContent = 'Retry';
                    retryBtn.onclick = () => {
                        feedbackElement.textContent = 'Retrying...';
                        retryCallback();
                    };
                    feedbackElement.parentElement.appendChild(retryBtn);
                }
            })
            .catch(() => {
                feedbackElement.textContent = 'Root node is offline or unreachable.';
                feedbackElement.className = 'sequence-control-feedback-error';
            });
    } else {
        // Regular error
        feedbackElement.textContent = 'Error: ' + (error.message || 'Unknown error');
        feedbackElement.className = 'sequence-control-feedback-error';
    }
}
```

### Retry Functionality

The web UI provides retry buttons for failed API requests. Users can click the retry button to attempt the operation again.

**Retry Button**:
- Appears automatically when disconnection errors occur
- Calls the original API function when clicked
- Shows "Retrying..." feedback during retry
- Removed on successful retry

## Re-registration Handling

### Re-registration Detection

When a root node re-registers with the same mesh ID, the system detects this as a re-registration event and handles it appropriately.

**Re-registration Scenarios**:
1. **IP Address Change**: Root node gets new IP from DHCP, re-registers with new IP
2. **Network Reconnection**: Root node disconnects and reconnects to network
3. **Manual Re-registration**: Root node explicitly re-registers (e.g., after firmware update)

**Handling Logic**:
1. Check if mesh ID already exists in registration storage
2. If exists, update existing registration instead of creating new one
3. Update IP address and UDP port if changed
4. Preserve original registration timestamp
5. Reset failure count and offline status (automatic recovery)
6. Log re-registration event

**Implementation**: `lyktparad-server/lib/registration.js`

```javascript
function registerRootNode(root_ip, mesh_id, node_count, firmware_version, timestamp, udp_port) {
    const meshIdHex = mesh_id.toString('hex');
    const rootIpStr = `${root_ip[0]}.${root_ip[1]}.${root_ip[2]}.${root_ip[3]}`;

    // Check if registration already exists
    const existing = registrations.get(meshIdHex);

    const registration = {
        root_ip: rootIpStr,
        root_ip_bytes: Buffer.from(root_ip),
        mesh_id: meshIdHex,
        mesh_id_bytes: Buffer.from(mesh_id),
        node_count: node_count,
        firmware_version: firmware_version,
        timestamp: timestamp,
        udp_port: udp_port || 8081,
        registered_at: existing ? existing.registered_at : Date.now(), // Preserve original registration time
        last_heartbeat: existing ? existing.last_heartbeat : null,
        last_state_update: existing ? existing.last_state_update : null,
        udp_failure_count: 0, // Always reset on re-registration (automatic recovery)
        is_offline: false // Always clear offline status on re-registration (automatic recovery)
    };

    // Detect IP change
    if (existing && existing.root_ip !== rootIpStr) {
        console.log(`[REGISTRATION] IP changed for mesh_id=${meshIdHex}: ${existing.root_ip} -> ${rootIpStr}`);
    }

    registrations.set(meshIdHex, registration);
    return registration;
}
```

### IP Change Handling

When a root node re-registers with a different IP address, the system updates the registration and logs the IP change event.

**Update Function**: `lyktparad-server/lib/registration.js`

```javascript
function updateRegistrationIp(meshIdHex, new_root_ip, new_udp_port = null) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }

    const newRootIpStr = `${new_root_ip[0]}.${new_root_ip[1]}.${new_root_ip[2]}.${new_root_ip[3]}`;
    registration.root_ip = newRootIpStr;
    registration.root_ip_bytes = Buffer.from(new_root_ip);

    if (new_udp_port !== null) {
        registration.udp_port = new_udp_port;
    }

    // Reset failure count and mark as online when IP is updated (re-registration)
    registration.udp_failure_count = 0;
    registration.is_offline = false;

    return true;
}
```

## Automatic Recovery

### Recovery Mechanism

When a root node re-registers after disconnection, the system automatically recovers and resumes normal operation without manual intervention.

**Recovery Triggers**:
1. **Re-registration**: Root node sends registration packet with same mesh ID
2. **Successful UDP Communication**: Root node responds to UDP command after being offline
3. **Heartbeat Received**: Root node sends heartbeat after being offline

**Recovery Actions**:
1. Update registration status to online
2. Reset UDP failure count to 0
3. Clear offline flag
4. Update last heartbeat/state update timestamp
5. Log recovery event
6. UI automatically updates to reflect recovered state

**Implementation**: Automatic recovery happens in multiple places:

**On Re-registration**: `lyktparad-server/lib/registration.js`

```javascript
function registerRootNode(...) {
    // ... registration logic ...
    udp_failure_count: 0, // Always reset on re-registration
    is_offline: false // Always clear offline status on re-registration
}
```

**On Successful UDP Communication**: `lyktparad-server/routes/proxy.js`

```javascript
// Successful UDP communication - reset failure count and mark as online
if (rootNode.registration) {
    rootNode.registration.udp_failure_count = 0;
    rootNode.registration.is_offline = false;
    updateLastHeartbeat(rootNode.registration.mesh_id);
}
```

**On Heartbeat Received**: `lyktparad-server/lib/registration.js`

```javascript
function updateLastHeartbeat(meshIdHex, timestamp = null) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }
    registration.last_heartbeat = timestamp || Date.now();
    registration.is_offline = false; // Mark as online when heartbeat received
    registration.udp_failure_count = 0; // Reset failure count on successful heartbeat
    return true;
}
```

**On State Update Received**: `lyktparad-server/lib/registration.js`

```javascript
function updateLastStateUpdate(meshIdHex, timestamp = null) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }
    registration.last_state_update = timestamp || Date.now();
    registration.is_offline = false; // Mark as online when state update received
    registration.udp_failure_count = 0; // Reset failure count on successful state update
    return true;
}
```

### UI Recovery Update

When the root node recovers, the web UI automatically updates to reflect the recovered state through periodic polling of the connection status API.

**Recovery Detection**: The connection status API returns `connected: true` and `status: "connected"` when the root node is online.

**UI Update**: The connection status indicator updates automatically through the periodic polling mechanism (every 5 seconds).

## Error Handling and Graceful Degradation

### Graceful Degradation Principles

The disconnection recovery system follows graceful degradation principles to ensure the external web server continues operating even when root nodes are offline.

**Key Principles**:
1. **Server Continues Operating**: External web server continues running when root nodes are offline
2. **Clear Error Messages**: Users receive clear, actionable error messages
3. **Direct Access Suggestion**: Users are informed they can access root nodes directly
4. **Automatic Recovery**: System recovers automatically when root nodes reconnect
5. **No Impact on ESP32**: ESP32 root nodes are unaffected by external server disconnection handling

### Error Handling Strategy

**API Proxy Errors**:
- All API proxy requests check registration status before attempting UDP communication
- Offline root nodes return 503 Service Unavailable immediately (no UDP attempt)
- UDP communication failures are tracked and handled appropriately
- Error responses include helpful suggestions for direct access

**State Updates**:
- State update endpoint (`GET /api/mesh/state`) returns cached state even if root is offline
- Stale state is indicated in response (if applicable)
- No errors returned for offline root nodes when retrieving cached state

**Connection Status API**:
- Always returns valid JSON response
- Indicates connection status clearly
- Provides root node IP address when available (for direct access)

### User Experience

**Error Messages**:
- Clear and descriptive
- Include actionable suggestions
- Show root node IP address when available
- Provide retry functionality when appropriate

**Status Indicators**:
- Visual connection status indicator
- Real-time status updates
- Color-coded for quick recognition
- Additional details available on hover/click

## Implementation Details

### File Structure

**Server-Side Implementation**:
- `lyktparad-server/lib/disconnection-detection.js`: Disconnection detection logic
- `lyktparad-server/lib/registration.js`: Registration storage and management
- `lyktparad-server/routes/proxy.js`: API proxy with error handling
- `lyktparad-server/server.js`: Connection status API endpoint
- `lyktparad-server/lib/udp-client.js`: UDP communication with failure tracking

**Client-Side Implementation**:
- `lyktparad-server/web-ui/js/app.js`: Connection status polling and error handling
- `lyktparad-server/web-ui/js/ota-api.js`: OTA API error handling
- `lyktparad-server/web-ui/index.html`: Connection status indicator HTML
- `lyktparad-server/web-ui/css/styles.css`: Connection status indicator styling

### Configuration

**Environment Variables**:
- `HEARTBEAT_TIMEOUT_MS`: Heartbeat timeout in milliseconds (default: 180000 = 3 minutes)
- `UDP_FAILURE_THRESHOLD`: UDP failure threshold count (default: 3)
- `CLEANUP_CHECK_INTERVAL_MS`: Cleanup check interval in milliseconds (default: 30000 = 30 seconds)

**Default Values**:
- Heartbeat timeout: 3 minutes
- UDP failure threshold: 3 consecutive failures
- Cleanup check interval: 30 seconds
- Cleanup timeout: 6 minutes (2x heartbeat timeout)

### Registration Storage

**Storage Structure**:
- In-memory Map keyed by mesh ID (hex string)
- Each registration contains:
  - `root_ip`: Root node IP address (string)
  - `mesh_id`: Mesh ID (hex string)
  - `node_count`: Number of connected nodes
  - `firmware_version`: Firmware version string
  - `registered_at`: Registration timestamp (milliseconds)
  - `last_heartbeat`: Last heartbeat timestamp (milliseconds, nullable)
  - `last_state_update`: Last state update timestamp (milliseconds, nullable)
  - `udp_port`: UDP port for communication
  - `udp_failure_count`: Current UDP failure count
  - `is_offline`: Offline flag (boolean)

**Storage Operations**:
- `registerRootNode()`: Register or update root node
- `getRegisteredRootNode()`: Get registration by mesh ID
- `getFirstRegisteredRootNode()`: Get first registration (for single mesh network)
- `removeRegistration()`: Remove registration
- `updateLastHeartbeat()`: Update last heartbeat timestamp
- `updateLastStateUpdate()`: Update last state update timestamp
- `incrementUdpFailureCount()`: Increment UDP failure count
- `markRegistrationOffline()`: Mark registration as offline
- `updateRegistrationIp()`: Update registration IP address

### Monitoring Lifecycle

**Start Monitoring**:
- Called automatically when server starts
- Sets up periodic interval for monitoring tasks
- Logs monitoring configuration

**Monitoring Tasks** (every 30 seconds):
1. Check all registrations for heartbeat timeout
2. Check all registrations for UDP failure threshold
3. Mark registrations as offline if conditions met
4. Clean up stale registrations

**Stop Monitoring**:
- Called when server shuts down (graceful shutdown)
- Clears monitoring interval
- Logs monitoring stop

## API Reference

### Connection Status API

**Endpoint**: `GET /api/connection/status`

**Description**: Returns real-time connection status information for the registered root node.

**Request**: No parameters required

**Response**:

```json
{
  "connected": boolean,
  "status": "connected" | "offline" | "not_registered" | "unknown" | "stale",
  "last_seen": "ISO timestamp" | null,
  "last_seen_ms": number | null,
  "time_since_last_seen_ms": number | null,
  "root_node_ip": "IP address" | null,
  "mesh_id": "mesh ID hex string" | null,
  "udp_port": number | null,
  "node_count": number | null,
  "firmware_version": "version string" | null,
  "registered_at": "ISO timestamp" | null,
  "udp_failure_count": number
}
```

**Status Values**:
- `"connected"`: Root node is online and reachable
- `"offline"`: Root node is offline (timeout or failure threshold exceeded)
- `"not_registered"`: No root node is registered
- `"unknown"`: Registration exists but no activity yet
- `"stale"`: Last activity was more than 1 minute ago but not yet offline

**Example Request**:
```bash
curl http://localhost:8080/api/connection/status
```

**Example Response** (Connected):
```json
{
  "connected": true,
  "status": "connected",
  "last_seen": "2025-01-15T10:35:00.000Z",
  "last_seen_ms": 1736943900000,
  "time_since_last_seen_ms": 5000,
  "root_node_ip": "192.168.1.100",
  "mesh_id": "aa:bb:cc:dd:ee:ff",
  "udp_port": 8081,
  "node_count": 5,
  "firmware_version": "1.0.0",
  "registered_at": "2025-01-15T10:00:00.000Z",
  "udp_failure_count": 0
}
```

**Example Response** (Offline):
```json
{
  "connected": false,
  "status": "offline",
  "last_seen": "2025-01-15T10:30:00.000Z",
  "last_seen_ms": 1736943000000,
  "time_since_last_seen_ms": 300000,
  "root_node_ip": "192.168.1.100",
  "mesh_id": "aa:bb:cc:dd:ee:ff",
  "udp_port": 8081,
  "node_count": 5,
  "firmware_version": "1.0.0",
  "registered_at": "2025-01-15T10:00:00.000Z",
  "udp_failure_count": 3
}
```

**Example Response** (Not Registered):
```json
{
  "connected": false,
  "status": "not_registered",
  "last_seen": null,
  "root_node_ip": null,
  "mesh_id": null
}
```

### Error Response Format

All API proxy endpoints return error responses in a consistent format when root node is unavailable:

**503 Service Unavailable** (Root offline):
```json
{
  "error": "Root node unavailable",
  "message": "The root node is currently offline or unreachable.",
  "code": 503,
  "suggestion": "You can access the root node directly via its IP address: http://192.168.1.100",
  "last_seen": "2025-01-15T10:30:00.000Z"
}
```

**404 Not Found** (No registration):
```json
{
  "error": "No root node registered",
  "message": "External web server has no registered root node. Please access the root node directly via its IP address.",
  "code": 404
}
```

## Integration Points

### Server Initialization

**Location**: `lyktparad-server/server.js`

**Integration**: Disconnection monitoring starts automatically when server starts.

```javascript
const { startMonitoring } = require('./lib/disconnection-detection');

// ... server setup ...

// Start disconnection monitoring
startMonitoring();
```

### Registration Handling

**Location**: `lyktparad-server/server.js`

**Integration**: Registration handler calls registration storage module.

```javascript
function handleRegistrationPacket(msg, rinfo) {
    // ... parse registration packet ...
    registerRootNode(root_ip, mesh_id, node_count, firmware_version, timestamp, udp_port);
}
```

### Heartbeat Handling

**Location**: `lyktparad-server/server.js`

**Integration**: Heartbeat handler updates last heartbeat timestamp.

```javascript
function handleHeartbeatPacket(msg, rinfo) {
    // ... parse heartbeat packet ...
    updateLastHeartbeat(meshIdHex);
}
```

### State Update Handling

**Location**: `lyktparad-server/server.js`

**Integration**: State update handler updates last state update timestamp.

```javascript
function handleStateUpdatePacket(msg, rinfo) {
    // ... parse state update packet ...
    updateLastStateUpdate(meshIdHex);
}
```

### API Proxy Integration

**Location**: `lyktparad-server/routes/proxy.js`

**Integration**: API proxy checks registration status and handles errors.

```javascript
async function processProxyRequest(req, res) {
    // Check registration status
    const rootNode = getRegisteredRootNode();
    if (!rootNode || rootNode.is_offline) {
        // Return 503 error
    }

    // Track UDP failures on error
    catch (error) {
        incrementUdpFailureCount(meshIdHex);
        // Mark offline if threshold exceeded
    }
}
```

### Web UI Integration

**Location**: `lyktparad-server/web-ui/js/app.js`

**Integration**: Web UI polls connection status and handles errors.

```javascript
// Poll connection status every 5 seconds
setInterval(updateConnectionStatus, 5000);

// Handle API errors
function handleApiError(error, feedbackElement, retryCallback) {
    // Check for disconnection errors
    // Display error with direct access suggestion
    // Add retry button
}
```

---

**Related Documentation**:
- [Root Node Registration Lifecycle](root-node-registration-lifecycle.md) - Complete registration and communication lifecycle
- [Web API Communication](web-api-communication.md) - API proxy and state update details
- [External Web Discovery](external-web-discovery.md) - Discovery mechanism details
