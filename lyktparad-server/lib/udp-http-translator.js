/* UDP to HTTP Translation Module
 *
 * This module translates UDP responses to HTTP responses.
 * Converts binary UDP payloads to JSON HTTP responses.
 *
 * Copyright (c) 2025 the_louie
 */

const { getEndpointInfo } = require('./udp-commands');

/*******************************************************
 *                Binary to JSON Conversion
 *******************************************************/

/**
 * Convert binary UDP payload to JSON response.
 *
 * @param {number} commandId - UDP command ID
 * @param {Buffer} payload - Binary payload
 * @returns {Object} JSON response object
 */
function binaryToJson(commandId, payload) {
    const { UDP_CMD_API_NODES, UDP_CMD_API_COLOR_GET, UDP_CMD_API_SEQUENCE_POINTER, UDP_CMD_API_SEQUENCE_STATUS, UDP_CMD_API_OTA_STATUS, UDP_CMD_API_OTA_VERSION, UDP_CMD_API_OTA_DISTRIBUTION_STATUS, UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS, UDP_CMD_API_PLUGIN_ACTIVATE, UDP_CMD_API_PLUGIN_DEACTIVATE, UDP_CMD_API_PLUGIN_ACTIVE, UDP_CMD_API_PLUGINS_LIST } = require('./udp-commands');

    switch (commandId) {
        case UDP_CMD_API_NODES:
            // GET /api/nodes: { nodes: number }
            // Binary: [node_count:4 bytes, network byte order] or [node_count:1 byte]
            // Try 4-byte first, then 1-byte
            let nodeCount;
            if (payload.length >= 4) {
                nodeCount = payload.readUInt32BE(0);
            } else if (payload.length >= 1) {
                nodeCount = payload[0];
            } else {
                nodeCount = 0;
            }
            return { nodes: nodeCount };

        case UDP_CMD_API_COLOR_GET:
            // GET /api/color: { r, g, b, is_set }
            // Binary: [r:1][g:1][b:1][is_set:1]
            if (payload.length >= 4) {
                return {
                    r: payload[0],
                    g: payload[1],
                    b: payload[2],
                    is_set: payload[3] !== 0
                };
            }
            return { r: 0, g: 0, b: 0, is_set: false };

        case UDP_CMD_API_SEQUENCE_POINTER:
            // GET /api/sequence/pointer: plain text number
            // Binary: [pointer:2 bytes, network byte order] or [pointer:1 byte]
            let pointer;
            if (payload.length >= 2) {
                pointer = payload.readUInt16BE(0);
            } else if (payload.length >= 1) {
                pointer = payload[0];
            } else {
                pointer = 0;
            }
            return { pointer: pointer };

        case UDP_CMD_API_SEQUENCE_STATUS:
            // GET /api/sequence/status: { active: boolean }
            // Binary: [active:1] (0=false, 1=true)
            const active = payload.length >= 1 && payload[0] !== 0;
            return { active: active };

        case UDP_CMD_API_OTA_STATUS:
            // GET /api/ota/status: { downloading: boolean, progress: number }
            // Binary: [downloading:1][progress:4 bytes float or 1 byte 0-100]
            if (payload.length >= 5) {
                const downloading = payload[0] !== 0;
                const progress = payload.readFloatBE(1);
                return { downloading: downloading, progress: progress };
            } else if (payload.length >= 2) {
                const downloading = payload[0] !== 0;
                const progress = payload[1] / 100.0;
                return { downloading: downloading, progress: progress };
            }
            return { downloading: false, progress: 0.0 };

        case UDP_CMD_API_OTA_VERSION:
            // GET /api/ota/version: { version: string }
            // Binary: [version_len:1][version:N bytes, null-terminated]
            if (payload.length >= 1) {
                const versionLen = payload[0];
                if (payload.length >= 1 + versionLen) {
                    const version = payload.slice(1, 1 + versionLen).toString('utf8').replace(/\0/g, '');
                    return { version: version };
                }
            }
            return { version: 'unknown' };

        case UDP_CMD_API_OTA_DISTRIBUTION_STATUS:
            // GET /api/ota/distribution/status: { distributing: boolean, ... }
            // Binary: [distributing:1][...]
            const distributing = payload.length >= 1 && payload[0] !== 0;
            return { distributing: distributing };

        case UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS:
            // GET /api/ota/distribution/progress: { progress: number, ... }
            // Binary: [progress:1 byte 0-100] or [progress:4 bytes float]
            let progress;
            if (payload.length >= 4) {
                progress = payload.readFloatBE(0);
            } else if (payload.length >= 1) {
                progress = payload[0] / 100.0;
            } else {
                progress = 0.0;
            }
            return { progress: progress };

        case UDP_CMD_API_PLUGIN_ACTIVATE:
        case UDP_CMD_API_PLUGIN_DEACTIVATE:
            // POST /api/plugin/activate or /api/plugin/deactivate
            // Response: [success:1][name_len:1][name:N bytes]
            if (payload.length < 1) {
                return { success: false, error: 'Invalid response' };
            }
            const success = payload[0] !== 0;
            if (success && payload.length >= 2) {
                const nameLen = payload[1];
                if (payload.length >= 2 + nameLen) {
                    const name = payload.slice(2, 2 + nameLen).toString('utf8');
                    return { success: true, plugin: name };
                }
            }
            return { success: false, error: 'Activation failed' };

        case UDP_CMD_API_PLUGIN_ACTIVE:
            // GET /api/plugin/active: { active: string | null }
            // Response: [name_len:1][name:N bytes] or [0] if no active plugin
            if (payload.length < 1) {
                return { active: null };
            }
            const activeNameLen = payload[0];
            if (activeNameLen == 0) {
                return { active: null };
            }
            if (payload.length >= 1 + activeNameLen) {
                const activeName = payload.slice(1, 1 + activeNameLen).toString('utf8');
                return { active: activeName };
            }
            return { active: null };

        case UDP_CMD_API_PLUGINS_LIST:
            // GET /api/plugins: { plugins: string[] }
            // Response: [count:1][name1_len:1][name1:N bytes][name2_len:1][name2:N bytes]...
            if (payload.length < 1) {
                return { plugins: [] };
            }
            const pluginCount = payload[0];
            const plugins = [];
            let offset = 1;
            for (let i = 0; i < pluginCount && offset < payload.length; i++) {
                if (offset >= payload.length) break;
                const pluginNameLen = payload[offset++];
                if (offset + pluginNameLen > payload.length) break;
                const pluginName = payload.slice(offset, offset + pluginNameLen).toString('utf8');
                plugins.push(pluginName);
                offset += pluginNameLen;
            }
            return { plugins: plugins };

        default:
            // For POST requests or unknown commands, try to parse as JSON string
            // or return success/failure based on payload
            if (payload.length === 0) {
                return { success: true };
            }
            // Try to parse as JSON string
            try {
                const jsonStr = payload.toString('utf8').replace(/\0/g, '');
                return JSON.parse(jsonStr);
            } catch (e) {
                // If not JSON, return success with payload as string
                return { success: true, data: payload.toString('utf8') };
            }
    }
}

/*******************************************************
 *                HTTP Response Construction
 *******************************************************/

/**
 * Map UDP response to HTTP status code.
 *
 * @param {number} commandId - UDP command ID
 * @param {Object} jsonData - JSON response data
 * @returns {number} HTTP status code
 */
function mapToHttpStatus(commandId, jsonData) {
    // Check for error indicators in JSON
    if (jsonData.error) {
        if (jsonData.error.includes('Forbidden') || jsonData.error.includes('Only root')) {
            return 403;
        }
        if (jsonData.error.includes('Bad Request') || jsonData.error.includes('Invalid')) {
            return 400;
        }
        if (jsonData.error.includes('Conflict')) {
            return 409;
        }
        return 500;
    }

    // Check for success indicator
    if (jsonData.success === false) {
        return 500;
    }

    // Default to 200 OK
    return 200;
}

/**
 * Build HTTP response object.
 *
 * @param {Object} jsonData - JSON response data
 * @param {number} statusCode - HTTP status code
 * @returns {Object} HTTP response object { status, json }
 */
function buildHttpResponse(jsonData, statusCode) {
    return {
        status: statusCode,
        json: jsonData
    };
}

/*******************************************************
 *                UDP to HTTP Converter
 *******************************************************/

/**
 * Convert UDP response to HTTP response.
 *
 * @param {Object} udpResponse - UDP response { commandId, payload, sequenceNumber }
 * @returns {Object} HTTP response { status, json, text } (text is set for plain text responses)
 */
function udpToHttpResponse(udpResponse) {
    const { UDP_CMD_API_SEQUENCE_POINTER } = require('./udp-commands');

    // Special case: /api/sequence/pointer returns plain text, not JSON
    if (udpResponse.commandId === UDP_CMD_API_SEQUENCE_POINTER) {
        // Convert binary payload to pointer value
        let pointer;
        if (udpResponse.payload.length >= 2) {
            pointer = udpResponse.payload.readUInt16BE(0);
        } else if (udpResponse.payload.length >= 1) {
            pointer = udpResponse.payload[0];
        } else {
            pointer = 0;
        }

        // Return plain text response
        return {
            status: 200,
            text: String(pointer)
        };
    }

    // Convert binary payload to JSON for other endpoints
    const jsonData = binaryToJson(udpResponse.commandId, udpResponse.payload);

    // Map to HTTP status code
    const statusCode = mapToHttpStatus(udpResponse.commandId, jsonData);

    // Build HTTP response
    return buildHttpResponse(jsonData, statusCode);
}

module.exports = {
    binaryToJson,
    mapToHttpStatus,
    buildHttpResponse,
    udpToHttpResponse
};
