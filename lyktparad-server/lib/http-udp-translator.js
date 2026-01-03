/* HTTP to UDP Translation Module
 *
 * This module translates HTTP requests to UDP command packets.
 * Converts JSON request bodies to binary UDP payloads.
 *
 * Copyright (c) 2025 the_louie
 */

const { getCommandId } = require('./udp-commands');

/*******************************************************
 *                Sequence Number Generator
 *******************************************************/

let sequenceNumber = 0;

/**
 * Generate next sequence number.
 *
 * @returns {number} Sequence number (wraps around at 65535)
 */
function getNextSequenceNumber() {
    sequenceNumber = (sequenceNumber + 1) % 65536;
    return sequenceNumber;
}

/*******************************************************
 *                HTTP Request Parser
 *******************************************************/

/**
 * Parse HTTP request to extract method, path, and body.
 *
 * @param {Object} req - Express request object
 * @returns {Object} Parsed request { method, path, body }
 */
function parseHttpRequest(req) {
    // Handle binary body (for sequence endpoint)
    let body = req.body;
    if (Buffer.isBuffer(req.body)) {
        body = req.body;
    } else if (req.body === undefined || req.body === null) {
        body = {};
    }

    return {
        method: req.method,
        path: req.path,
        body: body
    };
}

/*******************************************************
 *                JSON to Binary Conversion
 *******************************************************/

/**
 * Convert JSON request body to binary payload for UDP.
 *
 * @param {number} commandId - UDP command ID
 * @param {Object} jsonBody - JSON request body
 * @returns {Buffer} Binary payload buffer
 */
function jsonToBinary(commandId, jsonBody) {
    const { UDP_CMD_API_COLOR_POST, UDP_CMD_API_SEQUENCE_POST, UDP_CMD_API_OTA_DOWNLOAD, UDP_CMD_API_OTA_REBOOT } = require('./udp-commands');

    switch (commandId) {
        case UDP_CMD_API_COLOR_POST:
            // POST /api/color: { r, g, b }
            // Binary: [r:1][g:1][b:1]
            const r = Math.max(0, Math.min(255, jsonBody.r || 0));
            const g = Math.max(0, Math.min(255, jsonBody.g || 0));
            const b = Math.max(0, Math.min(255, jsonBody.b || 0));
            return Buffer.from([r, g, b]);

        case UDP_CMD_API_SEQUENCE_POST:
            // POST /api/sequence: binary data (rhythm + length + color data)
            // Body is already a Buffer from express.raw() middleware
            if (Buffer.isBuffer(jsonBody)) {
                return jsonBody;
            }
            // If body is base64 string, decode it
            if (typeof jsonBody === 'string') {
                return Buffer.from(jsonBody, 'base64');
            }
            // If body is object with data field, use data
            if (jsonBody && jsonBody.data && Buffer.isBuffer(jsonBody.data)) {
                return jsonBody.data;
            }
            // Fallback: try to convert to buffer
            if (jsonBody && typeof jsonBody === 'object') {
                return Buffer.from(JSON.stringify(jsonBody));
            }
            // Empty buffer if no body
            return Buffer.alloc(0);

        case UDP_CMD_API_OTA_DOWNLOAD:
            // POST /api/ota/download: { url: "..." }
            // Binary: [url_len:1][url:N bytes, null-terminated]
            const url = jsonBody.url || '';
            const urlBuffer = Buffer.from(url, 'utf8');
            const urlLen = Math.min(255, urlBuffer.length);
            const result = Buffer.alloc(1 + urlLen);
            result[0] = urlLen;
            urlBuffer.copy(result, 1, 0, urlLen);
            return result;

        case UDP_CMD_API_OTA_REBOOT:
            // POST /api/ota/reboot: { timeout?: number, delay?: number } (optional)
            // Binary: [timeout:2 bytes, network byte order][delay:2 bytes, network byte order]
            // Defaults: timeout=10, delay=1000
            const timeout = Math.max(0, Math.min(65535, jsonBody.timeout || 10));
            const delay = Math.max(0, Math.min(65535, jsonBody.delay || 1000));
            const rebootPayload = Buffer.alloc(4);
            rebootPayload.writeUInt16BE(timeout, 0);
            rebootPayload.writeUInt16BE(delay, 2);
            return rebootPayload;

        default:
            // For GET requests or simple POST requests, return empty buffer
            // For other POST requests, serialize JSON to string and convert to buffer
            if (Object.keys(jsonBody).length === 0) {
                return Buffer.alloc(0);
            }
            const jsonStr = JSON.stringify(jsonBody);
            return Buffer.from(jsonStr, 'utf8');
    }
}

/*******************************************************
 *                UDP Packet Construction
 *******************************************************/

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
 * Build UDP packet from command ID and payload.
 *
 * UDP packet structure: [CMD:1][LEN:2][SEQ:2][PAYLOAD:N][CHKSUM:2]
 *
 * @param {number} commandId - UDP command ID
 * @param {Buffer} payload - Binary payload
 * @returns {Object} { packet, sequenceNumber } or throws error if packet too large
 */
function buildUdpPacket(commandId, payload) {
    const seqNum = getNextSequenceNumber();
    const payloadLen = payload.length;

    // Calculate total packet size: CMD(1) + LEN(2) + SEQ(2) + PAYLOAD(N) + CHKSUM(2)
    const packetSize = 1 + 2 + 2 + payloadLen + 2;

    // UDP MTU is typically 1472 bytes (1500 - 20 IP header - 8 UDP header)
    // Validate packet size to prevent fragmentation or packet loss
    const UDP_MTU = 1472;
    if (packetSize > UDP_MTU) {
        throw new Error(`UDP packet size (${packetSize} bytes) exceeds MTU limit (${UDP_MTU} bytes). Payload too large.`);
    }

    // Packet structure: [CMD:1][LEN:2][SEQ:2][PAYLOAD:N][CHKSUM:2]
    // First, build packet without checksum
    const packetWithoutChecksum = Buffer.alloc(1 + 2 + 2 + payloadLen);
    packetWithoutChecksum[0] = commandId;
    packetWithoutChecksum[1] = (payloadLen >> 8) & 0xFF; // Length MSB
    packetWithoutChecksum[2] = payloadLen & 0xFF; // Length LSB
    packetWithoutChecksum[3] = (seqNum >> 8) & 0xFF; // Sequence MSB
    packetWithoutChecksum[4] = seqNum & 0xFF; // Sequence LSB
    payload.copy(packetWithoutChecksum, 5);

    // Calculate checksum over packet without checksum bytes
    const checksum = calculateChecksum(packetWithoutChecksum);

    // Build final packet with checksum
    const packet = Buffer.alloc(packetWithoutChecksum.length + 2);
    packetWithoutChecksum.copy(packet, 0);
    packet[packet.length - 2] = (checksum >> 8) & 0xFF; // Checksum MSB
    packet[packet.length - 1] = checksum & 0xFF; // Checksum LSB

    return { packet, sequenceNumber: seqNum };
}

/*******************************************************
 *                HTTP to UDP Converter
 *******************************************************/

/**
 * Convert HTTP request to UDP command packet.
 *
 * @param {Object} req - Express request object
 * @returns {Object|null} { commandId, packet, sequenceNumber } or null if endpoint not found
 */
function httpToUdpCommand(req) {
    // Parse HTTP request
    const parsed = parseHttpRequest(req);

    // Get command ID from endpoint mapping
    const commandId = getCommandId(parsed.method, parsed.path);
    if (commandId === null) {
        return null;
    }

    // Convert JSON body to binary payload
    const payload = jsonToBinary(commandId, parsed.body);

    // Build UDP packet
    const { packet, sequenceNumber } = buildUdpPacket(commandId, payload);

    return {
        commandId: commandId,
        packet: packet,
        sequenceNumber: sequenceNumber
    };
}

module.exports = {
    parseHttpRequest,
    jsonToBinary,
    buildUdpPacket,
    httpToUdpCommand,
    getNextSequenceNumber,
    calculateChecksum
};
