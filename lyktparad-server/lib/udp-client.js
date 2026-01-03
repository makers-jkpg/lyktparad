/* UDP Client Module
 *
 * This module provides UDP socket client functionality for communicating
 * with ESP32 root nodes. Handles sending UDP packets, receiving responses,
 * timeout handling, and request/response matching via sequence numbers.
 *
 * Copyright (c) 2025 the_louie
 */

const dgram = require('dgram');

/*******************************************************
 *                Pending Requests Storage
 *******************************************************/

/**
 * Pending requests map: sequenceNumber -> { resolve, reject, timeout }
 */
const pendingRequests = new Map();

/**
 * Cleanup timeout for pending requests (5 minutes)
 */
const PENDING_REQUEST_TIMEOUT = 5 * 60 * 1000;

/**
 * Clean up old pending requests periodically
 */
setInterval(() => {
    const now = Date.now();
    for (const [seqNum, request] of pendingRequests.entries()) {
        if (now - request.timestamp > PENDING_REQUEST_TIMEOUT) {
            pendingRequests.delete(seqNum);
            if (request.reject) {
                request.reject(new Error('Request timeout (cleanup)'));
            }
        }
    }
}, 60000); // Check every minute

/*******************************************************
 *                UDP Socket Management
 *******************************************************/

let udpSocket = null;

/**
 * Get or create UDP socket.
 *
 * @returns {dgram.Socket} UDP socket
 */
function getUdpSocket() {
    if (udpSocket === null) {
        udpSocket = dgram.createSocket('udp4');

        // Handle incoming messages
        udpSocket.on('message', (msg, rinfo) => {
            handleUdpResponse(msg, rinfo);
        });

        // Handle errors
        udpSocket.on('error', (err) => {
            console.error('UDP socket error:', err);
        });
    }
    return udpSocket;
}

/*******************************************************
 *                UDP Response Handler
 *******************************************************/

/**
 * Handle incoming UDP response.
 *
 * @param {Buffer} msg - Received UDP message
 * @param {Object} rinfo - Remote address info
 */
function handleUdpResponse(msg, rinfo) {
    // Minimum packet size: [CMD:1][LEN:2][SEQ:2][CHKSUM:2] = 7 bytes
    if (msg.length < 7) {
        console.warn('UDP response too short:', msg.length);
        return;
    }

    // Parse packet: [CMD:1][LEN:2][SEQ:2][PAYLOAD:N][CHKSUM:2]
    const commandId = msg[0];
    const payloadLen = (msg[1] << 8) | msg[2];
    const seqNum = (msg[3] << 8) | msg[4];
    const checksum = (msg[msg.length - 2] << 8) | msg[msg.length - 1];

    // Verify packet size
    const expectedSize = 1 + 2 + 2 + payloadLen + 2; // CMD + LEN + SEQ + PAYLOAD + CHKSUM
    if (msg.length !== expectedSize) {
        console.warn(`UDP response size mismatch: expected ${expectedSize}, got ${msg.length}`);
        return;
    }

    // Verify checksum (optional, but recommended)
    const packetWithoutChecksum = msg.slice(0, msg.length - 2);
    const calculatedChecksum = calculateChecksum(packetWithoutChecksum);
    if (checksum !== calculatedChecksum) {
        console.warn(`UDP response checksum mismatch: expected ${checksum}, got ${calculatedChecksum}`);
        // Continue anyway (checksum is optional for some commands)
    }

    // Extract payload
    const payload = msg.slice(5, 5 + payloadLen);

    // Find pending request
    const pendingRequest = pendingRequests.get(seqNum);
    if (!pendingRequest) {
        console.warn(`UDP response for unknown sequence number: ${seqNum}`);
        return;
    }

    // Remove from pending requests
    pendingRequests.delete(seqNum);

    // Clear timeout
    if (pendingRequest.timeout) {
        clearTimeout(pendingRequest.timeout);
    }

    // Resolve promise with response
    if (pendingRequest.resolve) {
        pendingRequest.resolve({
            commandId: commandId,
            payload: payload,
            sequenceNumber: seqNum
        });
    }
}

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

/*******************************************************
 *                UDP Send/Receive Functions
 *******************************************************/

/**
 * Send UDP command and wait for response.
 *
 * @param {string} ip - Target IP address
 * @param {number} port - Target UDP port
 * @param {Buffer} packet - UDP packet to send
 * @param {number} sequenceNumber - Sequence number for matching
 * @param {number} timeout - Timeout in milliseconds (default: 5000)
 * @returns {Promise<Object>} Promise that resolves with response { commandId, payload, sequenceNumber }
 */
function sendUdpCommandAndWait(ip, port, packet, sequenceNumber, timeout = 5000) {
    return new Promise((resolve, reject) => {
        // Store pending request
        const pendingRequest = {
            resolve: resolve,
            reject: reject,
            timestamp: Date.now()
        };
        pendingRequests.set(sequenceNumber, pendingRequest);

        // Set timeout
        pendingRequest.timeout = setTimeout(() => {
            pendingRequests.delete(sequenceNumber);
            reject(new Error('UDP response timeout'));
        }, timeout);

        // Get UDP socket
        const socket = getUdpSocket();

        // Send packet
        socket.send(packet, port, ip, (err) => {
            if (err) {
                // Clean up on send error
                pendingRequests.delete(sequenceNumber);
                if (pendingRequest.timeout) {
                    clearTimeout(pendingRequest.timeout);
                }
                reject(err);
            }
            // If send succeeds, wait for response (handled in handleUdpResponse)
        });
    });
}

/**
 * Send UDP command without waiting for response (fire-and-forget).
 *
 * @param {string} ip - Target IP address
 * @param {number} port - Target UDP port
 * @param {Buffer} packet - UDP packet to send
 * @returns {Promise<void>} Promise that resolves when packet is sent
 */
function sendUdpCommand(ip, port, packet) {
    return new Promise((resolve, reject) => {
        const socket = getUdpSocket();
        socket.send(packet, port, ip, (err) => {
            if (err) {
                reject(err);
            } else {
                resolve();
            }
        });
    });
}

/**
 * Close UDP socket (cleanup).
 */
function closeUdpSocket() {
    if (udpSocket !== null) {
        udpSocket.close();
        udpSocket = null;
    }
    // Clear all pending requests
    for (const [seqNum, request] of pendingRequests.entries()) {
        if (request.timeout) {
            clearTimeout(request.timeout);
        }
        if (request.reject) {
            request.reject(new Error('UDP socket closed'));
        }
    }
    pendingRequests.clear();
}

module.exports = {
    sendUdpCommandAndWait,
    sendUdpCommand,
    closeUdpSocket,
    getUdpSocket
};
