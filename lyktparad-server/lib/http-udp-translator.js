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
 * Validate plugin name format (alphanumeric, underscore, hyphen only).
 *
 * @param {string} name - Plugin name to validate
 * @returns {boolean} True if valid
 */
function isValidPluginName(name) {
    if (!name || typeof name !== 'string') {
        return false;
    }
    // Regex: ^[a-zA-Z0-9_-]+$
    return /^[a-zA-Z0-9_-]+$/.test(name);
}

/**
 * Parse HTTP request to extract method, path, body, and plugin name (if applicable).
 *
 * @param {Object} req - Express request object
 * @returns {Object} Parsed request { method, path, body, pluginName? }
 */
function parseHttpRequest(req) {
    // Handle binary body (for sequence endpoint and plugin data endpoint)
    let body = req.body;
    if (Buffer.isBuffer(req.body)) {
        body = req.body;
    } else if (req.body === undefined || req.body === null) {
        body = {};
    }

    const parsed = {
        method: req.method,
        path: req.path,
        body: body
    };

    // Extract plugin name from route parameters for plugin web UI endpoints
    // Pattern: /api/plugin/:pluginName/bundle or /api/plugin/:pluginName/data
    const pluginBundleMatch = req.path.match(/^\/api\/plugin\/([^/]+)\/bundle$/);
    const pluginDataMatch = req.path.match(/^\/api\/plugin\/([^/]+)\/data$/);

    if (pluginBundleMatch || pluginDataMatch) {
        const pluginName = pluginBundleMatch ? pluginBundleMatch[1] : pluginDataMatch[1];

        // Validate plugin name format
        if (!isValidPluginName(pluginName)) {
            throw new Error('Invalid plugin name format. Plugin name must contain only alphanumeric characters, underscores, and hyphens.');
        }

        parsed.pluginName = pluginName;
    }

    return parsed;
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
function jsonToBinary(commandId, jsonBody, parsedRequest) {
    const { UDP_CMD_API_COLOR_POST, UDP_CMD_API_SEQUENCE_POST, UDP_CMD_API_OTA_DOWNLOAD, UDP_CMD_API_OTA_REBOOT, UDP_CMD_API_PLUGIN_ACTIVATE, UDP_CMD_API_PLUGIN_DEACTIVATE, UDP_CMD_API_PLUGIN_BUNDLE_GET, UDP_CMD_API_PLUGIN_DATA_POST } = require('./udp-commands');

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

        case UDP_CMD_API_PLUGIN_ACTIVATE:
        case UDP_CMD_API_PLUGIN_DEACTIVATE:
        case UDP_CMD_API_PLUGIN_STOP:
        case UDP_CMD_API_PLUGIN_PAUSE:
        case UDP_CMD_API_PLUGIN_RESET:
            // POST /api/plugin/activate, /api/plugin/deactivate, /api/plugin/stop, /api/plugin/pause, /api/plugin/reset: { "name": "effects" }
            // Binary: [name_len:1][name:N bytes]
            const name = jsonBody.name || '';
            const nameBytes = Buffer.from(name, 'utf8');
            if (nameBytes.length > 63) {
                throw new Error('Plugin name too long (max 63 bytes)');
            }
            const buffer = Buffer.alloc(1 + nameBytes.length);
            buffer[0] = nameBytes.length;
            nameBytes.copy(buffer, 1);
            return buffer;

        case UDP_CMD_API_PLUGIN_BUNDLE_GET:
            // GET /api/plugin/:pluginName/bundle
            // Binary: [plugin_name_len:1][plugin_name:N bytes, UTF-8]
            if (!parsedRequest || !parsedRequest.pluginName) {
                throw new Error('Plugin name not found in request');
            }
            const bundlePluginName = parsedRequest.pluginName;
            const bundleNameBytes = Buffer.from(bundlePluginName, 'utf8');
            if (bundleNameBytes.length > 63) {
                throw new Error('Plugin name too long (max 63 bytes)');
            }
            const bundleBuffer = Buffer.alloc(1 + bundleNameBytes.length);
            bundleBuffer[0] = bundleNameBytes.length;
            bundleNameBytes.copy(bundleBuffer, 1);
            return bundleBuffer;

        case UDP_CMD_API_PLUGIN_DATA_POST:
            // POST /api/plugin/:pluginName/data
            // Binary: [plugin_name_len:1][plugin_name:N bytes][data:N bytes]
            if (!parsedRequest || !parsedRequest.pluginName) {
                throw new Error('Plugin name not found in request');
            }
            const dataPluginName = parsedRequest.pluginName;
            const dataNameBytes = Buffer.from(dataPluginName, 'utf8');
            if (dataNameBytes.length > 63) {
                throw new Error('Plugin name too long (max 63 bytes)');
            }

            // Read request body as binary (Buffer)
            let dataPayload;
            if (Buffer.isBuffer(jsonBody)) {
                dataPayload = jsonBody;
            } else if (typeof jsonBody === 'string') {
                dataPayload = Buffer.from(jsonBody, 'base64');
            } else if (jsonBody && jsonBody.data && Buffer.isBuffer(jsonBody.data)) {
                dataPayload = jsonBody.data;
            } else {
                dataPayload = Buffer.alloc(0);
            }

            // Validate total payload size (max 512 bytes data + name + headers < 1400 bytes)
            // Name (max 63) + 1 byte length + data (max 512) = 576 bytes max payload
            // Plus UDP headers (7 bytes) = 583 bytes < 1400 bytes (safe)
            if (dataPayload.length > 512) {
                throw new Error('Data payload too large (max 512 bytes)');
            }

            const dataBuffer = Buffer.alloc(1 + dataNameBytes.length + dataPayload.length);
            dataBuffer[0] = dataNameBytes.length;
            dataNameBytes.copy(dataBuffer, 1);
            dataPayload.copy(dataBuffer, 1 + dataNameBytes.length);
            return dataBuffer;

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
    const { UDP_CMD_API_PLUGIN_BUNDLE_GET } = require('./udp-commands');
    const seqNum = getNextSequenceNumber();
    const payloadLen = payload.length;

    // Bundle endpoint specific size limit: ~1400 bytes payload (after headers)
    // UDP packet headers: CMD(1) + LEN(2) + SEQ(2) + CHKSUM(2) = 7 bytes
    // Max payload for bundle: 1472 - 7 = 1465 bytes, but use ~1400 for safety margin
    if (commandId === UDP_CMD_API_PLUGIN_BUNDLE_GET) {
        const MAX_BUNDLE_PAYLOAD = 1400;
        if (payloadLen > MAX_BUNDLE_PAYLOAD) {
            throw new Error(`Bundle size (${payloadLen} bytes) exceeds UDP MTU limit (${MAX_BUNDLE_PAYLOAD} bytes payload). Bundle too large.`);
        }
    }

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

    // Convert JSON body to binary payload (pass parsed request for plugin name extraction)
    const payload = jsonToBinary(commandId, parsed.body, parsed);

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
    calculateChecksum,
    isValidPluginName
};
