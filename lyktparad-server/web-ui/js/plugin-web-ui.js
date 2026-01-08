/**
 * Plugin Web UI Module
 *
 * Provides JavaScript utilities for plugin web UI integration, enabling
 * dynamic loading of plugin HTML/CSS/JS bundles and communication with
 * plugins via binary data.
 *
 * Architecture: Zero CPU processing on ESP32 - all encoding/decoding
 * handled in JavaScript. ESP32 acts as transparent proxy.
 *
 * Copyright (c) 2025 the_louie
 */

(function() {
    'use strict';

    /**
     * Plugin UI container management
     * Tracks loaded plugins for cleanup
     */
    const pluginState = {
        currentPlugin: null,
        styleTagId: null,
        containerId: 'plugin-ui-container'
    };

    /**
     * Error types for better error handling
     */
    const PluginErrorType = {
        NETWORK: 'network',
        HTTP: 'http',
        PARSE: 'parse',
        INJECTION: 'injection',
        VALIDATION: 'validation'
    };

    /**
     * Create user-friendly error message
     * @param {string} type - Error type
     * @param {string} message - Error message
     * @param {number} [status] - HTTP status code (if applicable)
     * @returns {string} User-friendly error message
     */
    function createErrorMessage(type, message, status) {
        switch (type) {
            case PluginErrorType.NETWORK:
                return 'Network error: Unable to connect to server. Please check your connection.';
            case PluginErrorType.HTTP:
                if (status === 404) {
                    return 'Plugin not found or has no web UI.';
                } else if (status === 413) {
                    return 'Data payload too large. Please reduce the data size.';
                } else if (status >= 500) {
                    return 'Server error: Please try again later.';
                }
                return `HTTP error (${status}): ${message}`;
            case PluginErrorType.PARSE:
                return 'Failed to parse response. The plugin bundle may be malformed.';
            case PluginErrorType.INJECTION:
                return `Failed to inject plugin UI: ${message}`;
            case PluginErrorType.VALIDATION:
                return `Validation error: ${message}`;
            default:
                return message || 'Unknown error occurred.';
        }
    }

    /**
     * Log error with context
     * @param {string} message - Error message
     * @param {Error} [error] - Error object
     */
    function logError(message, error) {
        console.error('[Plugin Web UI]', message, error || '');
    }

    /**
     * Validate plugin name
     * Plugin names must match regex: ^[a-zA-Z0-9_-]+$
     * @param {string} pluginName - Plugin name to validate
     * @returns {boolean} True if valid
     */
    function isValidPluginName(pluginName) {
        if (!pluginName || typeof pluginName !== 'string') {
            return false;
        }
        return /^[a-zA-Z0-9_-]+$/.test(pluginName);
    }

    /**
     * Get or create plugin UI container
     * @returns {HTMLElement} Container element
     */
    function getPluginContainer() {
        let container = document.getElementById(pluginState.containerId);
        if (!container) {
            container = document.createElement('div');
            container.id = pluginState.containerId;
            container.className = 'plugin-ui-container';
            // Insert before closing main tag or at end of plugins tab content
            const pluginsTab = document.querySelector('.tab-content[data-tab="plugins"] main');
            if (pluginsTab) {
                pluginsTab.appendChild(container);
            } else {
                // Fallback: append to body
                document.body.appendChild(container);
            }
        }
        return container;
    }

    /**
     * Clear previous plugin UI
     * Removes HTML content and style tag from previous plugin
     */
    function clearPreviousPluginUI() {
        // Clear HTML container
        const container = getPluginContainer();
        container.innerHTML = '';

        // Remove previous style tag
        if (pluginState.styleTagId) {
            const oldStyleTag = document.getElementById(pluginState.styleTagId);
            if (oldStyleTag) {
                oldStyleTag.remove();
            }
            pluginState.styleTagId = null;
        }
    }

    /**
     * Inject CSS into <style> tag with style isolation
     * Creates a new <style> tag for the current plugin
     * @param {string} css - CSS content to inject
     * @param {string} pluginName - Plugin name for isolation
     */
    function injectCSS(css, pluginName) {
        if (!css || typeof css !== 'string') {
            return; // No CSS to inject
        }

        try {
            // Remove previous style tag if exists
            if (pluginState.styleTagId) {
                const oldStyleTag = document.getElementById(pluginState.styleTagId);
                if (oldStyleTag) {
                    oldStyleTag.remove();
                }
            }

            // Create new style tag
            const styleTag = document.createElement('style');
            styleTag.id = `plugin-style-${pluginName}`;
            styleTag.type = 'text/css';
            styleTag.textContent = css;

            // Insert into head
            document.head.appendChild(styleTag);
            pluginState.styleTagId = styleTag.id;

            // Note: CSS class prefixes are required for namespace scoping
            // Plugins must prefix all CSS classes with plugin name (e.g., .plugin-rgb-slider)
        } catch (error) {
            logError('Failed to inject CSS', error);
            throw new Error(createErrorMessage(PluginErrorType.INJECTION, 'CSS injection failed: ' + error.message));
        }
    }

    /**
     * Inject HTML into container with cleanup
     * @param {string} html - HTML content to inject
     * @param {string} pluginName - Plugin name
     */
    function injectHTML(html, pluginName) {
        if (!html || typeof html !== 'string') {
            return; // No HTML to inject
        }

        try {
            const container = getPluginContainer();
            container.innerHTML = html;

            // Note: This uses a trusted code model - plugins are compiled into firmware
            // HTML is injected directly into DOM without sanitization
        } catch (error) {
            logError('Failed to inject HTML', error);
            throw new Error(createErrorMessage(PluginErrorType.INJECTION, 'HTML injection failed: ' + error.message));
        }
    }

    /**
     * Execute JavaScript using new Function() (preferred over eval)
     * @param {string} js - JavaScript code to execute
     * @param {string} pluginName - Plugin name for context
     */
    function executeJavaScript(js, pluginName) {
        if (!js || typeof js !== 'string') {
            return; // No JavaScript to execute
        }

        try {
            // Use new Function() instead of eval() for better security
            // This creates a function in global scope, which is acceptable for trusted code
            const pluginFunction = new Function(js);
            pluginFunction();

            // Note: JavaScript must use namespace prefixes (e.g., plugin_rgb_init())
            // to avoid function name conflicts
        } catch (error) {
            logError('Failed to execute JavaScript', error);
            throw new Error(createErrorMessage(PluginErrorType.INJECTION, 'JavaScript execution failed: ' + error.message));
        }
    }

    /**
     * Load plugin bundle from server
     * Fetches HTML/CSS/JS bundle and injects into DOM
     * @param {string} pluginName - Plugin name
     * @returns {Promise<Object>} Promise resolving to bundle object
     * @throws {Error} If loading fails
     * @example
     * // Load a plugin bundle
     * try {
     *   const bundle = await window.PluginWebUI.loadPluginBundle('rgb_effect');
     *   console.log('Bundle loaded:', bundle);
     * } catch (error) {
     *   console.error('Failed to load bundle:', error.message);
     * }
     */
    async function loadPluginBundle(pluginName) {
        // Validate plugin name
        if (!isValidPluginName(pluginName)) {
            const error = new Error(createErrorMessage(PluginErrorType.VALIDATION, 'Invalid plugin name format'));
            logError('Invalid plugin name', error);
            throw error;
        }

        try {
            // Clear previous plugin UI
            clearPreviousPluginUI();

            // Fetch bundle from endpoint: GET /api/plugin/<plugin-name>/bundle
            const response = await fetch(`/api/plugin/${pluginName}/bundle`, {
                method: 'GET',
                headers: {
                    'Accept': 'application/json'
                }
            });

            // Handle HTTP errors
            if (!response.ok) {
                const errorMessage = createErrorMessage(PluginErrorType.HTTP, 'Bundle request failed', response.status);
                logError(`Bundle fetch failed: HTTP ${response.status}`);
                throw new Error(errorMessage);
            }

            // Parse JSON response
            let bundle;
            try {
                bundle = await response.json();
            } catch (parseError) {
                logError('Failed to parse bundle JSON', parseError);
                throw new Error(createErrorMessage(PluginErrorType.PARSE, 'Invalid JSON response'));
            }

            // Validate bundle structure (should have html, js, css fields, but all are optional)
            if (typeof bundle !== 'object' || bundle === null) {
                throw new Error(createErrorMessage(PluginErrorType.PARSE, 'Bundle is not an object'));
            }

            // Inject CSS first (so styles are available for HTML)
            if (bundle.css) {
                injectCSS(bundle.css, pluginName);
            }

            // Inject HTML
            if (bundle.html) {
                injectHTML(bundle.html, pluginName);
            }

            // Execute JavaScript last (so DOM elements are available)
            if (bundle.js) {
                executeJavaScript(bundle.js, pluginName);
            }

            // Update state
            pluginState.currentPlugin = pluginName;

            return bundle;
        } catch (error) {
            // Re-throw if it's already our error type
            if (error.message && (
                error.message.includes('Network error') ||
                error.message.includes('HTTP error') ||
                error.message.includes('Failed to parse') ||
                error.message.includes('Failed to inject') ||
                error.message.includes('Validation error')
            )) {
                throw error;
            }

            // Handle network errors
            if (error.name === 'TypeError' && error.message.includes('fetch')) {
                const networkError = new Error(createErrorMessage(PluginErrorType.NETWORK));
                logError('Network error during bundle fetch', error);
                throw networkError;
            }

            // Generic error
            logError('Unexpected error loading bundle', error);
            throw new Error(createErrorMessage(PluginErrorType.NETWORK, error.message));
        }
    }

    /**
     * Convert data to Uint8Array
     * Accepts ArrayBuffer, Uint8Array, or number array
     * @param {ArrayBuffer|Uint8Array|number[]} data - Data to convert
     * @returns {Uint8Array} Converted data
     */
    function convertToUint8Array(data) {
        if (data instanceof Uint8Array) {
            return data;
        } else if (data instanceof ArrayBuffer) {
            return new Uint8Array(data);
        } else if (Array.isArray(data)) {
            // Validate all elements are numbers
            if (!data.every(val => typeof val === 'number' && val >= 0 && val <= 255 && Number.isInteger(val))) {
                throw new Error(createErrorMessage(PluginErrorType.VALIDATION, 'Array must contain only integers 0-255'));
            }
            return new Uint8Array(data);
        } else {
            throw new Error(createErrorMessage(PluginErrorType.VALIDATION, 'Data must be ArrayBuffer, Uint8Array, or number array'));
        }
    }

    /**
     * Send plugin data to server
     * POSTs raw bytes to plugin endpoint (zero CPU on ESP32)
     * @param {string} pluginName - Plugin name
     * @param {ArrayBuffer|Uint8Array|number[]} data - Raw bytes to send
     * @returns {Promise<Object>} Promise resolving to success response
     * @throws {Error} If sending fails
     * @example
     * // Send RGB color data
     * const rgbData = window.PluginWebUI.encodeRGB(255, 0, 128);
     * try {
     *   const response = await window.PluginWebUI.sendPluginData('rgb_effect', rgbData);
     *   console.log('Data sent:', response);
     * } catch (error) {
     *   console.error('Failed to send data:', error.message);
     * }
     */
    async function sendPluginData(pluginName, data) {
        // Validate plugin name
        if (!isValidPluginName(pluginName)) {
            const error = new Error(createErrorMessage(PluginErrorType.VALIDATION, 'Invalid plugin name format'));
            logError('Invalid plugin name', error);
            throw error;
        }

        // Convert data to Uint8Array
        let uint8Data;
        try {
            uint8Data = convertToUint8Array(data);
        } catch (error) {
            logError('Data conversion failed', error);
            throw error;
        }

        // Validate payload size (512 bytes recommended max)
        if (uint8Data.length > 512) {
            const error = new Error(createErrorMessage(PluginErrorType.VALIDATION, `Payload size ${uint8Data.length} exceeds recommended limit of 512 bytes`));
            logError('Payload too large', error);
            throw error;
        }

        try {
            // POST to endpoint: POST /api/plugin/<plugin-name>/data
            // Content-Type: application/octet-stream (raw bytes)
            const response = await fetch(`/api/plugin/${pluginName}/data`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/octet-stream'
                },
                body: uint8Data
            });

            // Handle HTTP errors
            if (!response.ok) {
                let errorMessage;
                try {
                    const errorData = await response.json();
                    errorMessage = errorData.error || errorData.message || 'Data send failed';
                } catch {
                    errorMessage = createErrorMessage(PluginErrorType.HTTP, 'Data send failed', response.status);
                }
                logError(`Data send failed: HTTP ${response.status}`, errorMessage);
                throw new Error(errorMessage);
            }

            // Parse response (may be empty or JSON)
            try {
                const responseText = await response.text();
                if (responseText) {
                    return JSON.parse(responseText);
                }
                return { success: true };
            } catch (parseError) {
                // Empty response is OK
                return { success: true };
            }
        } catch (error) {
            // Re-throw if it's already our error type
            if (error.message && (
                error.message.includes('Invalid plugin name') ||
                error.message.includes('Payload size') ||
                error.message.includes('Data must be') ||
                error.message.includes('HTTP error') ||
                error.message.includes('Data send failed')
            )) {
                throw error;
            }

            // Handle network errors
            if (error.name === 'TypeError' && error.message.includes('fetch')) {
                const networkError = new Error(createErrorMessage(PluginErrorType.NETWORK));
                logError('Network error during data send', error);
                throw networkError;
            }

            // Generic error
            logError('Unexpected error sending data', error);
            throw new Error(createErrorMessage(PluginErrorType.NETWORK, error.message));
        }
    }

    /**
     * Encode RGB values to Uint8Array[3]
     * @param {number} r - Red value (0-255)
     * @param {number} g - Green value (0-255)
     * @param {number} b - Blue value (0-255)
     * @returns {Uint8Array} Uint8Array[3] with R, G, B values
     * @example
     * // Encode RGB color (red)
     * const redColor = window.PluginWebUI.encodeRGB(255, 0, 0);
     * // redColor is Uint8Array[3] with values [255, 0, 0]
     */
    function encodeRGB(r, g, b) {
        // Validate values
        if (typeof r !== 'number' || !Number.isInteger(r) || r < 0 || r > 255) {
            throw new Error(createErrorMessage(PluginErrorType.VALIDATION, 'R value must be integer 0-255'));
        }
        if (typeof g !== 'number' || !Number.isInteger(g) || g < 0 || g > 255) {
            throw new Error(createErrorMessage(PluginErrorType.VALIDATION, 'G value must be integer 0-255'));
        }
        if (typeof b !== 'number' || !Number.isInteger(b) || b < 0 || b > 255) {
            throw new Error(createErrorMessage(PluginErrorType.VALIDATION, 'B value must be integer 0-255'));
        }

        // Return Uint8Array[3] with R, G, B values
        return new Uint8Array([r, g, b]);
    }

    /**
     * Encode Uint8 value to Uint8Array[1]
     * @param {number} value - Value (0-255)
     * @returns {Uint8Array} Uint8Array[1] with value
     * @example
     * // Encode single byte value
     * const byteValue = window.PluginWebUI.encodeUint8(128);
     * // byteValue is Uint8Array[1] with value [128]
     */
    function encodeUint8(value) {
        // Validate value
        if (typeof value !== 'number' || !Number.isInteger(value) || value < 0 || value > 255) {
            throw new Error(createErrorMessage(PluginErrorType.VALIDATION, 'Value must be integer 0-255'));
        }

        return new Uint8Array([value]);
    }

    /**
     * Encode Uint16 value to Uint8Array[2] (little-endian)
     * @param {number} value - Value (0-65535)
     * @returns {Uint8Array} Uint8Array[2] with value in little-endian format
     * @example
     * // Encode 16-bit value (little-endian)
     * const uint16Value = window.PluginWebUI.encodeUint16(0x1234);
     * // uint16Value is Uint8Array[2] with values [0x34, 0x12] (LSB first)
     */
    function encodeUint16(value) {
        // Validate value
        if (typeof value !== 'number' || !Number.isInteger(value) || value < 0 || value > 65535) {
            throw new Error(createErrorMessage(PluginErrorType.VALIDATION, 'Value must be integer 0-65535'));
        }

        // Encode as little-endian (LSB first)
        const result = new Uint8Array(2);
        result[0] = value & 0xFF;        // Low byte
        result[1] = (value >> 8) & 0xFF; // High byte
        return result;
    }

    // Export public API
    window.PluginWebUI = {
        loadPluginBundle: loadPluginBundle,
        sendPluginData: sendPluginData,
        encodeRGB: encodeRGB,
        encodeUint8: encodeUint8,
        encodeUint16: encodeUint16
    };

})();