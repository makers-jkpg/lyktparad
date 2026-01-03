/* OTA UI Logic Module
 *
 * This module handles all UI interactions for the OTA firmware update section.
 * It manages URL validation, button handlers, progress tracking, status display,
 * error handling, and real-time updates.
 *
 * Copyright (c) 2025 the_louie
 */

// Import OTA API functions from global window.otaApi
// These are made available by ota-api.js
// Use a function to get API functions to handle async loading
function getOtaApi() {
    if (!window.otaApi) {
        throw new Error('OTA API not loaded. Make sure ota-api.js is loaded before ota-ui.js');
    }
    return window.otaApi;
}

// State management
let updateState = {
    stage: 'idle', // idle, downloading, distributing, rebooting
    currentUrl: null,
    pollingInterval: null,
    stateUpdateInterval: null,
    downloadProgress: 0.0,
    distributionProgress: 0.0,
    pollingFailureCount: 0, // Track consecutive polling failures
    lastPollingError: null // Store last polling error message
};

// DOM element references (will be initialized on page load)
let elements = {};

/**
 * Initialize OTA UI on page load.
 */
function initOtaUI() {
    // Get DOM element references
    elements = {
        versionDisplay: document.getElementById('ota-current-version'),
        stageIndicator: document.getElementById('ota-stage-indicator'),
        stageText: document.getElementById('ota-stage-text'),
        urlInput: document.getElementById('ota-firmware-url'),
        urlValidation: document.getElementById('ota-url-validation'),
        checkButton: document.getElementById('ota-check-button'),
        startButton: document.getElementById('ota-start-button'),
        cancelButton: document.getElementById('ota-cancel-button'),
        downloadSection: document.getElementById('ota-download-section'),
        downloadProgress: document.getElementById('ota-download-progress'),
        downloadPercentage: document.getElementById('ota-download-percentage'),
        downloadStatus: document.getElementById('ota-download-status'),
        distributionSection: document.getElementById('ota-distribution-section'),
        distributionProgress: document.getElementById('ota-distribution-progress'),
        distributionPercentage: document.getElementById('ota-distribution-percentage'),
        distributionStatus: document.getElementById('ota-distribution-status'),
        distributionNodes: document.getElementById('ota-distribution-nodes'),
        statusMessages: document.getElementById('ota-status-messages'),
        nodeList: document.getElementById('ota-node-list'),
        nodeListContent: document.getElementById('ota-node-list-content')
    };

    // Set up event listeners
    setupEventListeners();

    // Load current version on page load
    loadCurrentVersion();

    // Set initial button states
    updateButtonStates('idle');
}

/**
 * Set up event listeners for UI interactions.
 */
function setupEventListeners() {
    // URL input validation
    if (elements.urlInput) {
        elements.urlInput.addEventListener('input', handleUrlInput);
        elements.urlInput.addEventListener('blur', validateUrl);
    }

    // Button handlers
    if (elements.checkButton) {
        elements.checkButton.addEventListener('click', handleCheckForUpdates);
    }
    if (elements.startButton) {
        elements.startButton.addEventListener('click', handleStartUpdate);
    }
    if (elements.cancelButton) {
        elements.cancelButton.addEventListener('click', handleCancelUpdate);
    }
}

/**
 * Validate firmware URL format.
 *
 * @param {string} url - URL to validate
 * @returns {Object} Validation result { valid: boolean, message: string }
 */
function validateFirmwareUrl(url) {
    if (!url || url.trim() === '') {
        return { valid: false, message: 'URL is required' };
    }

    try {
        const urlObj = new URL(url);
        if (urlObj.protocol !== 'http:' && urlObj.protocol !== 'https:') {
            return { valid: false, message: 'URL must use HTTP or HTTPS protocol' };
        }
        return { valid: true, message: 'URL format is valid' };
    } catch (error) {
        return { valid: false, message: 'Invalid URL format' };
    }
}

/**
 * Handle URL input changes.
 */
function handleUrlInput() {
    if (!elements.urlInput) return;

    const url = elements.urlInput.value.trim();
    const validation = validateFirmwareUrl(url);

    // Update input styling
    elements.urlInput.classList.remove('valid', 'invalid');
    if (elements.urlValidation) {
        elements.urlValidation.textContent = '';
        elements.urlValidation.classList.remove('success', 'error');
    }

    if (url === '') {
        // Empty input - no validation message
        updateButtonStates(updateState.stage);
        return;
    }

    if (validation.valid) {
        elements.urlInput.classList.add('valid');
        if (elements.urlValidation) {
            elements.urlValidation.textContent = validation.message;
            elements.urlValidation.classList.add('success');
        }
    } else {
        elements.urlInput.classList.add('invalid');
        if (elements.urlValidation) {
            elements.urlValidation.textContent = validation.message;
            elements.urlValidation.classList.add('error');
        }
    }

    // Update button states based on validation
    updateButtonStates(updateState.stage);
}

/**
 * Validate URL on blur.
 */
function validateUrl() {
    handleUrlInput();
}

/**
 * Handle "Check for Updates" button click.
 */
async function handleCheckForUpdates() {
    const url = elements.urlInput.value.trim();
    const validation = validateFirmwareUrl(url);

    if (!validation.valid) {
        showErrorMessage(validation.message);
        return;
    }

    try {
        elements.checkButton.disabled = true;
        showInfoMessage('Checking URL format...');

        const api = getOtaApi();
        const result = await api.checkForUpdates(url);
        showSuccessMessage(`URL is valid: ${result.url}`);
    } catch (error) {
        showErrorMessage(`Failed to check URL: ${error.message}`, {
            retryCallback: handleCheckForUpdates,
            retryLabel: 'Retry'
        });
    } finally {
        elements.checkButton.disabled = false;
    }
}

/**
 * Handle "Start Update" button click.
 */
async function handleStartUpdate() {
    const url = elements.urlInput.value.trim();
    const validation = validateFirmwareUrl(url);

    if (!validation.valid) {
        showErrorMessage(validation.message);
        return;
    }

    // Get current version for comparison
    const currentVersion = elements.versionDisplay.textContent;
    let versionComparisonText = '';

    // Try to extract version from URL or show comparison if possible
    // Note: We can't reliably get new version from URL without downloading,
    // but we can show current version in confirmation
    if (currentVersion && currentVersion !== 'Loading...' && currentVersion !== 'Error loading version') {
        versionComparisonText = `\nCurrent version: ${currentVersion}`;
    }

    // Show confirmation dialog
    const confirmed = confirm(
        `Start firmware update from:\n${url}${versionComparisonText}\n\n` +
        `This will download and distribute firmware to all mesh nodes. Continue?`
    );

    if (!confirmed) {
        return;
    }

    try {
        updateButtonStates('downloading');
        updateState.stage = 'downloading';
        updateUpdateStage('downloading');
        updateState.currentUrl = url;
        showInfoMessage('Starting firmware download...');

        // Start download
        const api = getOtaApi();
        await api.startOtaDownload(url);
        showSuccessMessage('Download started successfully');

        // Show download progress section
        elements.downloadSection.style.display = 'block';
        updateDownloadProgress({ downloading: true, progress: 0.0 });

        // Start progress polling
        startProgressPolling();
    } catch (error) {
        updateState.stage = 'idle';
        updateButtonStates('idle');
        updateUpdateStage('idle');

        if (error.message.includes('Downgrade')) {
            showWarningMessage(
                'Downgrade prevented: ' + error.message +
                '\n\nIf you want to proceed with the downgrade, you may need to use the root node directly.'
            );
        } else {
            // Show error with retry option
            showErrorMessage(`Failed to start download: ${error.message}`, {
                retryCallback: handleStartUpdate,
                retryLabel: 'Retry Download'
            });
        }
    }
}

/**
 * Handle "Cancel Update" button click.
 */
async function handleCancelUpdate() {
    const confirmed = confirm(
        'Are you sure you want to cancel the current update operation?'
    );

    if (!confirmed) {
        return;
    }

    try {
        const api = getOtaApi();
        if (updateState.stage === 'downloading') {
            await api.cancelOtaDownload();
            showInfoMessage('Download cancelled');
        } else if (updateState.stage === 'distributing') {
            await api.cancelOtaDistribution();
            showInfoMessage('Distribution cancelled');
        }

        // Stop polling and reset state
        stopProgressPolling();
        updateState.stage = 'idle';
        updateState.currentUrl = null;
        updateButtonStates('idle');
        updateUpdateStage('idle');

        // Hide progress sections
        elements.downloadSection.style.display = 'none';
        elements.distributionSection.style.display = 'none';
    } catch (error) {
        // Show error with retry option for cancel operations
        showErrorMessage(`Failed to cancel: ${error.message}`, {
            retryCallback: handleCancelUpdate,
            retryLabel: 'Retry Cancel'
        });
    }
}

/**
 * Update button states based on current update stage.
 *
 * @param {string} stage - Current update stage (idle, downloading, distributing, rebooting)
 */
function updateButtonStates(stage) {
    if (!elements.urlInput || !elements.checkButton || !elements.startButton || !elements.cancelButton) {
        return; // Elements not available
    }

    const url = elements.urlInput.value.trim();
    const validation = validateFirmwareUrl(url);

    // Enable/disable buttons based on stage
    switch (stage) {
        case 'idle':
            elements.checkButton.disabled = !validation.valid || url === '';
            elements.startButton.disabled = !validation.valid || url === '';
            elements.cancelButton.style.display = 'none';
            break;

        case 'downloading':
        case 'distributing':
        case 'rebooting':
            elements.checkButton.disabled = true;
            elements.startButton.disabled = true;
            elements.cancelButton.style.display = 'inline-block';
            break;

        default:
            elements.checkButton.disabled = false;
            elements.startButton.disabled = false;
            elements.cancelButton.style.display = 'none';
    }
}

/**
 * Start progress polling.
 */
function startProgressPolling() {
    // Clear any existing polling
    stopProgressPolling();

    // Reset failure counter
    updateState.pollingFailureCount = 0;
    updateState.lastPollingError = null;

    // Poll every 1.5 seconds
    updateState.pollingInterval = setInterval(async () => {
        try {
            const api = getOtaApi();
            if (updateState.stage === 'downloading') {
                const status = await api.getOtaDownloadStatus();
                // Reset failure count on success
                updateState.pollingFailureCount = 0;
                updateState.lastPollingError = null;

                updateDownloadProgress(status);

                // Check if download completed
                if (!status.downloading && status.progress >= 1.0) {
                    showSuccessMessage('Download completed successfully');
                    // Automatically start distribution
                    await startDistribution();
                } else if (!status.downloading && status.progress < 1.0) {
                    // Download stopped but not complete
                    showWarningMessage('Download stopped before completion');
                    stopProgressPolling();
                    updateState.stage = 'idle';
                    updateButtonStates('idle');
                    updateUpdateStage('idle');
                }
            } else if (updateState.stage === 'distributing') {
                const status = await api.getOtaDistributionStatus();
                // Reset failure count on success
                updateState.pollingFailureCount = 0;
                updateState.lastPollingError = null;

                updateDistributionProgress(status);

                // Check if distribution completed
                if (!status.distributing && status.overall_progress >= 1.0) {
                    showSuccessMessage('Distribution completed successfully');
                    stopProgressPolling();
                    updateState.stage = 'idle';
                    updateButtonStates('idle');
                    updateUpdateStage('idle');
                } else if (!status.distributing && status.overall_progress < 1.0) {
                    // Distribution stopped but not complete
                    showWarningMessage('Distribution stopped before completion');
                    stopProgressPolling();
                    updateState.stage = 'idle';
                    updateButtonStates('idle');
                    updateUpdateStage('idle');
                }
            }
        } catch (error) {
            console.error('Progress polling error:', error);

            // Track consecutive failures
            updateState.pollingFailureCount++;
            updateState.lastPollingError = error.message;

            // After 3 consecutive failures, notify user (especially for root node disconnection)
            if (updateState.pollingFailureCount >= 3) {
                const errorMsg = error.message.includes('offline') || error.message.includes('unreachable')
                    ? error.message
                    : `Unable to get update status: ${error.message}. Root node may be offline.`;

                // Only show error once per failure streak
                if (updateState.pollingFailureCount === 3) {
                    showErrorMessage(errorMsg, {
                        retryCallback: () => {
                            // Reset failure count and continue polling
                            updateState.pollingFailureCount = 0;
                            updateState.lastPollingError = null;
                        },
                        retryLabel: 'Retry Status Check'
                    });
                }

                // If we have 10+ consecutive failures, stop polling to avoid infinite errors
                if (updateState.pollingFailureCount >= 10) {
                    showErrorMessage('Multiple status check failures. Stopping automatic updates. Please check root node connection.');
                    stopProgressPolling();
                    updateState.stage = 'idle';
                    updateButtonStates('idle');
                    updateUpdateStage('idle');
                }
            }
        }
    }, 1500);
}

/**
 * Stop progress polling.
 */
function stopProgressPolling() {
    if (updateState.pollingInterval) {
        clearInterval(updateState.pollingInterval);
        updateState.pollingInterval = null;
    }
    // Reset failure tracking
    updateState.pollingFailureCount = 0;
    updateState.lastPollingError = null;
    // Also stop state updates integration
    stopStateUpdatesIntegration();
}

/**
 * Start distribution after download completes.
 */
async function startDistribution() {
    try {
        updateState.stage = 'distributing';
        updateButtonStates('distributing');
        updateUpdateStage('distributing');
        showInfoMessage('Starting firmware distribution to mesh nodes...');

        const api = getOtaApi();
        await api.startOtaDistribution();
        showSuccessMessage('Distribution started successfully');

        // Show distribution progress section
        elements.distributionSection.style.display = 'block';
        updateDistributionProgress({
            distributing: true,
            overall_progress: 0.0,
            nodes_total: 0,
            nodes_complete: 0,
            nodes_failed: 0
        });

        // Start state updates integration for node list
        startStateUpdatesIntegration();
    } catch (error) {
        if (error.message.includes('Downgrade')) {
            showWarningMessage('Downgrade prevented during distribution');
        } else {
            // Show error with retry option
            showErrorMessage(`Failed to start distribution: ${error.message}`, {
                retryCallback: startDistribution,
                retryLabel: 'Retry Distribution'
            });
        }
        updateState.stage = 'idle';
        updateButtonStates('idle');
        updateUpdateStage('idle');
    }
}

/**
 * Update download progress display.
 *
 * @param {Object} status - Download status { downloading: boolean, progress: number }
 */
function updateDownloadProgress(status) {
    const progress = Math.max(0, Math.min(1, status.progress || 0));
    const percentage = Math.round(progress * 100);

    // Update progress bar
    const progressFill = elements.downloadProgress.querySelector('.progress-fill');
    if (progressFill) {
        progressFill.style.width = `${percentage}%`;
    }
    elements.downloadProgress.setAttribute('aria-valuenow', percentage);

    // Update percentage text
    elements.downloadPercentage.textContent = `${percentage}%`;

    // Update status text
    if (status.downloading) {
        elements.downloadStatus.textContent = `Downloading... ${percentage}%`;
    } else if (progress >= 1.0) {
        elements.downloadStatus.textContent = 'Download completed';
        if (progressFill) {
            progressFill.classList.add('success');
        }
    } else {
        elements.downloadStatus.textContent = 'Download stopped';
        if (progressFill) {
            progressFill.classList.add('warning');
        }
    }

    updateState.downloadProgress = progress;
}

/**
 * Update distribution progress display.
 *
 * @param {Object} status - Distribution status object
 */
function updateDistributionProgress(status) {
    const progress = Math.max(0, Math.min(1, status.overall_progress || 0));
    const percentage = Math.round(progress * 100);

    // Update progress bar
    const progressFill = elements.distributionProgress.querySelector('.progress-fill');
    if (progressFill) {
        progressFill.style.width = `${percentage}%`;
    }
    elements.distributionProgress.setAttribute('aria-valuenow', percentage);

    // Update percentage text
    elements.distributionPercentage.textContent = `${percentage}%`;

    // Update status text
    let statusText = '';
    if (status.distributing) {
        statusText = `Distributing... ${percentage}%`;
        if (status.total_blocks > 0) {
            statusText += ` (Block ${status.current_block} of ${status.total_blocks})`;
        }
    } else if (progress >= 1.0) {
        statusText = 'Distribution completed';
        if (progressFill) {
            progressFill.classList.add('success');
        }
    } else {
        statusText = 'Distribution stopped';
        if (progressFill) {
            progressFill.classList.add('warning');
        }
    }
    elements.distributionStatus.textContent = statusText;

    // Update node information
    if (status.nodes_total > 0) {
        const nodesText = `Nodes: ${status.nodes_complete}/${status.nodes_total} complete`;
        const failedText = status.nodes_failed > 0 ? `, ${status.nodes_failed} failed` : '';
        elements.distributionNodes.textContent = nodesText + failedText;
    }

    updateState.distributionProgress = progress;
}

/**
 * Show status message.
 *
 * @param {string} message - Message text
 * @param {string} type - Message type (success, error, warning, info)
 * @param {number} timeout - Auto-hide timeout in milliseconds (0 = no auto-hide)
 * @param {Object} options - Optional configuration { retryCallback, retryLabel }
 */
function showStatusMessage(message, type = 'info', timeout = 0, options = {}) {
    const messageDiv = document.createElement('div');
    messageDiv.className = `status-message ${type}`;
    messageDiv.setAttribute('role', 'alert');
    messageDiv.setAttribute('aria-live', 'assertive');

    const messageText = document.createElement('span');
    messageText.textContent = message;
    messageDiv.appendChild(messageText);

    // Add retry button if retry callback provided
    if (options.retryCallback && typeof options.retryCallback === 'function') {
        const retryBtn = document.createElement('button');
        retryBtn.className = 'btn btn-secondary retry-btn';
        retryBtn.textContent = options.retryLabel || 'Retry';
        retryBtn.setAttribute('aria-label', 'Retry operation');
        retryBtn.onclick = () => {
            messageDiv.remove();
            options.retryCallback();
        };
        messageDiv.appendChild(retryBtn);
    }

    // Add close button
    const closeBtn = document.createElement('button');
    closeBtn.className = 'close-btn';
    closeBtn.textContent = '×';
    closeBtn.setAttribute('aria-label', 'Close message');
    closeBtn.onclick = () => messageDiv.remove();
    messageDiv.appendChild(closeBtn);

    elements.statusMessages.appendChild(messageDiv);

    // Auto-hide after timeout
    if (timeout > 0) {
        setTimeout(() => {
            if (messageDiv.parentNode) {
                messageDiv.remove();
            }
        }, timeout);
    }

    // Scroll to message
    messageDiv.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

/**
 * Show success message.
 */
function showSuccessMessage(message) {
    showStatusMessage(message, 'success', 5000);
}

/**
 * Show error message.
 *
 * @param {string} message - Error message text
 * @param {Object} options - Optional configuration { retryCallback, retryLabel }
 */
function showErrorMessage(message, options = {}) {
    showStatusMessage(message, 'error', 0, options); // Don't auto-hide errors
}

/**
 * Show warning message.
 */
function showWarningMessage(message) {
    showStatusMessage(message, 'warning', 8000);
}

/**
 * Show info message.
 */
function showInfoMessage(message) {
    showStatusMessage(message, 'info', 5000);
}

/**
 * Update update stage display.
 *
 * @param {string} stage - Update stage (idle, downloading, distributing, rebooting)
 */
function updateUpdateStage(stage) {
    updateState.stage = stage;
    updateButtonStates(stage);

    // Update visual stage indicator
    const stageLabels = {
        'idle': 'Idle',
        'downloading': 'Downloading',
        'distributing': 'Distributing',
        'rebooting': 'Rebooting'
    };

    const stageLabel = stageLabels[stage] || 'Unknown';
    elements.stageText.textContent = stageLabel;

    // Show/hide stage indicator
    if (stage === 'idle') {
        elements.stageIndicator.style.display = 'none';
    } else {
        elements.stageIndicator.style.display = 'flex';
        // Update stage indicator class for styling
        elements.stageIndicator.className = `stage-indicator stage-${stage}`;
    }
}

/**
 * Load and display current firmware version.
 */
async function loadCurrentVersion() {
    try {
        const api = getOtaApi();
        const version = await api.getOtaVersion();
        elements.versionDisplay.textContent = version;
    } catch (error) {
        elements.versionDisplay.textContent = 'Error loading version';
        console.error('Failed to load version:', error);
        // Show error with retry option
        showErrorMessage(`Failed to load firmware version: ${error.message}`, {
            retryCallback: loadCurrentVersion,
            retryLabel: 'Retry'
        });
    }
}

/**
 * Update version display.
 *
 * @param {string} version - Version string
 */
function updateVersionDisplay(version) {
    elements.versionDisplay.textContent = version;
}

/**
 * Update node list display.
 *
 * @param {Array} nodes - Array of node objects with update status
 */
function updateNodeList(nodes) {
    if (!nodes || nodes.length === 0) {
        elements.nodeList.style.display = 'none';
        return;
    }

    elements.nodeList.style.display = 'block';
    elements.nodeListContent.innerHTML = '';

    nodes.forEach(node => {
        const nodeDiv = document.createElement('div');
        nodeDiv.className = `node-item ${node.status}`;
        nodeDiv.setAttribute('aria-label', `Node ${node.id}, status: ${node.status}`);

        const nodeId = document.createElement('div');
        nodeId.className = 'node-id';
        nodeId.textContent = `Node: ${node.id}`;
        nodeDiv.appendChild(nodeId);

        const nodeStatus = document.createElement('div');
        nodeStatus.className = 'node-status';
        nodeStatus.textContent = `Status: ${node.status}`;
        nodeDiv.appendChild(nodeStatus);

        elements.nodeListContent.appendChild(nodeDiv);
    });
}

/**
 * Fetch mesh state and distribution status, then update node list.
 * Integrates with periodic state updates API and distribution status.
 */
async function updateNodeListFromState() {
    try {
        // Fetch both mesh state and distribution status
        const [stateResponse, distStatusResponse] = await Promise.all([
            fetch('/api/mesh/state'),
            fetch('/api/ota/distribution/status')
        ]);

        let state = null;
        let distStatus = null;

        if (stateResponse.ok) {
            state = await stateResponse.json();
        }

        if (distStatusResponse.ok) {
            distStatus = await distStatusResponse.json();
        }

        // Only update node list if we're in distribution phase
        if (updateState.stage !== 'distributing') {
            return;
        }

        if (state && state.nodes && Array.isArray(state.nodes)) {
            // Use distribution status to determine completion counts
            const nodesComplete = distStatus ? distStatus.nodes_complete || 0 : 0;
            const nodesFailed = distStatus ? distStatus.nodes_failed || 0 : 0;
            const nodesTotal = distStatus ? distStatus.nodes_total || state.nodes.length : state.nodes.length;

            // Map state nodes to OTA node list format
            // Use distribution status to better determine node status
            let completedCount = 0;
            let failedCount = 0;

            const otaNodes = state.nodes.map((node, index) => {
                let otaStatus = 'pending';

                // Use distribution status to determine if node is complete or failed
                // Since we don't have per-node info, we approximate based on order
                if (node.status === 'connected') {
                    // Approximate: first N nodes are complete, last M nodes are failed
                    // This is an approximation since API doesn't provide per-node status
                    if (completedCount < nodesComplete) {
                        otaStatus = 'completed';
                        completedCount++;
                    } else if (failedCount < nodesFailed && index >= nodesTotal - nodesFailed) {
                        otaStatus = 'failed';
                        failedCount++;
                    } else {
                        otaStatus = 'in-progress';
                    }
                } else if (node.status === 'disconnected') {
                    otaStatus = 'failed';
                    failedCount++;
                }

                return {
                    id: node.node_id || node.id || `node-${index}`,
                    status: otaStatus
                };
            });

            updateNodeList(otaNodes);
        } else if (distStatus && distStatus.nodes_total > 0) {
            // Fallback: if we have distribution status but no state, show generic node list
            const otaNodes = [];
            for (let i = 0; i < distStatus.nodes_total; i++) {
                let status = 'in-progress';
                if (i < distStatus.nodes_complete) {
                    status = 'completed';
                } else if (i >= distStatus.nodes_total - distStatus.nodes_failed) {
                    status = 'failed';
                }
                otaNodes.push({
                    id: `node-${i}`,
                    status: status
                });
            }
            updateNodeList(otaNodes);
        }
    } catch (error) {
        // Silently fail - state updates are optional
        console.debug('Failed to fetch mesh state or distribution status:', error);
    }
}

/**
 * Start periodic state updates integration.
 * Polls mesh state API to update node list during distribution.
 */
function startStateUpdatesIntegration() {
    // Poll every 2 seconds during distribution
    if (updateState.stateUpdateInterval) {
        clearInterval(updateState.stateUpdateInterval);
    }

    updateState.stateUpdateInterval = setInterval(() => {
        if (updateState.stage === 'distributing') {
            updateNodeListFromState();
        }
    }, 2000);
}

/**
 * Stop state updates integration.
 */
function stopStateUpdatesIntegration() {
    if (updateState.stateUpdateInterval) {
        clearInterval(updateState.stateUpdateInterval);
        updateState.stateUpdateInterval = null;
    }
}

/**
 * Compare two version strings.
 *
 * @param {string} current - Current version
 * @param {string} newVersion - New version
 * @returns {Object} Comparison result { isUpgrade: boolean, isDowngrade: boolean, same: boolean }
 */
function compareVersions(current, newVersion) {
    // Simple version comparison (assumes semantic versioning)
    // This is a basic implementation - could be enhanced for more complex version formats
    const currentParts = current.split('.').map(Number);
    const newParts = newVersion.split('.').map(Number);

    for (let i = 0; i < Math.max(currentParts.length, newParts.length); i++) {
        const currentPart = currentParts[i] || 0;
        const newPart = newParts[i] || 0;

        if (newPart > currentPart) {
            return { isUpgrade: true, isDowngrade: false, same: false };
        } else if (newPart < currentPart) {
            return { isUpgrade: false, isDowngrade: true, same: false };
        }
    }

    return { isUpgrade: false, isDowngrade: false, same: true };
}

// Initialize UI when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initOtaUI);
} else {
    initOtaUI();
}
