/* mDNS Service Advertisement Module
 *
 * This module provides mDNS/Bonjour service advertisement for the external web server.
 * The service is advertised as _lyktparad-web._tcp to enable zero-configuration
 * discovery by ESP32 root nodes. mDNS is completely optional - the server works
 * perfectly without it (fallback to manual configuration).
 *
 * Copyright (c) 2025 the_louie
 */

let bonjour = null;
let bonjourInstance = null;
let service = null;

/**
 * Initialize mDNS module by loading the bonjour library.
 * This function handles the case where the library is not available.
 *
 * @returns {boolean} True if bonjour library is available, false otherwise
 */
function init() {
    if (bonjour !== null) {
        return bonjour !== false; // Already initialized
    }

    try {
        bonjour = require('bonjour');
        return true;
    } catch (err) {
        console.warn('mDNS: bonjour library not available, mDNS will be disabled');
        console.warn('mDNS: Install with: npm install bonjour');
        bonjour = false;
        return false;
    }
}

/**
 * Register mDNS service advertisement.
 *
 * @param {number} port - HTTP server port number
 * @param {string} serviceName - Service name (e.g., "Lyktparad Web Server")
 * @param {Object} metadata - Service metadata for TXT records
 * @param {string} metadata.version - Server version
 * @param {string} metadata.protocol - Protocol type (e.g., "udp")
 * @param {number} [metadata.udp_port] - UDP port number (if different from HTTP port)
 * @returns {Object|null} Service advertisement object, or null if registration failed
 */
function registerService(port, serviceName, metadata) {
    // Initialize bonjour library
    if (!init()) {
        return null;
    }

    try {
        // Stop previous service if it exists (handle re-registration)
        if (service) {
            try {
                service.stop();
            } catch (err) {
                // Ignore errors when stopping previous service
            }
            service = null;
        }

        // Create bonjour instance (reuse if already created)
        if (!bonjourInstance) {
            bonjourInstance = bonjour();
        }

        // Build TXT record from metadata
        const txtRecord = {};
        if (metadata.version) {
            txtRecord.version = metadata.version;
        }
        if (metadata.protocol) {
            txtRecord.protocol = metadata.protocol;
        }
        if (metadata.udp_port !== undefined) {
            txtRecord.udp_port = String(metadata.udp_port);
        }

        // Verify TXT record size (mDNS limit is 255 bytes per record)
        // Each TXT record entry is encoded as: [length byte][key=value string]
        // Calculate size: sum of (1 + key.length + 1 + value.length) for each entry
        let txtSize = 0;
        for (const key in txtRecord) {
            // Each entry: 1 byte length + key + '=' + value
            txtSize += 1 + key.length + 1 + txtRecord[key].length;
        }
        if (txtSize > 255) {
            console.warn(`mDNS: TXT record size (${txtSize} bytes) exceeds mDNS limit (255 bytes), truncating`);
            // Truncate version if needed (most likely to be large)
            if (txtRecord.version && txtSize > 255) {
                const versionEntrySize = 1 + 'version'.length + 1 + txtRecord.version.length;
                const otherSize = txtSize - versionEntrySize;
                const maxVersionLen = Math.max(0, 255 - otherSize - 1 - 'version'.length - 1);
                txtRecord.version = txtRecord.version.substring(0, maxVersionLen);
            }
        }

        // Publish service
        service = bonjourInstance.publish({
            name: serviceName,
            type: '_lyktparad-web._tcp',
            port: port,
            txt: txtRecord
        });

        console.log(`mDNS: Service registered: ${serviceName} (_lyktparad-web._tcp) on port ${port}`);
        if (Object.keys(txtRecord).length > 0) {
            console.log(`mDNS: TXT records:`, txtRecord);
        }

        return service;
    } catch (err) {
        console.warn('mDNS: Failed to register service:', err.message);
        console.warn('mDNS: Server will continue without mDNS');
        return null;
    }
}

/**
 * Unregister mDNS service advertisement.
 *
 * @param {Object} serviceAdvertisement - Service advertisement object from registerService()
 * @returns {boolean} True if unregistration succeeded, false otherwise
 */
function unregisterService(serviceAdvertisement) {
    if (!serviceAdvertisement) {
        return false;
    }

    try {
        serviceAdvertisement.stop();
        console.log('mDNS: Service unregistered');

        // Note: We don't destroy bonjourInstance here to allow for potential re-registration
        // The instance will be reused if registerService is called again
        // Only destroy on explicit cleanup (not needed for normal shutdown)

        service = null;
        return true;
    } catch (err) {
        console.warn('mDNS: Failed to unregister service:', err.message);
        return false;
    }
}

/**
 * Get the current service advertisement object.
 *
 * @returns {Object|null} Current service advertisement, or null if not registered
 */
function getService() {
    return service;
}

module.exports = {
    registerService,
    unregisterService,
    getService
};
