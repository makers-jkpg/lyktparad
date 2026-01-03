/* Registration Storage Module
 *
 * This module stores and manages root node registrations.
 * Registrations are stored in memory (can be extended to use a database).
 *
 * Copyright (c) 2025 the_louie
 */

/**
 * Registration storage (in-memory)
 * Key: mesh_id (as hex string)
 * Value: { root_ip, mesh_id, node_count, firmware_version, timestamp, udp_port }
 */
const registrations = new Map();

/**
 * Register or update a root node.
 *
 * @param {Buffer} root_ip - IPv4 address (4 bytes, network byte order)
 * @param {Buffer} mesh_id - Mesh ID (6 bytes)
 * @param {number} node_count - Number of connected nodes
 * @param {string} firmware_version - Firmware version string
 * @param {number} timestamp - Unix timestamp
 * @param {number} udp_port - UDP port for communication
 * @returns {Object} Registration object
 */
function registerRootNode(root_ip, mesh_id, node_count, firmware_version, timestamp, udp_port) {
    // Convert mesh_id to hex string for key
    const meshIdHex = mesh_id.toString('hex');

    // Convert root_ip to string
    const rootIpStr = `${root_ip[0]}.${root_ip[1]}.${root_ip[2]}.${root_ip[3]}`;

    const registration = {
        root_ip: rootIpStr,
        root_ip_bytes: Buffer.from(root_ip),
        mesh_id: meshIdHex,
        mesh_id_bytes: Buffer.from(mesh_id),
        node_count: node_count,
        firmware_version: firmware_version,
        timestamp: timestamp,
        udp_port: udp_port || 8081, // Default UDP port
        registered_at: Date.now()
    };

    registrations.set(meshIdHex, registration);

    return registration;
}

/**
 * Get registered root node by mesh ID.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @returns {Object|null} Registration object, or null if not found
 */
function getRegisteredRootNode(meshIdHex) {
    return registrations.get(meshIdHex) || null;
}

/**
 * Get the first registered root node (for single mesh network).
 *
 * @returns {Object|null} Registration object, or null if none registered
 */
function getFirstRegisteredRootNode() {
    if (registrations.size === 0) {
        return null;
    }
    // Return first registration (for single mesh network)
    return registrations.values().next().value;
}

/**
 * Remove registration by mesh ID.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @returns {boolean} True if removed, false if not found
 */
function removeRegistration(meshIdHex) {
    return registrations.delete(meshIdHex);
}

/**
 * Clear all registrations.
 */
function clearAllRegistrations() {
    registrations.clear();
}

/**
 * Get all registrations.
 *
 * @returns {Array} Array of registration objects
 */
function getAllRegistrations() {
    return Array.from(registrations.values());
}

module.exports = {
    registerRootNode,
    getRegisteredRootNode,
    getFirstRegisteredRootNode,
    removeRegistration,
    clearAllRegistrations,
    getAllRegistrations
};
