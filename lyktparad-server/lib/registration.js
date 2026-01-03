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
 * Value: { root_ip, mesh_id, node_count, firmware_version, timestamp, udp_port, last_heartbeat, udp_failure_count, is_offline }
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

    // Check if registration already exists
    const existing = registrations.get(meshIdHex);

    const registration = {
        root_ip: rootIpStr,
        root_ip_bytes: Buffer.from(root_ip),
        mesh_id: meshIdHex,
        mesh_id_bytes: Buffer.from(mesh_id),
        node_count: node_count,
        firmware_version: firmware_version,
        timestamp: timestamp,
        udp_port: udp_port || 8081, // Default UDP port
        registered_at: existing ? existing.registered_at : Date.now(), // Preserve original registration time
        last_heartbeat: existing ? existing.last_heartbeat : null, // Preserve existing heartbeat if updating
        last_state_update: existing ? existing.last_state_update : null, // Preserve existing state update if updating
        udp_failure_count: 0, // Always reset on re-registration (automatic recovery)
        is_offline: false // Always clear offline status on re-registration (automatic recovery)
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

/**
 * Update last heartbeat timestamp for a registration.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @param {number} timestamp - Unix timestamp (optional, defaults to current time)
 * @returns {boolean} True if updated, false if not found
 */
function updateLastHeartbeat(meshIdHex, timestamp = null) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }
    registration.last_heartbeat = timestamp || Date.now();
    registration.is_offline = false; // Mark as online when heartbeat received
    registration.udp_failure_count = 0; // Reset failure count on successful heartbeat
    return true;
}

/**
 * Update last state update timestamp for a registration.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @param {number} timestamp - Unix timestamp (optional, defaults to current time)
 * @returns {boolean} True if updated, false if not found
 */
function updateLastStateUpdate(meshIdHex, timestamp = null) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }
    registration.last_state_update = timestamp || Date.now();
    registration.is_offline = false; // Mark as online when state update received
    registration.udp_failure_count = 0; // Reset failure count on successful state update
    return true;
}

/**
 * Increment UDP failure count for a registration.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @returns {boolean} True if updated, false if not found
 */
function incrementUdpFailureCount(meshIdHex) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }
    registration.udp_failure_count = (registration.udp_failure_count || 0) + 1;
    return true;
}

/**
 * Mark registration as offline.
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @returns {boolean} True if updated, false if not found
 */
function markRegistrationOffline(meshIdHex) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }
    registration.is_offline = true;
    return true;
}

/**
 * Update registration IP address (for IP change detection).
 *
 * @param {string} meshIdHex - Mesh ID as hex string
 * @param {Buffer} new_root_ip - New IPv4 address (4 bytes, network byte order)
 * @param {number} new_udp_port - New UDP port (optional)
 * @returns {boolean} True if updated, false if not found
 */
function updateRegistrationIp(meshIdHex, new_root_ip, new_udp_port = null) {
    const registration = registrations.get(meshIdHex);
    if (!registration) {
        return false;
    }
    const newRootIpStr = `${new_root_ip[0]}.${new_root_ip[1]}.${new_root_ip[2]}.${new_root_ip[3]}`;
    registration.root_ip = newRootIpStr;
    registration.root_ip_bytes = Buffer.from(new_root_ip);
    if (new_udp_port !== null) {
        registration.udp_port = new_udp_port;
    }
    // Reset failure count and mark as online when IP is updated (re-registration)
    registration.udp_failure_count = 0;
    registration.is_offline = false;
    return true;
}

module.exports = {
    registerRootNode,
    getRegisteredRootNode,
    getFirstRegisteredRootNode,
    removeRegistration,
    clearAllRegistrations,
    getAllRegistrations,
    updateLastHeartbeat,
    updateLastStateUpdate,
    incrementUdpFailureCount,
    markRegistrationOffline,
    updateRegistrationIp
};
