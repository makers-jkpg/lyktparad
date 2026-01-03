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
const { registerRootNode } = require('./lib/registration');
const { proxyHandler } = require('./routes/proxy');
const { closeUdpSocket } = require('./lib/udp-client');

const app = express();
const PORT = process.env.PORT || 8080;
const UDP_PORT = process.env.UDP_PORT || 8081;
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

// API proxy routes (before static file serving)
app.all('/api/*', proxyHandler);

// Configure static file serving (must be after specific routes)
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

udpServer.on('message', (msg, rinfo) => {
    // Check if this is a registration packet
    if (msg.length >= 1 && msg[0] === UDP_CMD_REGISTRATION) {
        handleRegistrationPacket(msg, rinfo, udpServer);
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

    // Unregister mDNS service
    if (mdns) {
        const service = mdns.getService();
        if (service) {
            mdns.unregisterService(service);
        }
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
