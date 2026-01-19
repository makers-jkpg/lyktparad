/* Proxy Route Handler
 *
 * This module implements the HTTP proxy routes that translate HTTP requests
 * from the web UI to UDP commands for the ESP32 root node.
 *
 * Copyright (c) 2025 the_louie
 */

const { httpToUdpCommand } = require('../lib/http-udp-translator');
const { udpToHttpResponse } = require('../lib/udp-http-translator');
const { sendUdpCommandAndWait } = require('../lib/udp-client');
const { getFirstRegisteredRootNode, incrementUdpFailureCount, markRegistrationOffline, updateLastHeartbeat } = require('../lib/registration');

/*******************************************************
 *                Root Node Lookup
 *******************************************************/

/**
 * Get registered root node.
 *
 * @returns {Object|null} Root node info { root_ip, udp_port, registration } or null
 */
function getRegisteredRootNode() {
    const registration = getFirstRegisteredRootNode();
    if (!registration) {
        return null;
    }

    // Check if root node is offline
    if (registration.is_offline) {
        return null; // Treat offline nodes as unavailable
    }

    return {
        root_ip: registration.root_ip,
        udp_port: registration.udp_port,
        registration: registration
    };
}

/*******************************************************
 *                Proxy Processing
 *******************************************************/

/**
 * Process proxy request: HTTP → UDP → HTTP.
 *
 * @param {Object} req - Express request object
 * @param {Object} res - Express response object
 * @returns {Promise<void>}
 */
async function processProxyRequest(req, res) {
    let rootNode = null; // Declare outside try block for access in catch block
    try {
        // Check if root node is registered
        rootNode = getRegisteredRootNode();
        if (!rootNode) {
            const registration = getFirstRegisteredRootNode();
            if (registration && registration.is_offline) {
                // Root node is registered but offline
                res.status(503).json({
                    error: 'Root node unavailable',
                    message: 'The root node is currently offline or unreachable.',
                    code: 503,
                    suggestion: `You can access the root node directly via its IP address: http://${registration.root_ip}`,
                    last_seen: registration.last_heartbeat || registration.last_state_update || null
                });
            } else {
                // No root node registered
                res.status(404).json({
                    error: 'No root node registered',
                    message: 'External web server has no registered root node. Please access the root node directly via its IP address.',
                    code: 404
                });
            }
            return;
        }

        // Convert HTTP request to UDP command
        const udpCommand = httpToUdpCommand(req);
        if (!udpCommand) {
            res.status(404).json({
                error: 'Endpoint not found',
                message: `No UDP command mapping for ${req.method} ${req.path}`
            });
            return;
        }

        // Send UDP command and wait for response
        // Note: ESP32 listens for API commands on well-known port 8082
        // (rootNode.udp_port is the server's UDP port, not the root's port)
        const ESP32_API_PORT = 8082;
        const timeout = 8000; // 8 second timeout
        const udpResponse = await sendUdpCommandAndWait(
            rootNode.root_ip,
            ESP32_API_PORT,
            udpCommand.packet,
            udpCommand.sequenceNumber,
            timeout
        );

        // Successful UDP communication - reset failure count and mark as online
        if (rootNode.registration) {
            rootNode.registration.udp_failure_count = 0;
            rootNode.registration.is_offline = false;
            // Update last activity timestamp (treat successful API call as activity)
            updateLastHeartbeat(rootNode.registration.mesh_id);
        }

        // Convert UDP response to HTTP response
        const httpResponse = udpToHttpResponse(udpResponse);

        // Send HTTP response (plain text for /api/sequence/pointer, JSON for others)
        res.status(httpResponse.status);
        if (httpResponse.text !== undefined) {
            res.setHeader('Content-Type', 'text/plain');
            res.send(httpResponse.text);
        } else if (httpResponse.json !== undefined) {
            res.json(httpResponse.json);
        } else {
            // Fallback: empty response (should not happen)
            res.end();
        }

    } catch (error) {
        // Track UDP communication failures
        if (rootNode && rootNode.registration) {
            incrementUdpFailureCount(rootNode.registration.mesh_id);

            // Check if failure threshold exceeded
            const { isUdpFailureThresholdExceeded } = require('../lib/disconnection-detection');
            if (isUdpFailureThresholdExceeded(rootNode.registration)) {
                markRegistrationOffline(rootNode.registration.mesh_id);
                console.warn(`[PROXY] Root node marked offline due to UDP failures: mesh_id=${rootNode.registration.mesh_id}, failures=${rootNode.registration.udp_failure_count}`);
            }
        }

        // Handle errors
        if (error.message.includes('timeout')) {
            res.status(503).json({
                error: 'Root node unavailable',
                message: 'Root node did not respond within timeout period. The node may be offline or unreachable.',
                code: 503,
                suggestion: rootNode ? `You can access the root node directly via its IP address: http://${rootNode.root_ip}` : 'Please check if the root node is online.'
            });
        } else if (error.message.includes('ENETUNREACH') || error.message.includes('EHOSTUNREACH')) {
            res.status(503).json({
                error: 'Root node unavailable',
                message: 'Cannot reach root node. The node may be offline or on a different network.',
                code: 503,
                suggestion: rootNode ? `You can access the root node directly via its IP address: http://${rootNode.root_ip}` : 'Please check if the root node is online.'
            });
        } else if (error.message.includes('exceeds MTU') || error.message.includes('Payload too large') || error.message.includes('Bundle size') || error.message.includes('too large')) {
            res.status(413).json({
                error: 'Payload too large',
                message: 'Request payload exceeds maximum size. Please reduce the data size and try again.',
                code: 413
            });
        } else if (error.message.includes('Invalid plugin name') || error.message.includes('Plugin name')) {
            res.status(400).json({
                error: 'Bad Request',
                message: error.message || 'Invalid plugin name format.',
                code: 400
            });
        } else {
            console.error('Proxy request error:', error);
            res.status(500).json({
                error: 'Internal server error',
                message: error.message,
                code: 500
            });
        }
    }
}

/*******************************************************
 *                Retry Logic for Critical Commands
 *******************************************************/

/**
 * Check if command is critical (needs retry).
 *
 * @param {string} method - HTTP method
 * @param {string} path - HTTP path
 * @returns {boolean} True if critical command
 */
function isCriticalCommand(method, path) {
    // POST operations are critical (write operations)
    if (method === 'POST') {
        return true;
    }
    // GET operations are not critical (idempotent)
    return false;
}

/**
 * Process proxy request with retry for critical commands.
 *
 * @param {Object} req - Express request object
 * @param {Object} res - Express response object
 * @returns {Promise<void>}
 */
async function processProxyRequestWithRetry(req, res) {
    const isCritical = isCriticalCommand(req.method, req.path);
    const maxRetries = isCritical ? 2 : 0; // Retry critical commands up to 2 times

    for (let attempt = 0; attempt <= maxRetries; attempt++) {
        try {
            await processProxyRequest(req, res);
            // If we get here, response was sent successfully
            return;
        } catch (error) {
            // Check if response was already sent
            if (res.headersSent) {
                return; // Response already sent, don't retry
            }

            if (attempt === maxRetries) {
                // Last attempt failed, send error response
                // Get root node info for suggestion if available
                const registration = getFirstRegisteredRootNode();
                if (error.message && error.message.includes('timeout')) {
                    res.status(503).json({
                        error: 'Root node unavailable',
                        message: 'Root node did not respond after retries. The node may be offline.',
                        code: 503,
                        suggestion: registration ? `You can access the root node directly via its IP address: http://${registration.root_ip}` : 'Please check if the root node is online.'
                    });
                } else {
                    res.status(500).json({
                        error: 'Internal server error',
                        message: error.message || 'Unknown error',
                        code: 500
                    });
                }
                return;
            }

            // Wait before retry (exponential backoff)
            const delay = Math.min(1000 * Math.pow(2, attempt), 5000);
            await new Promise(resolve => setTimeout(resolve, delay));
        }
    }
}

/*******************************************************
 *                Route Handler
 *******************************************************/

/**
 * Generic proxy route handler.
 *
 * @param {Object} req - Express request object
 * @param {Object} res - Express response object
 */
async function proxyHandler(req, res) {
    // Set CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    // Handle OPTIONS request
    if (req.method === 'OPTIONS') {
        res.status(200).end();
        return;
    }

    // Process proxy request
    await processProxyRequestWithRetry(req, res);
}

module.exports = {
    proxyHandler,
    processProxyRequest,
    getRegisteredRootNode
};
