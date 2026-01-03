/* OTA API Integration Module
 *
 * This module provides functions for interacting with the OTA API endpoints.
 * All requests are proxied through the external web server to the ESP32 root node.
 *
 * Copyright (c) 2025 the_louie
 */

const API_BASE_URL = '/api';

/**
 * Get current firmware version.
 *
 * @returns {Promise<string>} Firmware version string
 * @throws {Error} If request fails
 */
async function getOtaVersion() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/version`);
        if (!response.ok) {
            if (response.status === 503) {
                const data = await response.json().catch(() => ({}));
                throw new Error(data.message || 'Root node is currently offline or unreachable. Please check the connection status.');
            }
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const data = await response.json();
        return data.version || 'unknown';
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve root node disconnection errors
        }
        throw new Error(`Failed to get firmware version: ${error.message}`);
    }
}

/**
 * Check for updates by validating URL format.
 * Note: This is a client-side validation. Actual version checking
 * would require fetching version info from the firmware URL.
 *
 * @param {string} url - Firmware URL
 * @returns {Promise<Object>} Version info or validation result
 * @throws {Error} If URL is invalid
 */
async function checkForUpdates(url) {
    // Validate URL format
    if (!url || typeof url !== 'string') {
        throw new Error('URL is required');
    }

    try {
        const urlObj = new URL(url);
        if (urlObj.protocol !== 'http:' && urlObj.protocol !== 'https:') {
            throw new Error('URL must use HTTP or HTTPS protocol');
        }
    } catch (error) {
        if (error instanceof TypeError) {
            throw new Error('Invalid URL format');
        }
        throw error;
    }

    // Return validation result
    // In a real implementation, you might fetch version info from the firmware URL
    return {
        valid: true,
        url: url,
        message: 'URL format is valid'
    };
}

/**
 * Start OTA download.
 *
 * @param {string} url - Firmware URL
 * @returns {Promise<Object>} Success response
 * @throws {Error} If download fails
 */
async function startOtaDownload(url) {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/download`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ url: url })
        });

        const data = await response.json();

        if (!response.ok) {
            if (response.status === 409) {
                throw new Error('Downgrade prevented: Firmware version is older than current version');
            }
            if (response.status === 503) {
                throw new Error(data.message || 'Root node is currently offline or unreachable. Please check the connection status.');
            }
            throw new Error(data.error || `HTTP ${response.status}: ${response.statusText}`);
        }

        return data;
    } catch (error) {
        if (error.message.includes('Downgrade') || error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve specific error messages
        }
        throw new Error(`Failed to start download: ${error.message}`);
    }
}

/**
 * Get download status and progress.
 *
 * @returns {Promise<Object>} Status object { downloading: boolean, progress: number }
 * @throws {Error} If request fails
 */
async function getOtaDownloadStatus() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/status`);
        if (!response.ok) {
            if (response.status === 503) {
                const data = await response.json().catch(() => ({}));
                throw new Error(data.message || 'Root node is currently offline or unreachable.');
            }
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const data = await response.json();
        return {
            downloading: data.downloading || false,
            progress: data.progress || 0.0
        };
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve root node disconnection errors
        }
        throw new Error(`Failed to get download status: ${error.message}`);
    }
}

/**
 * Cancel ongoing download.
 *
 * @returns {Promise<Object} Success response
 * @throws {Error} If cancel fails
 */
async function cancelOtaDownload() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/cancel`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });

        const data = await response.json();

        if (!response.ok) {
            if (response.status === 503) {
                throw new Error(data.message || 'Root node is currently offline or unreachable. Please check the connection status.');
            }
            throw new Error(data.error || `HTTP ${response.status}: ${response.statusText}`);
        }

        return data;
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve root node disconnection errors
        }
        throw new Error(`Failed to cancel download: ${error.message}`);
    }
}

/**
 * Start firmware distribution to mesh nodes.
 *
 * @returns {Promise<Object>} Success response
 * @throws {Error} If distribution fails
 */
async function startOtaDistribution() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/distribute`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });

        const data = await response.json();

        if (!response.ok) {
            if (response.status === 409) {
                throw new Error('Downgrade prevented: Firmware version is older than current version');
            }
            if (response.status === 503) {
                throw new Error(data.message || 'Root node is currently offline or unreachable. Please check the connection status.');
            }
            throw new Error(data.error || `HTTP ${response.status}: ${response.statusText}`);
        }

        return data;
    } catch (error) {
        if (error.message.includes('Downgrade') || error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve specific error messages
        }
        throw new Error(`Failed to start distribution: ${error.message}`);
    }
}

/**
 * Get distribution status.
 *
 * @returns {Promise<Object>} Status object with distribution details
 * @throws {Error} If request fails
 */
async function getOtaDistributionStatus() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/distribution/status`);
        if (!response.ok) {
            if (response.status === 503) {
                const data = await response.json().catch(() => ({}));
                throw new Error(data.message || 'Root node is currently offline or unreachable.');
            }
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const data = await response.json();
        return {
            distributing: data.distributing || false,
            total_blocks: data.total_blocks || 0,
            current_block: data.current_block || 0,
            overall_progress: data.overall_progress || 0.0,
            nodes_total: data.nodes_total || 0,
            nodes_complete: data.nodes_complete || 0,
            nodes_failed: data.nodes_failed || 0
        };
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve root node disconnection errors
        }
        throw new Error(`Failed to get distribution status: ${error.message}`);
    }
}

/**
 * Get distribution progress.
 *
 * @returns {Promise<number>} Progress value (0.0 to 1.0)
 * @throws {Error} If request fails
 */
async function getOtaDistributionProgress() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/distribution/progress`);
        if (!response.ok) {
            if (response.status === 503) {
                const data = await response.json().catch(() => ({}));
                throw new Error(data.message || 'Root node is currently offline or unreachable.');
            }
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        const data = await response.json();
        return data.progress || 0.0;
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve root node disconnection errors
        }
        throw new Error(`Failed to get distribution progress: ${error.message}`);
    }
}

/**
 * Cancel distribution.
 *
 * @returns {Promise<Object>} Success response
 * @throws {Error} If cancel fails
 */
async function cancelOtaDistribution() {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/distribution/cancel`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });

        const data = await response.json();

        if (!response.ok) {
            if (response.status === 503) {
                throw new Error(data.message || 'Root node is currently offline or unreachable. Please check the connection status.');
            }
            throw new Error(data.error || `HTTP ${response.status}: ${response.statusText}`);
        }

        return data;
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve root node disconnection errors
        }
        throw new Error(`Failed to cancel distribution: ${error.message}`);
    }
}

/**
 * Initiate coordinated reboot.
 *
 * @param {Object} options - Optional reboot parameters
 * @param {number} options.timeout - Timeout in seconds (default: 10)
 * @param {number} options.delay - Delay in milliseconds (default: 1000)
 * @returns {Promise<Object>} Success response
 * @throws {Error} If reboot fails
 */
async function rebootOta(options = {}) {
    try {
        const response = await fetch(`${API_BASE_URL}/ota/reboot`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                timeout: options.timeout || 10,
                delay: options.delay || 1000
            })
        });

        const data = await response.json();

        if (!response.ok) {
            if (response.status === 503) {
                throw new Error(data.message || 'Root node is currently offline or unreachable. Please check the connection status.');
            }
            throw new Error(data.error || `HTTP ${response.status}: ${response.statusText}`);
        }

        return data;
    } catch (error) {
        if (error.message.includes('offline') || error.message.includes('unreachable')) {
            throw error; // Preserve root node disconnection errors
        }
        throw new Error(`Failed to initiate reboot: ${error.message}`);
    }
}

// Export functions for use in other modules (Node.js)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        getOtaVersion,
        checkForUpdates,
        startOtaDownload,
        getOtaDownloadStatus,
        cancelOtaDownload,
        startOtaDistribution,
        getOtaDistributionStatus,
        getOtaDistributionProgress,
        cancelOtaDistribution,
        rebootOta
    };
}

// Make functions available globally for browser use
if (typeof window !== 'undefined') {
    window.otaApi = {
        getOtaVersion,
        checkForUpdates,
        startOtaDownload,
        getOtaDownloadStatus,
        cancelOtaDownload,
        startOtaDistribution,
        getOtaDistributionStatus,
        getOtaDistributionProgress,
        cancelOtaDistribution,
        rebootOta
    };
}
