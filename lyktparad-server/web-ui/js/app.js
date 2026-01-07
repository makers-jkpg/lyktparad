// Global variables
let gridData = new Uint8Array(768);
let tempo = 250;
let numRows = 4;
let selectedRow = null;
let selectedCol = null;
let selectedAction = null;
let updateInterval;
let importHideTimeout = null;
let sequenceIndicatorInterval = null;
let localPointerTimer = null;
let currentLocalPointer = 0;
let maxSquares = 64;
let sequenceSynced = false;
let buttonStatePollInterval = null;

// Error handling helper function
function handleApiError(error, feedbackElement, retryCallback) {
  if (!feedbackElement) {
    console.error('API error:', error);
    return;
  }

  // Check if error is a root node disconnection error (503)
  const isDisconnectionError = error.message && (
    error.message.includes('503') ||
    error.message.includes('offline') ||
    error.message.includes('unreachable') ||
    error.message.includes('Root node unavailable')
  );

  if (isDisconnectionError) {
    // Get connection status to find root node IP for direct access suggestion
    fetch('/api/connection/status')
      .then(response => response.json())
      .then(status => {
        let errorMsg = 'Root node is offline or unreachable.';
        if (status.root_node_ip) {
          errorMsg += ` You can access it directly at http://${status.root_node_ip}`;
        }
        feedbackElement.textContent = errorMsg;
        feedbackElement.className = 'sequence-control-feedback-error';

        // Add retry button if retry callback provided
        if (retryCallback && typeof retryCallback === 'function') {
          // Remove existing retry button if any
          const existingRetry = feedbackElement.parentElement.querySelector('.retry-btn');
          if (existingRetry) {
            existingRetry.remove();
          }

          const retryBtn = document.createElement('button');
          retryBtn.className = 'btn btn-secondary retry-btn';
          retryBtn.textContent = 'Retry';
          retryBtn.style.marginLeft = '10px';
          retryBtn.onclick = () => {
            feedbackElement.textContent = 'Retrying...';
            retryCallback();
          };
          feedbackElement.parentElement.appendChild(retryBtn);
        }
      })
      .catch(() => {
        feedbackElement.textContent = 'Root node is offline or unreachable.';
        feedbackElement.className = 'sequence-control-feedback-error';
      });
  } else {
    // Regular error
    feedbackElement.textContent = 'Error: ' + (error.message || 'Unknown error');
    feedbackElement.className = 'sequence-control-feedback-error';
  }

  console.error('API error:', error);
}

// API functions
function updateNodeCount() {
  fetch('/api/nodes')
    .then(response => response.json())
    .then(data => {
      document.getElementById('nodeCount').textContent = data.nodes;
    })
    .catch(err => {
      console.error('Node count update error:', err);
    });
}

// Plugin activation API functions
async function activatePlugin(pluginName) {
  try {
    const response = await fetch('/api/plugin/activate', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({ name: pluginName })
    });

    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.error || 'Failed to activate plugin');
    }

    const result = await response.json();
    return result;
  } catch (error) {
    console.error('Plugin activation error:', error);
    throw error;
  }
}

async function deactivatePlugin(pluginName) {
  try {
    const response = await fetch('/api/plugin/deactivate', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({ name: pluginName })
    });

    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.error || 'Failed to deactivate plugin');
    }

    const result = await response.json();
    return result;
  } catch (error) {
    console.error('Plugin deactivation error:', error);
    throw error;
  }
}

async function stopPlugin(pluginName) {
  try {
    const response = await fetch('/api/plugin/stop', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({ name: pluginName })
    });

    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.error || 'Failed to stop plugin');
    }

    const result = await response.json();
    return result;
  } catch (error) {
    console.error('Plugin stop error:', error);
    throw error;
  }
}

async function pausePlugin(pluginName) {
  try {
    const response = await fetch('/api/plugin/pause', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({ name: pluginName })
    });

    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.error || 'Failed to pause plugin');
    }

    const result = await response.json();
    return result;
  } catch (error) {
    console.error('Plugin pause error:', error);
    throw error;
  }
}

async function resetPlugin(pluginName) {
  try {
    const response = await fetch('/api/plugin/reset', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({ name: pluginName })
    });

    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.error || 'Failed to reset plugin');
    }

    const result = await response.json();
    return result;
  } catch (error) {
    console.error('Plugin reset error:', error);
    throw error;
  }
}

async function getActivePlugin() {
  try {
    const response = await fetch('/api/plugin/active');
    if (!response.ok) {
      throw new Error('Failed to get active plugin');
    }
    const result = await response.json();
    return result.active;
  } catch (error) {
    console.error('Get active plugin error:', error);
    return null;
  }
}

// Utility functions
function rgbToHex(r, g, b) {
  return '#' + [r, g, b].map(x => {
    const hex = x.toString(16);
    return hex.length === 1 ? '0' + hex : hex;
  }).join('');
}

function hexToRgb(hex) {
  const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return result ? {
    r: parseInt(result[1], 16),
    g: parseInt(result[2], 16),
    b: parseInt(result[3], 16)
  } : null;
}

function getChannelIndex(row, col, channel) {
  return (row * 16 + col) * 3 + channel;
}

function getSquareColor(row, col) {
  const idx = getChannelIndex(row, col, 0);
  return {
    r: gridData[idx],
    g: gridData[idx + 1],
    b: gridData[idx + 2]
  };
}

function setSquareColor(row, col, r, g, b) {
  const idx = getChannelIndex(row, col, 0);
  gridData[idx] = r;
  gridData[idx + 1] = g;
  gridData[idx + 2] = b;
}

function setRowColor(row, r, g, b) {
  for (let col = 0; col < 16; col++) {
    setSquareColor(row, col, r, g, b);
  }
}

function setColumnColor(col, r, g, b) {
  for (let row = 0; row < 16; row++) {
    setSquareColor(row, col, r, g, b);
  }
}

function setAllColor(r, g, b) {
  for (let row = 0; row < 16; row++) {
    for (let col = 0; col < 16; col++) {
      setSquareColor(row, col, r, g, b);
    }
  }
}

function quantizeColor(r, g, b) {
  return {
    r: Math.max(0, Math.min(15, Math.floor(r / 16))),
    g: Math.max(0, Math.min(15, Math.floor(g / 16))),
    b: Math.max(0, Math.min(15, Math.floor(b / 16)))
  };
}

function initializeDefaultPattern() {
  for (let row = 0; row < 16; row++) {
    if (row % 2 === 0) {
      setRowColor(row, 15, 15, 15);
    } else {
      setRowColor(row, 0, 0, 15);
    }
  }
}

function updateGridRows() {
  const gridContainer = document.getElementById('gridContainer');
  if (gridContainer) {
    gridContainer.style.gridTemplateRows = 'auto repeat(' + numRows + ', 1fr)';
  }
  for (let row = 0; row < 16; row++) {
    const rowElements = document.querySelectorAll(`[data-row="${row}"]`);
    if (row < numRows) {
      rowElements.forEach(el => el.style.display = '');
    } else {
      rowElements.forEach(el => el.style.display = 'none');
    }
  }
}

function renderGrid() {
  const squares = document.querySelectorAll('.grid-square');
  squares.forEach(function(square) {
    const row = parseInt(square.dataset.row);
    const col = parseInt(square.dataset.col);
    const color = getSquareColor(row, col);
    const r8 = color.r * 17;
    const g8 = color.g * 17;
    const b8 = color.b * 17;
    square.style.backgroundColor = 'rgb(' + r8 + ', ' + g8 + ', ' + b8 + ')';
  });
}

function showColorPicker(row, col, action) {
  selectedRow = row;
  selectedCol = col;
  selectedAction = action;
  let currentColor = {r: 0, g: 0, b: 0};
  if (action === 'square') {
    currentColor = getSquareColor(row, col);
  } else if (action === 'row') {
    currentColor = getSquareColor(row, 0);
  } else if (action === 'col') {
    currentColor = getSquareColor(0, col);
  } else if (action === 'all') {
    currentColor = getSquareColor(0, 0);
  }
  const r8 = currentColor.r * 17;
  const g8 = currentColor.g * 17;
  const b8 = currentColor.b * 17;
  const colorPicker = document.getElementById('colorPicker');
  colorPicker.value = rgbToHex(r8, g8, b8);
  colorPicker.click();
}

function packGridData(numRows) {
  const numSquares = numRows * 16;
  const packedSize = (numSquares / 2) * 3;
  const packed = new Uint8Array(packedSize);
  for (let i = 0; i < numSquares; i += 2) {
    const row0 = Math.floor(i / 16);
    const col0 = i % 16;
    const row1 = Math.floor((i + 1) / 16);
    const col1 = (i + 1) % 16;
    const color0 = getSquareColor(row0, col0);
    const color1 = getSquareColor(row1, col1);
    const byteIdx = Math.floor(i / 2) * 3;
    packed[byteIdx] = (color0.r << 4) | color0.g;
    packed[byteIdx + 1] = (color0.b << 4) | color1.r;
    packed[byteIdx + 2] = (color1.g << 4) | color1.b;
  }
  return packed;
}

function generateCSVLine(index, r, g, b) {
  return index + ';' + r + ';' + g + ';' + b;
}

function exportGridToCSV() {
  const csvLines = [];
  const numSquares = numRows * 16;
  for (let i = 0; i < numSquares; i++) {
    const row = Math.floor(i / 16);
    const col = i % 16;
    const index = i + 1;
    const color = getSquareColor(row, col);
    csvLines.push(generateCSVLine(index, color.r, color.g, color.b));
  }
  return csvLines.join('\n');
}

function parseCSVLine(line) {
  const trimmed = line.trim();
  if (!trimmed) {
    return null;
  }
  const parts = trimmed.split(';');
  if (parts.length !== 4) {
    throw new Error('Invalid CSV line: expected 4 columns, got ' + parts.length);
  }
  const index = parseInt(parts[0], 10);
  const r = parseInt(parts[1], 10);
  const g = parseInt(parts[2], 10);
  const b = parseInt(parts[3], 10);
  if (isNaN(index) || index < 1 || index > 256) {
    throw new Error('Invalid index: must be 1-256, got ' + parts[0]);
  }
  if (isNaN(r) || r < 0 || r > 15) {
    throw new Error('Invalid RED value: must be 0-15, got ' + parts[1]);
  }
  if (isNaN(g) || g < 0 || g > 15) {
    throw new Error('Invalid GREEN value: must be 0-15, got ' + parts[2]);
  }
  if (isNaN(b) || b < 0 || b > 15) {
    throw new Error('Invalid BLUE value: must be 0-15, got ' + parts[3]);
  }
  return { index: index, r: r, g: g, b: b };
}

function parseCSVText(csvText) {
  const lines = csvText.split(/\r?\n/);
  const parsedData = [];
  for (let i = 0; i < lines.length; i++) {
    try {
      const parsed = parseCSVLine(lines[i]);
      if (parsed !== null) {
        parsedData.push(parsed);
      }
    } catch (err) {
      throw new Error('Line ' + (i + 1) + ': ' + err.message);
    }
  }
  return parsedData;
}

function indexToGridPosition(index) {
  const row = Math.floor((index - 1) / 16);
  const col = (index - 1) % 16;
  if (row < 0 || row > 15 || col < 0 || col > 15) {
    throw new Error('Index ' + index + ' maps to invalid grid position (row: ' + row + ', col: ' + col + ')');
  }
  return { row: row, col: col };
}

function populateGridFromCSV(parsedData) {
  gridData = new Uint8Array(768);
  let maxIndex = 0;
  for (let i = 0; i < parsedData.length; i++) {
    const entry = parsedData[i];
    if (entry.index > maxIndex) {
      maxIndex = entry.index;
    }
    const pos = indexToGridPosition(entry.index);
    setSquareColor(pos.row, pos.col, entry.r, entry.g, entry.b);
  }
  if (maxIndex === 0) {
    throw new Error('No valid data found in CSV');
  }
  const calculatedRows = Math.ceil(maxIndex / 16);
  if (calculatedRows < 1 || calculatedRows > 16) {
    throw new Error('Calculated row count out of range: ' + calculatedRows + ' (must be 1-16)');
  }
  return calculatedRows;
}

function unpackGridData(packed) {
  gridData = new Uint8Array(768);
  const numPairs = Math.floor(packed.length / 3);
  const numSquares = numPairs * 2;
  for (let i = 0; i < numSquares; i += 2) {
    const byteIdx = Math.floor(i / 2) * 3;
    if (byteIdx + 2 >= packed.length) break;
    const byte0 = packed[byteIdx];
    const byte1 = packed[byteIdx + 1];
    const byte2 = packed[byteIdx + 2];
    const row0 = Math.floor(i / 16);
    const col0 = i % 16;
    const idx0 = (row0 * 16 + col0) * 3;
    gridData[idx0] = (byte0 >> 4) & 0x0F;
    gridData[idx0 + 1] = byte0 & 0x0F;
    gridData[idx0 + 2] = (byte1 >> 4) & 0x0F;
    const row1 = Math.floor((i + 1) / 16);
    const col1 = (i + 1) % 16;
    const idx1 = (row1 * 16 + col1) * 3;
    gridData[idx1] = byte1 & 0x0F;
    gridData[idx1 + 1] = (byte2 >> 4) & 0x0F;
    gridData[idx1 + 2] = byte2 & 0x0F;
  }
}

function updateRhythmDisplay() {
  const display = document.getElementById('rhythmDisplay');
  if (display) {
    display.textContent = tempo + 'ms';
  }
}

function showImportUI() {
  if (importHideTimeout) {
    clearTimeout(importHideTimeout);
    importHideTimeout = null;
  }
  const importContainer = document.getElementById('importContainer');
  const exportContainer = document.getElementById('exportContainer');
  const importTextarea = document.getElementById('importTextarea');
  const importFeedback = document.getElementById('importFeedback');
  exportContainer.style.display = 'none';
  importContainer.style.display = 'block';
  importTextarea.value = '';
  importTextarea.focus();
  importFeedback.textContent = '';
  importFeedback.className = '';
}

function hideImportUI() {
  const importContainer = document.getElementById('importContainer');
  const importTextarea = document.getElementById('importTextarea');
  const importFeedback = document.getElementById('importFeedback');
  importContainer.style.display = 'none';
  importTextarea.value = '';
  importFeedback.textContent = '';
  importFeedback.className = '';
}

function importSequence() {
  const importTextarea = document.getElementById('importTextarea');
  const importFeedback = document.getElementById('importFeedback');
  const csvText = importTextarea.value.trim();
  importFeedback.textContent = '';
  importFeedback.className = '';
  if (!csvText) {
    importFeedback.textContent = 'Please paste CSV data';
    importFeedback.className = 'import-feedback-error';
    return;
  }
  try {
    const parsedData = parseCSVText(csvText);
    if (parsedData.length === 0) {
      importFeedback.textContent = 'No valid CSV data found';
      importFeedback.className = 'import-feedback-error';
      return;
    }
    const importedRows = populateGridFromCSV(parsedData);
    numRows = importedRows;
    maxSquares = numRows * 16;
    const rowCountSelect = document.getElementById('rowCountSelect');
    rowCountSelect.value = numRows;
    updateGridRows();
    renderGrid();
    stopSequenceIndicator();
    importFeedback.textContent = 'Import successful!';
    importFeedback.className = 'import-feedback-success';
    if (importHideTimeout) {
      clearTimeout(importHideTimeout);
    }
    importHideTimeout = setTimeout(function() {
      hideImportUI();
      importHideTimeout = null;
    }, 2000);
  } catch (err) {
    importFeedback.textContent = 'Import error: ' + (err.message || 'Unknown error');
    importFeedback.className = 'import-feedback-error';
    console.error('Import error:', err);
  }
}

function exportSequence() {
  try {
    if (numRows < 1 || numRows > 16) {
      throw new Error('Row count out of range (1-16)');
    }
    const csvText = exportGridToCSV();
    const exportTextarea = document.getElementById('exportTextarea');
    const exportContainer = document.getElementById('exportContainer');
    const importContainer = document.getElementById('importContainer');
    const exportFeedback = document.getElementById('exportFeedback');
    importContainer.style.display = 'none';
    exportTextarea.value = csvText;
    exportContainer.style.display = 'block';
    exportTextarea.select();
    exportFeedback.textContent = '';
    exportFeedback.className = '';
  } catch (err) {
    const exportFeedback = document.getElementById('exportFeedback');
    exportFeedback.textContent = 'Error: ' + err.message;
    exportFeedback.className = 'export-feedback-error';
    console.error('Export error:', err);
  }
}

function updateSequenceIndicator() {
  fetch('/api/sequence/pointer')
    .then(response => {
      if (!response.ok) {
        return Promise.reject(new Error('HTTP error: ' + response.status));
      }
      return response.text();
    })
    .then(pointerText => {
      const pointer = parseInt(pointerText, 10);
      if (isNaN(pointer) || pointer < 0 || pointer > 255) {
        return;
      }
      currentLocalPointer = pointer;
      updateIndicatorBorder();
    })
    .catch(err => {
      console.error('Failed to fetch pointer:', err);
    });
}

function updateIndicatorBorder() {
  const row = Math.floor(currentLocalPointer / 16);
  const col = currentLocalPointer % 16;
  document.querySelectorAll('.grid-square.current').forEach(sq => sq.classList.remove('current'));
  const currentSquare = document.querySelector(`[data-row="${row}"][data-col="${col}"]`);
  if (currentSquare) {
    currentSquare.classList.add('current');
  }
}

function startSequenceIndicator() {
  if (sequenceIndicatorInterval) {
    clearInterval(sequenceIndicatorInterval);
  }
  if (localPointerTimer) {
    clearInterval(localPointerTimer);
  }
  maxSquares = numRows * 16;
  const backendSyncInterval = tempo * 16;
  sequenceIndicatorInterval = setInterval(updateSequenceIndicator, backendSyncInterval);
  localPointerTimer = setInterval(function() {
    currentLocalPointer = (currentLocalPointer + 1) % maxSquares;
    updateIndicatorBorder();
  }, tempo);
  updateSequenceIndicator();
}

function stopSequenceIndicator() {
  if (sequenceIndicatorInterval) {
    clearInterval(sequenceIndicatorInterval);
    sequenceIndicatorInterval = null;
  }
  if (localPointerTimer) {
    clearInterval(localPointerTimer);
    localPointerTimer = null;
  }
  currentLocalPointer = 0;
  document.querySelectorAll('.grid-square.current').forEach(sq => sq.classList.remove('current'));
}

function updateSequenceButtonStates() {
  const startButton = document.getElementById('startButton');
  const stopButton = document.getElementById('stopButton');
  const resetButton = document.getElementById('resetButton');
  if (!startButton || !stopButton || !resetButton) return;
  fetch('/api/sequence/status')
    .then(response => {
      if (!response.ok) {
        startButton.disabled = true;
        stopButton.disabled = true;
        resetButton.disabled = true;
        return;
      }
      return response.json();
    })
    .then(status => {
      if (status === undefined) return;
      const isActive = status.active;
      startButton.disabled = !sequenceSynced || isActive;
      stopButton.disabled = !isActive;
      resetButton.disabled = !sequenceSynced;
    })
    .catch(err => {
      console.error('Failed to check sequence status:', err);
      startButton.disabled = true;
      stopButton.disabled = true;
      resetButton.disabled = true;
    });
}

function handleSequenceStart() {
  const startButton = document.getElementById('startButton');
  const feedback = document.getElementById('sequenceControlFeedback');
  startButton.disabled = true;
  feedback.textContent = 'Starting...';
  feedback.className = '';
  fetch('/api/sequence/start', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      return response.json().then(err => Promise.reject(new Error(err.error || 'HTTP error: ' + response.status))).catch(() => Promise.reject(new Error('HTTP error: ' + response.status)));
    }
    return response.json();
  })
  .then(result => {
    if (result.success) {
      feedback.textContent = 'Sequence started!';
      feedback.className = 'sequence-control-feedback-success';
      startSequenceIndicator();
      updateSequenceButtonStates();
    } else {
      feedback.textContent = 'Error: ' + (result.error || 'Failed to start');
      feedback.className = 'sequence-control-feedback-error';
      updateSequenceButtonStates();
    }
  })
  .catch(async err => {
    // Handle 503 errors (root node unavailable)
    if (err.message && err.message.includes('HTTP error: 503')) {
      try {
        const errorResponse = await fetch('/api/sequence/start', { method: 'POST' }).catch(() => null);
        if (errorResponse && errorResponse.status === 503) {
          const errorData = await errorResponse.json().catch(() => ({}));
          handleApiError(new Error(errorData.message || 'Root node unavailable'), feedback, handleSequenceStart);
        } else {
          handleApiError(err, feedback, handleSequenceStart);
        }
      } catch {
        handleApiError(err, feedback, handleSequenceStart);
      }
    } else {
      handleApiError(err, feedback, handleSequenceStart);
    }
    updateSequenceButtonStates();
  });
}

function handleSequenceStop() {
  const stopButton = document.getElementById('stopButton');
  const feedback = document.getElementById('sequenceControlFeedback');
  stopButton.disabled = true;
  feedback.textContent = 'Stopping...';
  feedback.className = '';
  fetch('/api/sequence/stop', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      return response.json().then(err => Promise.reject(new Error(err.error || 'HTTP error: ' + response.status))).catch(() => Promise.reject(new Error('HTTP error: ' + response.status)));
    }
    return response.json();
  })
  .then(result => {
    if (result.success) {
      feedback.textContent = 'Sequence stopped!';
      feedback.className = 'sequence-control-feedback-success';
      stopSequenceIndicator();
      updateSequenceButtonStates();
    } else {
      feedback.textContent = 'Error: ' + (result.error || 'Failed to stop');
      feedback.className = 'sequence-control-feedback-error';
      updateSequenceButtonStates();
    }
  })
  .catch(async err => {
    if (err.message && err.message.includes('HTTP error: 503')) {
      try {
        const errorResponse = await fetch('/api/sequence/stop', { method: 'POST' }).catch(() => null);
        if (errorResponse && errorResponse.status === 503) {
          const errorData = await errorResponse.json().catch(() => ({}));
          handleApiError(new Error(errorData.message || 'Root node unavailable'), feedback, handleSequenceStop);
        } else {
          handleApiError(err, feedback, handleSequenceStop);
        }
      } catch {
        handleApiError(err, feedback, handleSequenceStop);
      }
    } else {
      handleApiError(err, feedback, handleSequenceStop);
    }
    updateSequenceButtonStates();
  });
}

function handleSequenceReset() {
  const resetButton = document.getElementById('resetButton');
  const feedback = document.getElementById('sequenceControlFeedback');
  resetButton.disabled = true;
  feedback.textContent = 'Resetting...';
  feedback.className = '';
  fetch('/api/sequence/reset', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      return response.json().then(err => Promise.reject(new Error(err.error || 'HTTP error: ' + response.status))).catch(() => Promise.reject(new Error('HTTP error: ' + response.status)));
    }
    return response.json();
  })
  .then(result => {
    if (result.success) {
      feedback.textContent = 'Sequence reset!';
      feedback.className = 'sequence-control-feedback-success';
      const wasActive = sequenceIndicatorInterval !== null || localPointerTimer !== null;
      if (wasActive) {
        stopSequenceIndicator();
        startSequenceIndicator();
      } else {
        currentLocalPointer = 0;
        updateIndicatorBorder();
      }
      updateSequenceButtonStates();
    } else {
      feedback.textContent = 'Error: ' + (result.error || 'Failed to reset');
      feedback.className = 'sequence-control-feedback-error';
      updateSequenceButtonStates();
    }
  })
  .catch(async err => {
    if (err.message && err.message.includes('HTTP error: 503')) {
      try {
        const errorResponse = await fetch('/api/sequence/reset', { method: 'POST' }).catch(() => null);
        if (errorResponse && errorResponse.status === 503) {
          const errorData = await errorResponse.json().catch(() => ({}));
          handleApiError(new Error(errorData.message || 'Root node unavailable'), feedback, handleSequenceReset);
        } else {
          handleApiError(err, feedback, handleSequenceReset);
        }
      } catch {
        handleApiError(err, feedback, handleSequenceReset);
      }
    } else {
      handleApiError(err, feedback, handleSequenceReset);
    }
    updateSequenceButtonStates();
  });
}

function syncGridData() {
  const syncButton = document.getElementById('syncButton');
  const syncFeedback = document.getElementById('syncFeedback');
  syncButton.disabled = true;
  syncButton.textContent = 'Syncing...';
  syncFeedback.textContent = '';
  syncFeedback.className = '';
  try {
    const packedData = packGridData(numRows);
    const payloadSize = 2 + packedData.length;
    const payload = new Uint8Array(payloadSize);
    const backendValue = Math.floor(tempo / 10);
    if (backendValue < 1 || backendValue > 255) {
      throw new Error('Tempo value out of range (10-2550ms)');
    }
    if (numRows < 1 || numRows > 16) {
      throw new Error('Row count out of range (1-16)');
    }
    payload[0] = backendValue;
    payload[1] = numRows;
    payload.set(packedData, 2);
    fetch('/api/sequence', {
      method: 'POST',
      body: payload
    })
    .then(response => response.json())
    .then(result => {
      if (result.success) {
        syncFeedback.textContent = 'Synced successfully!';
        syncFeedback.className = 'sync-feedback-success';
        sequenceSynced = true;
        stopSequenceIndicator();
        startSequenceIndicator();
        updateSequenceButtonStates();
      } else {
        syncFeedback.textContent = 'Error: ' + (result.error || 'Failed to sync');
        syncFeedback.className = 'sync-feedback-error';
        sequenceSynced = false;
        updateSequenceButtonStates();
      }
      syncButton.disabled = false;
      syncButton.textContent = 'Sync';
    })
    .catch(err => {
      // Handle 503 errors (root node unavailable)
      if (err.message && err.message.includes('HTTP error: 503')) {
        // Re-fetch to get error details - we need the payload from outer scope
        const syncPayload = payload; // Use payload from outer scope
        fetch('/api/sequence', { method: 'POST', body: syncPayload })
          .then(async errorResponse => {
            if (errorResponse.status === 503) {
              const errorData = await errorResponse.json().catch(() => ({}));
              handleApiError(new Error(errorData.message || 'Root node unavailable'), syncFeedback, () => {
                // Retry callback for sync
                syncSequence();
              });
            } else {
              handleApiError(err, syncFeedback);
            }
          })
          .catch(() => {
            handleApiError(err, syncFeedback);
          });
      } else {
        handleApiError(err, syncFeedback);
      }
      sequenceSynced = false;
      updateSequenceButtonStates();
      syncButton.disabled = false;
      syncButton.textContent = 'Sync';
    });
  } catch (err) {
    syncFeedback.textContent = 'Error: ' + err.message;
    syncFeedback.className = 'sync-feedback-error';
    sequenceSynced = false;
    updateSequenceButtonStates();
    syncButton.disabled = false;
    syncButton.textContent = 'Sync';
    console.error('Sync error:', err);
  }
}

// Connection status polling
let connectionStatusInterval = null;

function updateConnectionStatus() {
  fetch('/api/connection/status')
    .then(response => response.json())
    .then(data => {
      const icon = document.getElementById('connection-status-icon');
      const text = document.getElementById('connection-status-text');
      const details = document.getElementById('connection-status-details');

      if (!icon || !text || !details) {
        return; // Elements not found
      }

      // Update icon and text based on status
      icon.className = 'connection-status-icon';
      if (data.connected) {
        icon.classList.add('connected');
        text.textContent = 'Connected';
      } else if (data.status === 'not_registered') {
        icon.classList.add('unknown');
        text.textContent = 'Not Registered';
      } else if (data.status === 'offline') {
        icon.classList.add('offline');
        text.textContent = 'Offline';
      } else {
        icon.classList.add('unknown');
        text.textContent = 'Unknown';
      }

      // Update details
      let detailsText = '';
      if (data.root_node_ip) {
        detailsText = `IP: ${data.root_node_ip}`;
      }
      if (data.last_seen) {
        const lastSeen = new Date(data.last_seen);
        const ago = Math.floor((Date.now() - lastSeen.getTime()) / 1000);
        let agoText = '';
        if (ago < 60) {
          agoText = `${ago}s ago`;
        } else if (ago < 3600) {
          agoText = `${Math.floor(ago / 60)}m ago`;
        } else {
          agoText = `${Math.floor(ago / 3600)}h ago`;
        }
        if (detailsText) {
          detailsText += ` • Last seen: ${agoText}`;
        } else {
          detailsText = `Last seen: ${agoText}`;
        }
      }
      details.textContent = detailsText;
    })
    .catch(err => {
      console.error('Connection status update error:', err);
      const icon = document.getElementById('connection-status-icon');
      const text = document.getElementById('connection-status-text');
      if (icon && text) {
        icon.className = 'connection-status-icon unknown';
        text.textContent = 'Status Unknown';
      }
    });
}

function startConnectionStatusPolling() {
  // Update immediately
  updateConnectionStatus();
  // Poll every 8 seconds (within 5-10 second range)
  connectionStatusInterval = setInterval(updateConnectionStatus, 8000);
}

function stopConnectionStatusPolling() {
  if (connectionStatusInterval) {
    clearInterval(connectionStatusInterval);
    connectionStatusInterval = null;
  }
}

// Event handlers and initialization
document.addEventListener('DOMContentLoaded', function() {
  initializeDefaultPattern();
  renderGrid();
  updateNodeCount();
  updateRhythmDisplay();
  updateGridRows();
  startConnectionStatusPolling();
  updateInterval = setInterval(function() {
    updateNodeCount();
  }, 5000);
  buttonStatePollInterval = setInterval(function() {
    updateSequenceButtonStates();
  }, 2000);

  const gridContainer = document.getElementById('gridContainer');
  gridContainer.addEventListener('click', function(e) {
    const target = e.target;
    if (target.classList.contains('grid-square')) {
      const row = parseInt(target.dataset.row);
      const col = parseInt(target.dataset.col);
      showColorPicker(row, col, 'square');
    } else if (target.classList.contains('grid-label-row')) {
      const row = parseInt(target.dataset.row);
      showColorPicker(row, null, 'row');
    } else if (target.classList.contains('grid-label-col')) {
      const col = parseInt(target.dataset.col);
      showColorPicker(null, col, 'col');
    } else if (target.classList.contains('grid-label-z')) {
      showColorPicker(null, null, 'all');
    }
  });

  const colorPicker = document.getElementById('colorPicker');
  colorPicker.addEventListener('change', function(e) {
    const rgb = hexToRgb(e.target.value);
    if (rgb) {
      const quantized = quantizeColor(rgb.r, rgb.g, rgb.b);
      if (selectedAction === 'square') {
        setSquareColor(selectedRow, selectedCol, quantized.r, quantized.g, quantized.b);
      } else if (selectedAction === 'row') {
        setRowColor(selectedRow, quantized.r, quantized.g, quantized.b);
      } else if (selectedAction === 'col') {
        setColumnColor(selectedCol, quantized.r, quantized.g, quantized.b);
      } else if (selectedAction === 'all') {
        setAllColor(quantized.r, quantized.g, quantized.b);
      }
      renderGrid();
    }
  });

  function updateTempoButtonStates() {
    const tempoDecrease = document.getElementById('tempoDecrease');
    const tempoIncrease = document.getElementById('tempoIncrease');
    if (tempoDecrease) {
      tempoDecrease.disabled = tempo <= 10;
    }
    if (tempoIncrease) {
      tempoIncrease.disabled = tempo >= 2550;
    }
  }

  const tempoDecrease = document.getElementById('tempoDecrease');
  if (tempoDecrease) {
    tempoDecrease.addEventListener('click', function() {
      tempo = tempo - 10;
      tempo = Math.max(10, tempo);
      tempo = Math.round(tempo / 10) * 10;
      updateRhythmDisplay();
      updateTempoButtonStates();
      if (sequenceIndicatorInterval || localPointerTimer) {
        startSequenceIndicator();
      }
    });
  }

  const tempoIncrease = document.getElementById('tempoIncrease');
  if (tempoIncrease) {
    tempoIncrease.addEventListener('click', function() {
      tempo = tempo + 10;
      tempo = Math.min(2550, tempo);
      tempo = Math.round(tempo / 10) * 10;
      updateRhythmDisplay();
      updateTempoButtonStates();
      if (sequenceIndicatorInterval || localPointerTimer) {
        startSequenceIndicator();
      }
    });
  }

  updateTempoButtonStates();

  const rowCountSelect = document.getElementById('rowCountSelect');
  rowCountSelect.addEventListener('change', function(e) {
    numRows = parseInt(e.target.value);
    maxSquares = numRows * 16;
    updateGridRows();
  });

  const syncButton = document.getElementById('syncButton');
  syncButton.addEventListener('click', syncGridData);

  const startButton = document.getElementById('startButton');
  const stopButton = document.getElementById('stopButton');
  const resetButton = document.getElementById('resetButton');
  if (startButton) startButton.disabled = true;
  if (stopButton) stopButton.disabled = true;
  if (resetButton) resetButton.disabled = true;
  startButton.addEventListener('click', handleSequenceStart);
  stopButton.addEventListener('click', handleSequenceStop);
  resetButton.addEventListener('click', handleSequenceReset);
  updateSequenceButtonStates();

  const exportButton = document.getElementById('exportButton');
  exportButton.addEventListener('click', exportSequence);

  const importButton = document.getElementById('importButton');
  importButton.addEventListener('click', showImportUI);

  const confirmImportButton = document.getElementById('confirmImportButton');
  confirmImportButton.addEventListener('click', importSequence);

  const cancelImportButton = document.getElementById('cancelImportButton');
  cancelImportButton.addEventListener('click', hideImportUI);

  const importTextarea = document.getElementById('importTextarea');
  importTextarea.addEventListener('keydown', function(e) {
    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      importSequence();
    }
  });

  const copyExportButton = document.getElementById('copyExportButton');
  copyExportButton.addEventListener('click', function() {
    const exportTextarea = document.getElementById('exportTextarea');
    const exportFeedback = document.getElementById('exportFeedback');
    exportTextarea.select();
    exportTextarea.setSelectionRange(0, exportTextarea.value.length);
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(exportTextarea.value).then(function() {
          exportFeedback.textContent = 'Copied to clipboard!';
          exportFeedback.className = 'export-feedback-success';
        }).catch(function(err) {
          exportTextarea.focus();
          exportTextarea.select();
          if (document.execCommand('copy')) {
            exportFeedback.textContent = 'Copied to clipboard!';
            exportFeedback.className = 'export-feedback-success';
          } else {
            exportFeedback.textContent = 'Failed to copy';
            exportFeedback.className = 'export-feedback-error';
          }
        });
      } else {
        exportTextarea.focus();
        if (document.execCommand('copy')) {
          exportFeedback.textContent = 'Copied to clipboard!';
          exportFeedback.className = 'export-feedback-success';
        } else {
          exportFeedback.textContent = 'Copy not supported';
          exportFeedback.className = 'export-feedback-error';
        }
      }
    } catch (err) {
      exportFeedback.textContent = 'Copy failed: ' + err.message;
      exportFeedback.className = 'export-feedback-error';
    }
  });
});
