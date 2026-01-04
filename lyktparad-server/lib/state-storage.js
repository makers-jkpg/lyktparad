/* State Storage Module
 *
 * This module stores and manages mesh state updates from root nodes.
 * State is stored in memory (can be extended to use a database).
 *
 * Copyright (c) 2025 the_louie
 */

/**
 * State storage (in-memory)
 * Key: mesh_id (as hex string)
 * Value: { root_ip, mesh_id, timestamp, mesh_state, node_count, nodes, sequence_state, ota_state, updated_at }
 */
const states = new Map();

/**
 * Store or update mesh state.
 *
 * @param {Buffer} root_ip - IPv4 address (4 bytes, network byte order)
 * @param {Buffer} mesh_id - Mesh ID (6 bytes)
 * @param {number} timestamp - Unix timestamp (network byte order)
 * @param {number} mesh_state - Mesh state (0=disconnected, 1=connected)
 * @param {number} node_count - Number of connected nodes
 * @param {Array} nodes - Array of node entries
 * @param {Object} sequence_state - Sequence state { active, position, total }
 * @param {Object} ota_state - OTA state { in_progress, progress }
 * @returns {Object} State object
 */
function storeMeshState(root_ip, mesh_id, timestamp, mesh_state, node_count, nodes, sequence_state, ota_state) {
    // Convert mesh_id to hex string for key
    const meshIdHex = mesh_id.toString('hex');

    // Convert root_ip to string
    const rootIpStr = `${root_ip[0]}.${root_ip[1]}.${root_ip[2]}.${root_ip[3]}`;

    const state = {
        root_ip: rootIpStr,
        root_ip_bytes: Buffer.from(root_ip),
        mesh_id: meshIdHex,
        mesh_id_bytes: Buffer.from(mesh_id),
        timestamp: timestamp,
        mesh_state: mesh_state,
        node_count: node_count,
        nodes: nodes || [],
        sequence_state: sequence_state || { active: 0, position: 0, total: 0 },
        ota_state: ota_state || { in_progress: 0, progress: 0 },
        updated_at: Date.now()
    };

    states.set(meshIdHex, state);

    return state;
}

/**
 * Get mesh state by mesh ID.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @returns {Object|null} State object, or null if not found
 */
function getMeshState(meshIdHex) {
    return states.get(meshIdHex) || null;
}

/**
 * Get the first mesh state (for single mesh network).
 *
 * @returns {Object|null} State object, or null if none stored
 */
function getFirstMeshState() {
    if (states.size === 0) {
        return null;
    }
    // Return first state (for single mesh network)
    return states.values().next().value;
}

/**
 * Remove state by mesh ID.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @returns {boolean} True if removed, false if not found
 */
function removeMeshState(meshIdHex) {
    return states.delete(meshIdHex);
}

/**
 * Clear all states.
 */
function clearAllStates() {
    states.clear();
}

/**
 * Check if state is stale (older than maxAge milliseconds).
 *
 * @param {Object} state - State object
 * @param {number} maxAge - Maximum age in milliseconds (default: 10 seconds)
 * @returns {boolean} True if stale, false if fresh
 */
function isStateStale(state, maxAge = 10000) {
    if (!state || !state.updated_at) {
        return true;
    }
    return (Date.now() - state.updated_at) > maxAge;
}

module.exports = {
    storeMeshState,
    getMeshState,
    getFirstMeshState,
    removeMeshState,
    clearAllStates,
    isStateStale
};
