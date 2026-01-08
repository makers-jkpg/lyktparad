/* UDP Command ID Mapping Module
 *
 * This module defines the mapping between HTTP API endpoints and UDP command IDs.
 * Command IDs 0xE7-0xEF are reserved for API commands (Web UI → Root node via external server).
 *
 * Copyright (c) 2025 the_louie
 */

/*******************************************************
 *                UDP Command ID Constants
 *******************************************************/

/* Plugin Web UI Command IDs (0xE5-0xE6) */
const UDP_CMD_API_PLUGIN_BUNDLE_GET = 0xE5;
const UDP_CMD_API_PLUGIN_DATA_POST = 0xE6;

/* API Command IDs (0xE7-0xEF) */
const UDP_CMD_API_NODES = 0xE7;
const UDP_CMD_API_COLOR_GET = 0xE8;
const UDP_CMD_API_COLOR_POST = 0xE9;
const UDP_CMD_API_SEQUENCE_POST = 0xEA;
const UDP_CMD_API_SEQUENCE_POINTER = 0xEB;
const UDP_CMD_API_SEQUENCE_START = 0xEC;
const UDP_CMD_API_SEQUENCE_STOP = 0xED;
const UDP_CMD_API_SEQUENCE_RESET = 0xEE;
const UDP_CMD_API_SEQUENCE_STATUS = 0xEF;

/* OTA Command IDs (extend range if needed, or use 0xF0-0xFF) */
/* For now, we'll use sequential IDs starting from 0xF0 */
const UDP_CMD_API_OTA_DOWNLOAD = 0xF0;
const UDP_CMD_API_OTA_STATUS = 0xF1;
const UDP_CMD_API_OTA_VERSION = 0xF2;
const UDP_CMD_API_OTA_CANCEL = 0xF3;
const UDP_CMD_API_OTA_DISTRIBUTE = 0xF4;
const UDP_CMD_API_OTA_DISTRIBUTION_STATUS = 0xF5;
const UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS = 0xF6;
const UDP_CMD_API_OTA_DISTRIBUTION_CANCEL = 0xF7;
const UDP_CMD_API_OTA_REBOOT = 0xF8;
const UDP_CMD_API_PLUGIN_ACTIVATE = 0xFA;
const UDP_CMD_API_PLUGIN_DEACTIVATE = 0xFB;
const UDP_CMD_API_PLUGIN_ACTIVE = 0xFC;
const UDP_CMD_API_PLUGINS_LIST = 0xFD;
const UDP_CMD_API_PLUGIN_STOP = 0xF9;
const UDP_CMD_API_PLUGIN_PAUSE = 0xFE;
const UDP_CMD_API_PLUGIN_RESET = 0xFF;

/*******************************************************
 *                Endpoint to Command ID Mapping
 *******************************************************/

/**
 * Maps HTTP method + path to UDP command ID.
 *
 * @param {string} method - HTTP method (GET, POST)
 * @param {string} path - HTTP path (e.g., "/api/nodes")
 * @returns {number|null} UDP command ID, or null if not found
 */
function getCommandId(method, path) {
    // Handle plugin web UI routes with parameters
    // Pattern: GET /api/plugin/:pluginName/bundle or POST /api/plugin/:pluginName/data
    const pluginBundleMatch = path.match(/^\/api\/plugin\/([^/]+)\/bundle$/);
    if (pluginBundleMatch && method === 'GET') {
        return UDP_CMD_API_PLUGIN_BUNDLE_GET;
    }
    const pluginDataMatch = path.match(/^\/api\/plugin\/([^/]+)\/data$/);
    if (pluginDataMatch && method === 'POST') {
        return UDP_CMD_API_PLUGIN_DATA_POST;
    }

    const key = `${method} ${path}`;
    const mapping = {
        'GET /api/nodes': UDP_CMD_API_NODES,
        'GET /api/color': UDP_CMD_API_COLOR_GET,
        'POST /api/color': UDP_CMD_API_COLOR_POST,
        'POST /api/sequence': UDP_CMD_API_SEQUENCE_POST,
        'GET /api/sequence/pointer': UDP_CMD_API_SEQUENCE_POINTER,
        'POST /api/sequence/start': UDP_CMD_API_SEQUENCE_START,
        'POST /api/sequence/stop': UDP_CMD_API_SEQUENCE_STOP,
        'POST /api/sequence/reset': UDP_CMD_API_SEQUENCE_RESET,
        'GET /api/sequence/status': UDP_CMD_API_SEQUENCE_STATUS,
        'POST /api/ota/download': UDP_CMD_API_OTA_DOWNLOAD,
        'GET /api/ota/status': UDP_CMD_API_OTA_STATUS,
        'GET /api/ota/version': UDP_CMD_API_OTA_VERSION,
        'POST /api/ota/cancel': UDP_CMD_API_OTA_CANCEL,
        'POST /api/ota/distribute': UDP_CMD_API_OTA_DISTRIBUTE,
        'GET /api/ota/distribution/status': UDP_CMD_API_OTA_DISTRIBUTION_STATUS,
        'GET /api/ota/distribution/progress': UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS,
        'POST /api/ota/distribution/cancel': UDP_CMD_API_OTA_DISTRIBUTION_CANCEL,
        'POST /api/ota/reboot': UDP_CMD_API_OTA_REBOOT,
        'POST /api/plugin/activate': UDP_CMD_API_PLUGIN_ACTIVATE,
        'POST /api/plugin/deactivate': UDP_CMD_API_PLUGIN_DEACTIVATE,
        'GET /api/plugin/active': UDP_CMD_API_PLUGIN_ACTIVE,
        'GET /api/plugins': UDP_CMD_API_PLUGINS_LIST,
        'POST /api/plugin/stop': UDP_CMD_API_PLUGIN_STOP,
        'POST /api/plugin/pause': UDP_CMD_API_PLUGIN_PAUSE,
        'POST /api/plugin/reset': UDP_CMD_API_PLUGIN_RESET
    };
    return mapping[key] || null;
}

/*******************************************************
 *                Command ID to Endpoint Mapping
 *******************************************************/

/**
 * Maps UDP command ID to endpoint information.
 *
 * @param {number} commandId - UDP command ID
 * @returns {Object|null} Object with method and path, or null if not found
 */
function getEndpointInfo(commandId) {
    const mapping = {
        [UDP_CMD_API_PLUGIN_BUNDLE_GET]: { method: 'GET', path: '/api/plugin/:pluginName/bundle' },
        [UDP_CMD_API_PLUGIN_DATA_POST]: { method: 'POST', path: '/api/plugin/:pluginName/data' },
        [UDP_CMD_API_NODES]: { method: 'GET', path: '/api/nodes' },
        [UDP_CMD_API_COLOR_GET]: { method: 'GET', path: '/api/color' },
        [UDP_CMD_API_COLOR_POST]: { method: 'POST', path: '/api/color' },
        [UDP_CMD_API_SEQUENCE_POST]: { method: 'POST', path: '/api/sequence' },
        [UDP_CMD_API_SEQUENCE_POINTER]: { method: 'GET', path: '/api/sequence/pointer' },
        [UDP_CMD_API_SEQUENCE_START]: { method: 'POST', path: '/api/sequence/start' },
        [UDP_CMD_API_SEQUENCE_STOP]: { method: 'POST', path: '/api/sequence/stop' },
        [UDP_CMD_API_SEQUENCE_RESET]: { method: 'POST', path: '/api/sequence/reset' },
        [UDP_CMD_API_SEQUENCE_STATUS]: { method: 'GET', path: '/api/sequence/status' },
        [UDP_CMD_API_OTA_DOWNLOAD]: { method: 'POST', path: '/api/ota/download' },
        [UDP_CMD_API_OTA_STATUS]: { method: 'GET', path: '/api/ota/status' },
        [UDP_CMD_API_OTA_VERSION]: { method: 'GET', path: '/api/ota/version' },
        [UDP_CMD_API_OTA_CANCEL]: { method: 'POST', path: '/api/ota/cancel' },
        [UDP_CMD_API_OTA_DISTRIBUTE]: { method: 'POST', path: '/api/ota/distribute' },
        [UDP_CMD_API_OTA_DISTRIBUTION_STATUS]: { method: 'GET', path: '/api/ota/distribution/status' },
        [UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS]: { method: 'GET', path: '/api/ota/distribution/progress' },
        [UDP_CMD_API_OTA_DISTRIBUTION_CANCEL]: { method: 'POST', path: '/api/ota/distribution/cancel' },
        [UDP_CMD_API_OTA_REBOOT]: { method: 'POST', path: '/api/ota/reboot' },
        [UDP_CMD_API_PLUGIN_ACTIVATE]: { method: 'POST', path: '/api/plugin/activate' },
        [UDP_CMD_API_PLUGIN_DEACTIVATE]: { method: 'POST', path: '/api/plugin/deactivate' },
        [UDP_CMD_API_PLUGIN_ACTIVE]: { method: 'GET', path: '/api/plugin/active' },
        [UDP_CMD_API_PLUGINS_LIST]: { method: 'GET', path: '/api/plugins' },
        [UDP_CMD_API_PLUGIN_STOP]: { method: 'POST', path: '/api/plugin/stop' },
        [UDP_CMD_API_PLUGIN_PAUSE]: { method: 'POST', path: '/api/plugin/pause' },
        [UDP_CMD_API_PLUGIN_RESET]: { method: 'POST', path: '/api/plugin/reset' }
    };
    return mapping[commandId] || null;
}

module.exports = {
    UDP_CMD_API_PLUGIN_BUNDLE_GET,
    UDP_CMD_API_PLUGIN_DATA_POST,
    UDP_CMD_API_NODES,
    UDP_CMD_API_COLOR_GET,
    UDP_CMD_API_COLOR_POST,
    UDP_CMD_API_SEQUENCE_POST,
    UDP_CMD_API_SEQUENCE_POINTER,
    UDP_CMD_API_SEQUENCE_START,
    UDP_CMD_API_SEQUENCE_STOP,
    UDP_CMD_API_SEQUENCE_RESET,
    UDP_CMD_API_SEQUENCE_STATUS,
    UDP_CMD_API_OTA_DOWNLOAD,
    UDP_CMD_API_OTA_STATUS,
    UDP_CMD_API_OTA_VERSION,
    UDP_CMD_API_OTA_CANCEL,
    UDP_CMD_API_OTA_DISTRIBUTE,
    UDP_CMD_API_OTA_DISTRIBUTION_STATUS,
    UDP_CMD_API_OTA_DISTRIBUTION_PROGRESS,
    UDP_CMD_API_OTA_DISTRIBUTION_CANCEL,
    UDP_CMD_API_OTA_REBOOT,
    UDP_CMD_API_PLUGIN_ACTIVATE,
    UDP_CMD_API_PLUGIN_DEACTIVATE,
    UDP_CMD_API_PLUGIN_ACTIVE,
    UDP_CMD_API_PLUGINS_LIST,
    UDP_CMD_API_PLUGIN_STOP,
    UDP_CMD_API_PLUGIN_PAUSE,
    UDP_CMD_API_PLUGIN_RESET,
    getCommandId,
    getEndpointInfo
};
