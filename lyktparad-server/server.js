/* External Web Server for lyktparad
 *
 * This is an optional external web server that hosts the web UI and provides
 * a foundation for API proxying. The server is completely optional -
 * ESP32 mesh devices work perfectly without it.
 *
 * Copyright (c) 2025 the_louie
 */

const express = require('express');
const cors = require('cors');
const path = require('path');
const dgram = require('dgram');

// Try to load mDNS module (optional)
let mdns = null;
try {
    mdns = require('./mdns');
} catch (err) {
    // mDNS module unavailable, server continues without it
    console.warn('mDNS: Module not available, server will run without mDNS');
}

// Load modules
const { registerRootNode, updateLastHeartbeat, updateLastStateUpdate, updateRegistrationIp, getFirstRegisteredRootNode } = require('./lib/registration');
const { proxyHandler } = require('./routes/proxy');
const { closeUdpSocket } = require('./lib/udp-client');
const { storeMeshState, getFirstMeshState, isStateStale } = require('./lib/state-storage');
const { startBroadcast, stopBroadcast } = require('./lib/udp-broadcast');
const { startMonitoring, stopMonitoring } = require('./lib/disconnection-detection');

const app = express();
const PORT = process.env.PORT || 8080;
const UDP_PORT = process.env.UDP_PORT || 8081;
const BROADCAST_PORT = process.env.BROADCAST_PORT || 5353;
const BROADCAST_INTERVAL = process.env.BROADCAST_INTERVAL ? parseInt(process.env.BROADCAST_INTERVAL, 10) : 30000;
const serverStartTime = Date.now();

// Server version (from package.json or constant)
const SERVER_VERSION = require('./package.json').version || '1.0.0';
// Configure CORS for development (allows all origins)
// Note: Should be restricted in production
app.use(cors({
    origin: '*',
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['Content-Type']
}));

// Raw body parsing for binary data (sequence endpoint) - must be before JSON parser
app.use('/api/sequence', express.raw({ type: 'application/octet-stream', limit: '500kb' }));

// JSON body parsing middleware (applied after raw parser for sequence endpoint)
app.use(express.json());

// Health check endpoint (defined before static file serving for better performance)
app.get('/health', (req, res) => {
    const uptime = Math.floor((Date.now() - serverStartTime) / 1000);
    res.json({
        status: 'ok',
        server: {
            port: PORT,
            uptime: uptime,
            timestamp: new Date().toISOString()
        }
    });
});

// API: GET /api/mesh/state - Returns latest mesh state
app.get('/api/mesh/state', (req, res) => {
    const state = getFirstMeshState();
    if (!state) {
        res.status(404).json({ error: 'No mesh state available' });
        return;
    }

    // Check if state is stale
    if (isStateStale(state, 10000)) { // 10 seconds
        res.json({
            ...state,
            stale: true,
            warning: 'State may be outdated'
        });
        return;
    }

    // Convert to JSON format
    const jsonState = {
        root_ip: state.root_ip,
        mesh_id: state.mesh_id,
        timestamp: state.timestamp,
        timestamp_iso: new Date(state.timestamp * 1000).toISOString(),
        mesh_state: state.mesh_state === 1 ? 'connected' : 'disconnected',
        node_count: state.node_count,
        nodes: state.nodes.map(node => ({
            node_id: node.node_id,
            ip: node.ip,
            layer: node.layer,
            parent_id: node.parent_id,
            role: node.role === 0 ? 'root' : node.role === 1 ? 'child' : 'leaf',
            status: node.status === 1 ? 'connected' : 'disconnected'
        })),
        sequence: {
            active: state.sequence_state.active === 1,
            position: state.sequence_state.position,
            total: state.sequence_state.total
        },
        ota: {
            in_progress: state.ota_state.in_progress === 1,
            progress: state.ota_state.progress
        },
        updated_at: new Date(state.updated_at).toISOString(),
        stale: false
    };

    res.json(jsonState);
});

// API: GET /api/connection/status - Returns connection status
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

// API proxy routes (before static file serving)
app.all('/api/*', proxyHandler);

// Configure static file serving (must be after specific routes)
// Serve plugin files from web-ui/plugins/ at /plugins/
app.use('/plugins', express.static(path.join(__dirname, 'web-ui/plugins'), {
    // Serve plugin files with proper MIME types
    setHeaders: (res, filePath) => {
        if (filePath.endsWith('.js')) {
            res.setHeader('Content-Type', 'application/javascript');
        } else if (filePath.endsWith('.css')) {
            res.setHeader('Content-Type', 'text/css');
        }
    }
}));

// Serve web-ui directory as root, so index.html is accessible at /
app.use(express.static(path.join(__dirname, 'web-ui'), {
    // Ensure index.html is served for root path
    index: 'index.html'
}));

/*******************************************************
 *                UDP Registration Handler
 *******************************************************/

/**
 * UDP packet structure: [CMD:1][LEN:2][PAYLOAD:N][CHKSUM:2]
 * Registration payload: [root_ip:4][mesh_id:6][node_count:1][firmware_version_len:1][firmware_version:N][timestamp:4]
 */
const UDP_CMD_REGISTRATION = 0xE0;
const UDP_CMD_REGISTRATION_ACK = 0xE3;
const UDP_CMD_HEARTBEAT = 0xE1;
const UDP_CMD_STATE_UPDATE = 0xE2;

/**
 * Calculate simple checksum (16-bit sum of all bytes).
 *
 * @param {Buffer} data - Data buffer
 * @returns {number} 16-bit checksum
 */
function calculateChecksum(data) {
    let sum = 0;
    for (let i = 0; i < data.length; i++) {
        sum = (sum + data[i]) & 0xFFFF;
    }
    return sum;
}

/**
 * Handle UDP registration packet.
 *
 * @param {Buffer} msg - UDP message
 * @param {Object} rinfo - Remote address info
 * @param {dgram.Socket} socket - UDP socket
 */
function handleRegistrationPacket(msg, rinfo, socket) {
    // Minimum packet size: [CMD:1][LEN:2][CHKSUM:2] = 5 bytes
    if (msg.length < 5) {
        console.warn('Registration packet too short:', msg.length);
        return;
    }

    // Parse packet: [CMD:1][LEN:2][PAYLOAD:N][CHKSUM:2]
    const commandId = msg[0];
    if (commandId !== UDP_CMD_REGISTRATION) {
        return; // Not a registration packet
    }

    const payloadLen = (msg[1] << 8) | msg[2];
    const checksum = (msg[msg.length - 2] << 8) | msg[msg.length - 1];

    // Verify packet size
    const expectedSize = 1 + 2 + payloadLen + 2; // CMD + LEN + PAYLOAD + CHKSUM
    if (msg.length !== expectedSize) {
        console.warn(`Registration packet size mismatch: expected ${expectedSize}, got ${msg.length}`);
        return;
    }

    // Verify checksum
    const packetWithoutChecksum = msg.slice(0, msg.length - 2);
    const calculatedChecksum = calculateChecksum(packetWithoutChecksum);
    if (checksum !== calculatedChecksum) {
        console.warn(`Registration packet checksum mismatch: expected ${checksum}, got ${calculatedChecksum}`);
        // Continue anyway (checksum verification is optional)
    }

    // Extract payload: [root_ip:4][mesh_id:6][node_count:1][firmware_version_len:1][firmware_version:N][timestamp:4]
    const payload = msg.slice(3, 3 + payloadLen);

    if (payload.length < 4 + 6 + 1 + 1 + 1 + 4) { // Minimum: root_ip + mesh_id + node_count + version_len + 1 char version + timestamp
        console.warn('Registration payload too short:', payload.length);
        return;
    }

    // Parse payload
    const root_ip = payload.slice(0, 4);
    const mesh_id = payload.slice(4, 10);
    const node_count = payload[10];
    const firmware_version_len = payload[11];
    const firmware_version = payload.slice(12, 12 + firmware_version_len).toString('utf8').replace(/\0/g, '');
    const timestamp = payload.readUInt32BE(12 + firmware_version_len);

    // Register root node
    try {
        const meshIdHex = mesh_id.toString('hex');
        const existingRegistration = require('./lib/registration').getRegisteredRootNode(meshIdHex);

        // Check if IP address changed (re-registration with new IP)
        if (existingRegistration) {
            const newRootIpStr = `${root_ip[0]}.${root_ip[1]}.${root_ip[2]}.${root_ip[3]}`;
            if (existingRegistration.root_ip !== newRootIpStr) {
                console.log(`Root node IP changed: ${existingRegistration.root_ip} -> ${newRootIpStr}, mesh_id: ${meshIdHex}`);
                // Note: registerRootNode() below will create a new registration with the new IP,
                // so we don't need to call updateRegistrationIp() here (it would be redundant)
            }
        }

        const registration = registerRootNode(
            root_ip,
            mesh_id,
            node_count,
            firmware_version,
            timestamp,
            UDP_PORT // Use configured UDP port
        );

        console.log(`Root node registered: ${registration.root_ip}, mesh_id: ${registration.mesh_id}, nodes: ${registration.node_count}`);

        // Send ACK: [CMD:0xE3][LEN:2][STATUS:1][CHKSUM:2]
        // STATUS: 0=success, 1=failure
        const ackPayload = Buffer.from([0]); // STATUS = 0 (success)
        const ackPacketWithoutChecksum = Buffer.alloc(1 + 2 + 1);
        ackPacketWithoutChecksum[0] = UDP_CMD_REGISTRATION_ACK;
        ackPacketWithoutChecksum[1] = (ackPayload.length >> 8) & 0xFF;
        ackPacketWithoutChecksum[2] = ackPayload.length & 0xFF;
        ackPayload.copy(ackPacketWithoutChecksum, 3);

        const ackChecksum = calculateChecksum(ackPacketWithoutChecksum);
        const ackPacket = Buffer.alloc(ackPacketWithoutChecksum.length + 2);
        ackPacketWithoutChecksum.copy(ackPacket, 0);
        ackPacket[ackPacket.length - 2] = (ackChecksum >> 8) & 0xFF;
        ackPacket[ackPacket.length - 1] = ackChecksum & 0xFF;

        socket.send(ackPacket, rinfo.port, rinfo.address, (err) => {
            if (err) {
                console.error('Failed to send registration ACK:', err);
            }
        });

    } catch (error) {
        console.error('Registration error:', error);

        // Send failure ACK
        const ackPayload = Buffer.from([1]); // STATUS = 1 (failure)
        const ackPacketWithoutChecksum = Buffer.alloc(1 + 2 + 1);
        ackPacketWithoutChecksum[0] = UDP_CMD_REGISTRATION_ACK;
        ackPacketWithoutChecksum[1] = (ackPayload.length >> 8) & 0xFF;
        ackPacketWithoutChecksum[2] = ackPayload.length & 0xFF;
        ackPayload.copy(ackPacketWithoutChecksum, 3);

        const ackChecksum = calculateChecksum(ackPacketWithoutChecksum);
        const ackPacket = Buffer.alloc(ackPacketWithoutChecksum.length + 2);
        ackPacketWithoutChecksum.copy(ackPacket, 0);
        ackPacket[ackPacket.length - 2] = (ackChecksum >> 8) & 0xFF;
        ackPacket[ackPacket.length - 1] = ackChecksum & 0xFF;

        socket.send(ackPacket, rinfo.port, rinfo.address, (err) => {
            if (err) {
                console.error('Failed to send registration failure ACK:', err);
            }
        });
    }
}

/*******************************************************
 *                UDP Server Setup
 *******************************************************/

// Create UDP socket for registration and API commands
const udpServer = dgram.createSocket('udp4');

/**
 * Handle state update packet (command 0xE2).
 *
 * @param {Buffer} msg - UDP message
 * @param {Object} rinfo - Remote address info
 */
function handleStateUpdatePacket(msg, rinfo) {
    // Minimum packet size: [CMD:1][LEN:2][CHKSUM:2] = 5 bytes
    if (msg.length < 5) {
        console.warn('State update packet too short:', msg.length);
        return;
    }

    // Parse packet: [CMD:0xE2][LEN:2][PAYLOAD:N][CHKSUM:2]
    const commandId = msg[0];
    if (commandId !== UDP_CMD_STATE_UPDATE) {
        return; // Not a state update packet
    }

    const payloadLen = (msg[1] << 8) | msg[2];
    const checksum = (msg[msg.length - 2] << 8) | msg[msg.length - 1];

    // Verify packet size
    const expectedSize = 1 + 2 + payloadLen + 2; // CMD + LEN + PAYLOAD + CHKSUM
    if (msg.length !== expectedSize) {
        console.warn(`State update packet size mismatch: expected ${expectedSize}, got ${msg.length}`);
        return;
    }

    // Verify checksum
    const packetWithoutChecksum = msg.slice(0, msg.length - 2);
    const calculatedChecksum = calculateChecksum(packetWithoutChecksum);
    if (checksum !== calculatedChecksum) {
        console.warn(`State update packet checksum mismatch: expected ${checksum}, got ${calculatedChecksum}`);
        // Continue anyway (checksum verification is optional)
    }

    // Extract payload
    const payload = msg.slice(3, 3 + payloadLen);

    // Minimum payload: root_ip(4) + mesh_id(6) + timestamp(4) + mesh_state(1) + node_count(1) + sequence(5) + ota(2) = 23 bytes
    if (payload.length < 23) {
        console.warn('State update payload too short:', payload.length);
        return;
    }

    // Parse payload: [root_ip:4][mesh_id:6][timestamp:4][mesh_state:1][node_count:1][nodes:N][sequence_active:1][sequence_position:2][sequence_total:2][ota_in_progress:1][ota_progress:1]
    let offset = 0;

    // Root IP (4 bytes, network byte order)
    const root_ip = payload.slice(offset, offset + 4);
    offset += 4;

    // Mesh ID (6 bytes)
    const mesh_id = payload.slice(offset, offset + 6);
    offset += 6;

    // Timestamp (4 bytes, network byte order)
    const timestamp = payload.readUInt32BE(offset);
    offset += 4;

    // Mesh state (1 byte)
    const mesh_state = payload[offset++];

    // Node count (1 byte)
    const node_count = payload[offset++];

    // Node list (N * node_entry)
    const nodes = [];
    for (let i = 0; i < node_count; i++) {
        if (offset + 19 > payload.length) {
            console.warn('State update payload truncated at node list');
            break;
        }

        const node = {
            node_id: payload.slice(offset, offset + 6).toString('hex'),
            ip: `${payload[offset + 6]}.${payload[offset + 7]}.${payload[offset + 8]}.${payload[offset + 9]}`,
            layer: payload[offset + 10],
            parent_id: payload.slice(offset + 11, offset + 17).toString('hex'),
            role: payload[offset + 17],
            status: payload[offset + 18]
        };
        nodes.push(node);
        offset += 19; // 6 + 4 + 1 + 6 + 1 + 1 = 19 bytes per node
    }

    // Sequence state
    if (offset + 5 > payload.length) {
        console.warn('State update payload truncated at sequence state');
        return;
    }
    const sequence_active = payload[offset++];
    const sequence_position = payload.readUInt16BE(offset);
    offset += 2;
    const sequence_total = payload.readUInt16BE(offset);
    offset += 2;

    // OTA state
    if (offset + 2 > payload.length) {
        console.warn('State update payload truncated at OTA state');
        return;
    }
    const ota_in_progress = payload[offset++];
    const ota_progress = payload[offset++];

    // Store state
    try {
        storeMeshState(
            root_ip,
            mesh_id,
            timestamp,
            mesh_state,
            node_count,
            nodes,
            {
                active: sequence_active,
                position: sequence_position,
                total: sequence_total
            },
            {
                in_progress: ota_in_progress,
                progress: ota_progress
            }
        );

        // Update last state update timestamp (also acts as heartbeat)
        const meshIdHex = mesh_id.toString('hex');

        // Check if IP address changed (detect IP changes in state updates)
        const { getRegisteredRootNode } = require('./lib/registration');
        const existingRegistration = getRegisteredRootNode(meshIdHex);
        if (existingRegistration) {
            const newRootIpStr = `${root_ip[0]}.${root_ip[1]}.${root_ip[2]}.${root_ip[3]}`;
            if (existingRegistration.root_ip !== newRootIpStr) {
                console.log(`Root node IP changed (detected in state update): ${existingRegistration.root_ip} -> ${newRootIpStr}, mesh_id: ${meshIdHex}`);
                updateRegistrationIp(meshIdHex, root_ip, null); // Update IP, preserve UDP port
            }
        }

        updateLastStateUpdate(meshIdHex, Date.now());

        console.log(`Mesh state updated: mesh_id=${mesh_id.toString('hex')}, nodes=${node_count}, sequence_active=${sequence_active}, ota_in_progress=${ota_in_progress}`);
    } catch (error) {
        console.error('State update storage error:', error);
    }
}

/**
 * Handle heartbeat packet (command 0xE1).
 *
 * @param {Buffer} msg - UDP message
 * @param {Object} rinfo - Remote address info
 */
function handleHeartbeatPacket(msg, rinfo) {
    // Minimum packet size: [CMD:1][LEN:2][CHKSUM:2] = 5 bytes
    if (msg.length < 5) {
        console.warn('Heartbeat packet too short:', msg.length);
        return;
    }

    // Parse packet: [CMD:0xE1][LEN:2][PAYLOAD:N][CHKSUM:2]
    const commandId = msg[0];
    if (commandId !== UDP_CMD_HEARTBEAT) {
        return; // Not a heartbeat packet
    }

    const payloadLen = (msg[1] << 8) | msg[2];
    const checksum = (msg[msg.length - 2] << 8) | msg[msg.length - 1];

    // Verify packet size
    const expectedSize = 1 + 2 + payloadLen + 2; // CMD + LEN + PAYLOAD + CHKSUM
    if (msg.length !== expectedSize) {
        console.warn(`Heartbeat packet size mismatch: expected ${expectedSize}, got ${msg.length}`);
        return;
    }

    // Verify checksum (optional)
    const packetWithoutChecksum = msg.slice(0, msg.length - 2);
    const calculatedChecksum = calculateChecksum(packetWithoutChecksum);
    if (checksum !== calculatedChecksum) {
        console.warn(`Heartbeat packet checksum mismatch: expected ${checksum}, got ${calculatedChecksum}`);
        // Continue anyway (checksum verification is optional)
    }

    // Extract payload: [timestamp: 4 bytes] [node_count: 1 byte, optional]
    const payload = msg.slice(3, 3 + payloadLen);

    // Minimum payload: timestamp (4 bytes)
    if (payload.length < 4) {
        console.warn('Heartbeat payload too short:', payload.length);
        return;
    }

    // Parse payload
    const timestamp = payload.readUInt32BE(0); // Unix timestamp (network byte order)
    const node_count = payload.length >= 5 ? payload[4] : null; // Optional node count

    // Find registration by source IP address
    const sourceIp = rinfo.address;
    const { getAllRegistrations, updateRegistrationIp } = require('./lib/registration');
    const registrations = getAllRegistrations();

    // Find registration matching source IP
    let registration = registrations.find(reg => reg.root_ip === sourceIp);

    // If not found by IP, try to find by all registrations (for IP change detection)
    // Note: Heartbeat packets don't contain mesh_id, so we can only detect IP changes
    // if we have a single registration. State updates handle IP changes more reliably.
    if (!registration && registrations.length === 1) {
        registration = registrations[0];
        // Update IP address if it changed
        const newIpBytes = sourceIp.split('.').map(Number);
        const newIpBuffer = Buffer.from(newIpBytes);
        console.log(`Root node IP changed (detected in heartbeat): ${registration.root_ip} -> ${sourceIp}, mesh_id: ${registration.mesh_id}`);
        updateRegistrationIp(registration.mesh_id, newIpBuffer, null);
        registration.root_ip = sourceIp; // Update local reference
    }

    if (registration) {
        // Update last heartbeat timestamp
        updateLastHeartbeat(registration.mesh_id, Date.now());

        // Optionally update node count if provided
        if (node_count !== null) {
            registration.node_count = node_count;
        }

        // Log heartbeat (debug level, not too verbose)
        // console.log(`Heartbeat received: mesh_id=${registration.mesh_id}, nodes=${node_count || registration.node_count}`);
    } else {
        // Heartbeat from unregistered node - log but don't error
        console.warn(`Heartbeat received from unregistered node: ${sourceIp}`);
    }
}

udpServer.on('message', (msg, rinfo) => {
    // Check if this is a registration packet
    if (msg.length >= 1 && msg[0] === UDP_CMD_REGISTRATION) {
        handleRegistrationPacket(msg, rinfo, udpServer);
    } else if (msg.length >= 1 && msg[0] === UDP_CMD_HEARTBEAT) {
        handleHeartbeatPacket(msg, rinfo);
    } else if (msg.length >= 1 && msg[0] === UDP_CMD_STATE_UPDATE) {
        handleStateUpdatePacket(msg, rinfo);
    }
    // Other UDP commands (API responses) are handled by udp-client.js
});

udpServer.on('error', (err) => {
    console.error('UDP server error:', err);
});

udpServer.bind(UDP_PORT, () => {
    console.log(`UDP server listening on port ${UDP_PORT}`);
});

/*******************************************************
 *                HTTP Server Setup
 *******************************************************/

// Start server
const server = app.listen(PORT, () => {
    console.log(`lyktparad-server listening on port ${PORT}`);
    console.log(`Web UI available at http://localhost:${PORT}`);
    console.log(`Health check available at http://localhost:${PORT}/health`);
    console.log(`UDP server listening on port ${UDP_PORT}`);

    // Register mDNS service after server starts listening
    if (mdns) {
        const serviceName = 'Lyktparad Web Server';
        const metadata = {
            version: SERVER_VERSION,
            protocol: 'udp',
            udp_port: parseInt(UDP_PORT, 10)
        };

        mdns.registerService(PORT, serviceName, metadata);
    }

    // Start UDP broadcast (fallback discovery mechanism)
    startBroadcast({
        broadcastPort: BROADCAST_PORT,
        broadcastInterval: BROADCAST_INTERVAL,
        service: 'lyktparad-web',
        port: parseInt(PORT, 10),
        udpPort: parseInt(UDP_PORT, 10),
        protocol: 'udp',
        version: SERVER_VERSION
    });

    // Start disconnection monitoring
    startMonitoring();
});

// Error handling for port conflicts
server.on('error', (err) => {
    if (err.code === 'EADDRINUSE') {
        console.error(`Port ${PORT} is already in use. Please set PORT environment variable to use a different port.`);
    } else {
        console.error('Server error:', err);
    }
    process.exit(1);
});

/*******************************************************
 *                Graceful Shutdown
 *******************************************************/

function gracefulShutdown() {
    console.log('\nShutting down gracefully...');

    // Stop disconnection monitoring
    stopMonitoring();

    // Stop UDP broadcast
    stopBroadcast();

    // Unregister mDNS service and cleanup resources
    if (mdns) {
        const service = mdns.getService();
        if (service) {
            mdns.unregisterService(service);
        }
        // Cleanup bonjour instance resources
        mdns.cleanup();
    }

    // Close UDP socket
    closeUdpSocket();
    udpServer.close();

    // Close HTTP server
    server.close(() => {
        console.log('Server closed');
        process.exit(0);
    });

    // Force shutdown after 10 seconds
    setTimeout(() => {
        console.error('Forced shutdown after timeout');
        process.exit(1);
    }, 10000);
}

// Handle shutdown signals
process.on('SIGTERM', gracefulShutdown);
process.on('SIGINT', gracefulShutdown);
