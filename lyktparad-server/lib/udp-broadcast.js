/* UDP Broadcast Module
 *
 * This module provides UDP broadcast discovery for the external web server.
 * The server periodically broadcasts its presence on a known UDP port so that
 * ESP32 root nodes can discover it when mDNS is unavailable.
 *
 * Copyright (c) 2025 the_louie
 */

const dgram = require('dgram');

/**
 * UDP broadcast state
 */
let broadcastSocket = null;
let broadcastInterval = null;
let isBroadcasting = false;

/**
 * Default broadcast configuration
 */
const DEFAULT_BROADCAST_PORT = 5353;
const DEFAULT_BROADCAST_INTERVAL = 30000; // 30 seconds

/**
 * Build broadcast payload as JSON.
 *
 * @param {Object} config - Broadcast configuration
 * @param {string} config.service - Service name (default: "lyktparad-web")
 * @param {number} config.port - HTTP port (default: 8080)
 * @param {number} config.udpPort - UDP port (default: 8081)
 * @param {string} config.protocol - Protocol (default: "udp")
 * @param {string} config.version - Server version (default: "1.0.0")
 * @returns {string} JSON payload string
 */
function buildBroadcastPayload(config = {}) {
    const payload = {
        service: config.service || 'lyktparad-web',
        port: config.port || 8080,
        udp_port: config.udpPort || 8081,
        protocol: config.protocol || 'udp',
        version: config.version || '1.0.0'
    };
    return JSON.stringify(payload);
}

/**
 * Send a single UDP broadcast packet.
 *
 * @param {Object} config - Broadcast configuration
 * @param {number} config.broadcastPort - Broadcast port (default: 5353)
 * @param {string} config.service - Service name
 * @param {number} config.port - HTTP port
 * @param {number} config.udpPort - UDP port
 * @param {string} config.protocol - Protocol
 * @param {string} config.version - Server version
 * @returns {boolean} True if send succeeded, false otherwise
 */
function sendBroadcast(config = {}) {
    if (!broadcastSocket) {
        console.warn('UDP broadcast: Socket not initialized, cannot send broadcast');
        return false;
    }

    const broadcastPort = config.broadcastPort || DEFAULT_BROADCAST_PORT;
    const broadcastAddress = '255.255.255.255';

    try {
        const payload = buildBroadcastPayload(config);
        const payloadBuffer = Buffer.from(payload, 'utf8');

        broadcastSocket.send(payloadBuffer, broadcastPort, broadcastAddress, (err) => {
            if (err) {
                console.debug(`UDP broadcast: Send error (non-critical): ${err.message}`);
            } else {
                console.debug(`UDP broadcast: Sent to ${broadcastAddress}:${broadcastPort} (${payloadBuffer.length} bytes)`);
            }
        });

        return true;
    } catch (error) {
        console.warn(`UDP broadcast: Send exception (non-critical): ${error.message}`);
        return false;
    }
}

/**
 * Start periodic UDP broadcast.
 *
 * @param {Object} config - Broadcast configuration
 * @param {number} config.broadcastPort - Broadcast port (default: 5353)
 * @param {number} config.broadcastInterval - Broadcast interval in milliseconds (default: 30000)
 * @param {string} config.service - Service name (default: "lyktparad-web")
 * @param {number} config.port - HTTP port
 * @param {number} config.udpPort - UDP port
 * @param {string} config.protocol - Protocol (default: "udp")
 * @param {string} config.version - Server version
 * @returns {boolean} True if started successfully, false otherwise
 */
function startBroadcast(config = {}) {
    if (isBroadcasting) {
        console.warn('UDP broadcast: Already broadcasting, ignoring start request');
        return false;
    }

    try {
        // Create UDP socket
        broadcastSocket = dgram.createSocket('udp4');

        // Enable broadcast
        broadcastSocket.setBroadcast(true);

        // Handle socket errors (non-critical, continue broadcasting)
        broadcastSocket.on('error', (err) => {
            console.warn(`UDP broadcast: Socket error (non-critical): ${err.message}`);
        });

        // Send initial broadcast immediately
        const broadcastIntervalMs = config.broadcastInterval || DEFAULT_BROADCAST_INTERVAL;
        sendBroadcast(config);

        // Set up periodic broadcast
        broadcastInterval = setInterval(() => {
            sendBroadcast(config);
        }, broadcastIntervalMs);

        isBroadcasting = true;
        console.log(`UDP broadcast: Started (interval: ${broadcastIntervalMs}ms, port: ${config.broadcastPort || DEFAULT_BROADCAST_PORT})`);
        return true;
    } catch (error) {
        console.error(`UDP broadcast: Failed to start: ${error.message}`);
        stopBroadcast(); // Clean up on error
        return false;
    }
}

/**
 * Stop periodic UDP broadcast.
 *
 * Cleans up the broadcast socket and interval timer.
 */
function stopBroadcast() {
    if (!isBroadcasting) {
        return; // Already stopped
    }

    // Clear interval timer
    if (broadcastInterval) {
        clearInterval(broadcastInterval);
        broadcastInterval = null;
    }

    // Close UDP socket
    if (broadcastSocket) {
        try {
            broadcastSocket.close();
        } catch (error) {
            console.warn(`UDP broadcast: Error closing socket: ${error.message}`);
        }
        broadcastSocket = null;
    }

    isBroadcasting = false;
    console.log('UDP broadcast: Stopped');
}

module.exports = {
    startBroadcast,
    stopBroadcast,
    sendBroadcast,
    buildBroadcastPayload
};
