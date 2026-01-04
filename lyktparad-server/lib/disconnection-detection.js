/* Disconnection Detection Module
 *
 * This module monitors root node registrations for disconnection scenarios,
 * including heartbeat timeout detection, UDP communication failure tracking,
 * and stale registration cleanup.
 *
 * Copyright (c) 2025 the_louie
 */

const { getAllRegistrations, markRegistrationOffline, removeRegistration } = require('./registration');

/*******************************************************
 *                Configuration
 *******************************************************/

/**
 * Default heartbeat timeout (3 minutes = 180000 ms)
 * Configurable via environment variable HEARTBEAT_TIMEOUT_MS
 */
const DEFAULT_HEARTBEAT_TIMEOUT_MS = 3 * 60 * 1000; // 3 minutes
const HEARTBEAT_TIMEOUT_MS = process.env.HEARTBEAT_TIMEOUT_MS
    ? parseInt(process.env.HEARTBEAT_TIMEOUT_MS, 10)
    : DEFAULT_HEARTBEAT_TIMEOUT_MS;

/**
 * UDP failure threshold (3 consecutive failures)
 * Configurable via environment variable UDP_FAILURE_THRESHOLD
 */
const DEFAULT_UDP_FAILURE_THRESHOLD = 3;
const UDP_FAILURE_THRESHOLD = process.env.UDP_FAILURE_THRESHOLD
    ? parseInt(process.env.UDP_FAILURE_THRESHOLD, 10)
    : DEFAULT_UDP_FAILURE_THRESHOLD;

/**
 * Cleanup check interval (30 seconds)
 * Configurable via environment variable CLEANUP_CHECK_INTERVAL_MS
 */
const DEFAULT_CLEANUP_CHECK_INTERVAL_MS = 30 * 1000; // 30 seconds
const CLEANUP_CHECK_INTERVAL_MS = process.env.CLEANUP_CHECK_INTERVAL_MS
    ? parseInt(process.env.CLEANUP_CHECK_INTERVAL_MS, 10)
    : DEFAULT_CLEANUP_CHECK_INTERVAL_MS;

/*******************************************************
 *                Heartbeat Timeout Detection
 *******************************************************/

/**
 * Check if registration has exceeded heartbeat timeout.
 *
 * @param {Object} registration - Registration object
 * @returns {boolean} True if timeout exceeded, false otherwise
 */
function isHeartbeatTimeout(registration) {
    if (!registration.last_heartbeat && !registration.last_state_update) {
        // No heartbeat or state update ever received - consider offline if registered more than timeout ago
        const registeredAgo = Date.now() - registration.registered_at;
        return registeredAgo > HEARTBEAT_TIMEOUT_MS;
    }

    // Use last_heartbeat if available, otherwise use last_state_update
    const lastActivity = registration.last_heartbeat || registration.last_state_update;
    if (!lastActivity) {
        return false; // Should not happen, but be safe
    }

    const timeSinceActivity = Date.now() - lastActivity;
    return timeSinceActivity > HEARTBEAT_TIMEOUT_MS;
}

/**
 * Check if registration has exceeded UDP failure threshold.
 *
 * @param {Object} registration - Registration object
 * @returns {boolean} True if threshold exceeded, false otherwise
 */
function isUdpFailureThresholdExceeded(registration) {
    const failureCount = registration.udp_failure_count || 0;
    return failureCount >= UDP_FAILURE_THRESHOLD;
}

/**
 * Monitor all registrations for heartbeat timeout.
 *
 * @returns {Array} Array of registrations that have timed out
 */
function monitorHeartbeatTimeout() {
    const registrations = getAllRegistrations();
    const timedOut = [];

    for (const registration of registrations) {
        if (isHeartbeatTimeout(registration) || isUdpFailureThresholdExceeded(registration)) {
            if (!registration.is_offline) {
                // Mark as offline (first time detection)
                markRegistrationOffline(registration.mesh_id);
                console.warn(`[DISCONNECTION] Root node marked offline: mesh_id=${registration.mesh_id}, root_ip=${registration.root_ip}, ` +
                    `last_heartbeat=${registration.last_heartbeat ? new Date(registration.last_heartbeat).toISOString() : 'never'}, ` +
                    `last_state_update=${registration.last_state_update ? new Date(registration.last_state_update).toISOString() : 'never'}, ` +
                    `udp_failures=${registration.udp_failure_count || 0}`);
                timedOut.push(registration);
            }
        }
    }

    return timedOut;
}

/*******************************************************
 *                Stale Registration Cleanup
 *******************************************************/

/**
 * Clean up stale registrations (exceeded timeout).
 *
 * @param {boolean} forceCleanup - If true, remove registrations even if recently marked offline
 * @returns {Array} Array of cleaned up registration mesh IDs
 */
function cleanupStaleRegistrations(forceCleanup = false) {
    const registrations = getAllRegistrations();
    const cleanedUp = [];

    for (const registration of registrations) {
        // Clean up if offline for more than 2x timeout (6 minutes by default)
        const cleanupTimeout = HEARTBEAT_TIMEOUT_MS * 2;
        const isOffline = registration.is_offline || false;
        const lastActivity = registration.last_heartbeat || registration.last_state_update;

        if (isOffline && lastActivity) {
            const timeSinceActivity = Date.now() - lastActivity;
            if (timeSinceActivity > cleanupTimeout || forceCleanup) {
                // Remove stale registration
                const removed = removeRegistration(registration.mesh_id);
                if (removed) {
                    console.log(`[CLEANUP] Removed stale registration: mesh_id=${registration.mesh_id}, root_ip=${registration.root_ip}, ` +
                        `offline_for=${Math.floor(timeSinceActivity / 1000)}s`);
                    cleanedUp.push(registration.mesh_id);
                }
            }
        } else if (!lastActivity && (Date.now() - registration.registered_at) > cleanupTimeout) {
            // Registration with no activity for cleanup timeout - remove it
            const removed = removeRegistration(registration.mesh_id);
            if (removed) {
                console.log(`[CLEANUP] Removed registration with no activity: mesh_id=${registration.mesh_id}, root_ip=${registration.root_ip}`);
                cleanedUp.push(registration.mesh_id);
            }
        }
    }

    return cleanedUp;
}

/*******************************************************
 *                Periodic Monitoring
 *******************************************************/

let monitoringInterval = null;

/**
 * Start periodic monitoring of heartbeat timeouts and cleanup.
 */
function startMonitoring() {
    if (monitoringInterval !== null) {
        return; // Already started
    }

    monitoringInterval = setInterval(() => {
        // Monitor heartbeat timeouts
        monitorHeartbeatTimeout();

        // Clean up stale registrations
        cleanupStaleRegistrations();
    }, CLEANUP_CHECK_INTERVAL_MS);

    console.log(`[DISCONNECTION] Started monitoring: heartbeat_timeout=${HEARTBEAT_TIMEOUT_MS}ms, ` +
        `udp_failure_threshold=${UDP_FAILURE_THRESHOLD}, cleanup_interval=${CLEANUP_CHECK_INTERVAL_MS}ms`);
}

/**
 * Stop periodic monitoring.
 */
function stopMonitoring() {
    if (monitoringInterval !== null) {
        clearInterval(monitoringInterval);
        monitoringInterval = null;
        console.log('[DISCONNECTION] Stopped monitoring');
    }
}

/*******************************************************
 *                Exports
 *******************************************************/

module.exports = {
    isHeartbeatTimeout,
    isUdpFailureThresholdExceeded,
    monitorHeartbeatTimeout,
    cleanupStaleRegistrations,
    startMonitoring,
    stopMonitoring,
    HEARTBEAT_TIMEOUT_MS,
    UDP_FAILURE_THRESHOLD,
    CLEANUP_CHECK_INTERVAL_MS
};
