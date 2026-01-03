/* mDNS Service Advertisement Module
 *
 * This module provides mDNS/Bonjour service advertisement for the external web server.
 * It allows ESP32 root nodes to discover the server via zero-configuration networking.
 * mDNS is completely optional - the server works without it (UDP broadcast fallback).
 *
 * Copyright (c) 2025 the_louie
 */

const bonjour = require('bonjour')();

/*******************************************************
 *                Service State
 *******************************************************/

/**
 * Currently registered service (null if not registered)
 */
let currentService = null;

/*******************************************************
 *                Service Registration
 *******************************************************/

/**
 * Register mDNS service.
 *
 * @param {number} port - HTTP port number
 * @param {string} serviceName - Service name (e.g., "Lyktparad Web Server")
 * @param {Object} metadata - Service metadata (version, protocol, udp_port)
 * @returns {Object|null} Service object or null on failure
 */
function registerService(port, serviceName, metadata) {
    // Unregister existing service if any
    if (currentService) {
        try {
            currentService.stop();
        } catch (err) {
            console.warn('mDNS: Error stopping existing service:', err.message);
        }
        currentService = null;
    }

    try {
        // Build TXT record from metadata
        const txt = {};
        if (metadata.version) {
            txt.version = String(metadata.version);
        }
        if (metadata.protocol) {
            txt.protocol = String(metadata.protocol);
        }
        if (metadata.udp_port) {
            txt.udp_port = String(metadata.udp_port);
        }

        // Publish service with type _lyktparad-web._tcp
        // Note: bonjour package expects type without underscore prefix and protocol suffix
        // It will automatically add _ prefix and ._tcp suffix
        currentService = bonjour.publish({
            name: serviceName,
            type: 'lyktparad-web',
            port: port,
            txt: txt
        });

        console.log(`mDNS: Service registered: ${serviceName} on port ${port} (type: _lyktparad-web._tcp)`);
        return currentService;
    } catch (err) {
        console.warn('mDNS: Failed to register service:', err.message);
        currentService = null;
        return null;
    }
}

/**
 * Get currently registered service.
 *
 * @returns {Object|null} Service object or null if not registered
 */
function getService() {
    return currentService;
}

/**
 * Unregister mDNS service.
 *
 * @param {Object} service - Service object to unregister (optional, uses current if not provided)
 * @returns {boolean} True if unregistered successfully, false otherwise
 */
function unregisterService(service) {
    const serviceToStop = service || currentService;
    if (!serviceToStop) {
        return false;
    }

    try {
        serviceToStop.stop();
        if (serviceToStop === currentService) {
            currentService = null;
        }
        console.log('mDNS: Service unregistered');
        return true;
    } catch (err) {
        console.warn('mDNS: Error unregistering service:', err.message);
        return false;
    }
}

/*******************************************************
 *                Module Exports
 *******************************************************/

module.exports = {
    registerService,
    getService,
    unregisterService
};
