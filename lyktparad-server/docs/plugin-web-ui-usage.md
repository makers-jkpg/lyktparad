# Plugin Web UI Usage Guide

This document provides a comprehensive guide for using the Plugin Web UI JavaScript API.

**Copyright (c) 2025 the_louie**

## Overview

The Plugin Web UI system enables dynamic loading of plugin user interfaces and communication with plugins via binary data. All encoding/decoding is handled in JavaScript, with the ESP32 acting as a transparent proxy (zero CPU processing).

## Architecture

- **Frontend**: JavaScript module (`plugin-web-ui.js`) and integration functions (`app.js`)
- **Backend**: REST API endpoints on ESP32 embedded webserver or external Node.js server
- **Protocol**: JSON for bundles, raw bytes for data communication
- **Data Format**: Binary (Uint8Array) with maximum payload size of 512 bytes

## API Reference

### Core Module: `window.PluginWebUI`

The PluginWebUI module provides the core functionality for plugin web UI integration.

#### `loadPluginBundle(pluginName)`

Loads a plugin's HTML/CSS/JS bundle from the server and injects it into the DOM.

**Parameters:**
- `pluginName` (string): Name of the plugin to load (must match regex: `^[a-zA-Z0-9_-]+$`)

**Returns:**
- `Promise<Object>`: Resolves to bundle object with `html`, `js`, and `css` properties

**Throws:**
- `Error`: If plugin name is invalid, fetch fails, or bundle is malformed

**Example:**
```javascript
try {
  const bundle = await window.PluginWebUI.loadPluginBundle('rgb_effect');
  console.log('Bundle loaded:', bundle);
  // bundle = { html: '...', js: '...', css: '...' }
} catch (error) {
  console.error('Failed to load bundle:', error.message);
}
```

#### `sendPluginData(pluginName, data)`

Sends raw binary data to a plugin.

**Parameters:**
- `pluginName` (string): Name of the plugin (must match regex: `^[a-zA-Z0-9_-]+$`)
- `data` (ArrayBuffer|Uint8Array|number[]): Data to send (max 512 bytes)

**Returns:**
- `Promise<Object>`: Resolves to success response

**Throws:**
- `Error`: If plugin name is invalid, payload is too large, or send fails

**Example:**
```javascript
// Send RGB color data
const rgbData = window.PluginWebUI.encodeRGB(255, 0, 128);
try {
  const response = await window.PluginWebUI.sendPluginData('rgb_effect', rgbData);
  console.log('Data sent:', response);
} catch (error) {
  console.error('Failed to send data:', error.message);
}
```

#### `encodeRGB(r, g, b)`

Encodes RGB color values to Uint8Array[3].

**Parameters:**
- `r` (number): Red value (0-255, integer)
- `g` (number): Green value (0-255, integer)
- `b` (number): Blue value (0-255, integer)

**Returns:**
- `Uint8Array`: Array of 3 bytes [r, g, b]

**Throws:**
- `Error`: If values are out of range or not integers

**Example:**
```javascript
// Encode red color
const redColor = window.PluginWebUI.encodeRGB(255, 0, 0);
// redColor is Uint8Array[3] with values [255, 0, 0]

// Send to plugin
await window.PluginWebUI.sendPluginData('rgb_effect', redColor);
```

#### `encodeUint8(value)`

Encodes a single byte value to Uint8Array[1].

**Parameters:**
- `value` (number): Value (0-255, integer)

**Returns:**
- `Uint8Array`: Array of 1 byte

**Throws:**
- `Error`: If value is out of range or not an integer

**Example:**
```javascript
// Encode brightness value
const brightness = window.PluginWebUI.encodeUint8(128);
// brightness is Uint8Array[1] with value [128]
```

#### `encodeUint16(value)`

Encodes a 16-bit value to Uint8Array[2] in little-endian format.

**Parameters:**
- `value` (number): Value (0-65535, integer)

**Returns:**
- `Uint8Array`: Array of 2 bytes in little-endian format (LSB first)

**Throws:**
- `Error`: If value is out of range or not an integer

**Example:**
```javascript
// Encode 16-bit value
const uint16Value = window.PluginWebUI.encodeUint16(0x1234);
// uint16Value is Uint8Array[2] with values [0x34, 0x12] (little-endian)
```

### Integration Functions: `app.js`

These functions provide higher-level integration with the UI.

#### `loadPluginWebUI(pluginName, feedbackElement)`

Loads plugin UI with feedback handling and UI state management.

**Parameters:**
- `pluginName` (string): Plugin name to load
- `feedbackElement` (HTMLElement, optional): Element to display loading/error feedback

**Returns:**
- `Promise<Object>`: Resolves to bundle object

**Example:**
```javascript
const feedbackEl = document.getElementById('plugin-feedback');
try {
  const bundle = await loadPluginWebUI('rgb_effect', feedbackEl);
  console.log('Plugin UI loaded:', bundle);
} catch (error) {
  console.error('Failed to load plugin UI:', error);
}
```

#### `sendPluginWebUIData(pluginName, data, feedbackElement)`

Sends plugin data with feedback handling.

**Parameters:**
- `pluginName` (string): Plugin name
- `data` (ArrayBuffer|Uint8Array|number[]): Data to send
- `feedbackElement` (HTMLElement, optional): Element to display feedback

**Returns:**
- `Promise<Object>`: Resolves to response

**Example:**
```javascript
const rgbData = window.PluginWebUI.encodeRGB(255, 128, 0);
const feedbackEl = document.getElementById('plugin-feedback');
try {
  const response = await sendPluginWebUIData('rgb_effect', rgbData, feedbackEl);
  console.log('Data sent:', response);
} catch (error) {
  console.error('Failed to send data:', error);
}
```

#### `fetchPluginList()`

Fetches the list of available plugins from the server.

**Returns:**
- `Promise<string[]>`: Resolves to array of plugin names

**Example:**
```javascript
try {
  const plugins = await fetchPluginList();
  console.log('Available plugins:', plugins);
  // plugins = ['rgb_effect', 'sequence', 'beat']
} catch (error) {
  console.error('Failed to fetch plugins:', error);
}
```

#### `populatePluginDropdown(plugins)`

Populates the plugin dropdown with available plugins.

**Parameters:**
- `plugins` (string[]): Array of plugin names

**Example:**
```javascript
const plugins = await fetchPluginList();
populatePluginDropdown(plugins);
// Dropdown will be populated with formatted plugin names
```

#### `initializePluginSelector()`

Initializes the plugin selector dropdown with event handlers.

**Example:**
```javascript
// Usually called after populating dropdown
initializePluginSelector();
// Dropdown change events will now trigger plugin UI loading
```

## Common Use Cases

### Use Case 1: Load Plugin UI

```javascript
// Step 1: Fetch plugin list
const plugins = await fetchPluginList();

// Step 2: Populate dropdown
populatePluginDropdown(plugins);

// Step 3: Initialize selector
initializePluginSelector();

// Step 4: User selects plugin from dropdown
// Plugin UI loads automatically via event handler
```

### Use Case 2: Send RGB Color Data

```javascript
// Encode RGB values
const red = 255;
const green = 0;
const blue = 128;
const rgbData = window.PluginWebUI.encodeRGB(red, green, blue);

// Send to plugin
try {
  await window.PluginWebUI.sendPluginData('rgb_effect', rgbData);
  console.log('Color sent successfully');
} catch (error) {
  console.error('Failed to send color:', error.message);
}
```

### Use Case 3: Send Multiple Values

```javascript
// Encode multiple values into single payload
const values = [
  ...window.PluginWebUI.encodeRGB(255, 0, 0),  // Color
  ...window.PluginWebUI.encodeUint8(128),      // Brightness
  ...window.PluginWebUI.encodeUint16(1000)     // Duration
];

// Combine into single Uint8Array
const payload = new Uint8Array(values);

// Send (must be <= 512 bytes)
if (payload.length <= 512) {
  await window.PluginWebUI.sendPluginData('rgb_effect', payload);
}
```

### Use Case 4: Error Handling

```javascript
async function loadPluginWithErrorHandling(pluginName) {
  try {
    const bundle = await window.PluginWebUI.loadPluginBundle(pluginName);
    return bundle;
  } catch (error) {
    // Handle specific error types
    if (error.message.includes('404')) {
      console.error('Plugin not found:', pluginName);
    } else if (error.message.includes('Network error')) {
      console.error('Network connection failed');
    } else if (error.message.includes('Invalid plugin name')) {
      console.error('Invalid plugin name format');
    } else {
      console.error('Unknown error:', error.message);
    }
    throw error; // Re-throw for caller to handle
  }
}
```

## Error Scenarios and Handling

### Network Errors

**Symptom**: `TypeError: Failed to fetch`

**Cause**: Network connection lost or server unreachable

**Handling**:
```javascript
try {
  await window.PluginWebUI.loadPluginBundle('plugin_name');
} catch (error) {
  if (error instanceof TypeError && error.message.includes('fetch')) {
    // Show user-friendly network error message
    showError('Network error: Please check your connection');
  }
}
```

### HTTP Errors

**Symptom**: HTTP status codes (404, 413, 500, etc.)

**Handling**:
```javascript
try {
  await window.PluginWebUI.loadPluginBundle('plugin_name');
} catch (error) {
  if (error.message.includes('404')) {
    showError('Plugin not found or has no web UI');
  } else if (error.message.includes('413')) {
    showError('Data payload too large (max 512 bytes)');
  } else if (error.message.includes('500')) {
    showError('Server error: Please try again later');
  }
}
```

### Validation Errors

**Symptom**: `Error: Invalid plugin name format` or `Value must be integer 0-255`

**Cause**: Invalid input parameters

**Handling**:
```javascript
// Validate before calling
const pluginName = 'my_plugin';
if (!/^[a-zA-Z0-9_-]+$/.test(pluginName)) {
  showError('Invalid plugin name format');
  return;
}

// Validate RGB values
const r = 255;
if (typeof r !== 'number' || !Number.isInteger(r) || r < 0 || r > 255) {
  showError('RGB value must be integer 0-255');
  return;
}
```

### Payload Size Errors

**Symptom**: `Error: Payload too large (max 512 bytes)`

**Cause**: Data exceeds 512 byte limit

**Handling**:
```javascript
const data = new Uint8Array(600); // Too large
if (data.length > 512) {
  showError('Data payload too large. Maximum size is 512 bytes.');
  return;
}
```

## Troubleshooting

### Plugin list not loading

**Symptoms**: Dropdown shows "Failed to load plugins" or remains empty

**Solutions**:
1. Check network connection
2. Verify `/api/plugins` endpoint is accessible
3. Check browser console for errors
4. Verify server is running and responding

### Plugin UI not loading

**Symptoms**: No UI appears after selecting plugin, error message shown

**Solutions**:
1. Verify plugin has web UI bundle
2. Check `/api/plugin/:name/bundle` endpoint
3. Verify bundle format is valid JSON
4. Check browser console for JavaScript errors
5. Verify plugin name is correct

### Data sending fails

**Symptoms**: Error message when sending data, plugin doesn't receive data

**Solutions**:
1. Check payload size (must be <= 512 bytes)
2. Verify plugin name is correct
3. Check network connection
4. Verify Content-Type header is `application/octet-stream`
5. Check server logs for errors

### UI conflicts

**Symptoms**: Plugin UI breaks existing UI, styles conflict, JavaScript errors

**Solutions**:
1. Verify plugin uses namespaced CSS classes (prefix with plugin name)
2. Verify plugin JavaScript uses namespaced functions
3. Check for duplicate DOM element IDs
4. Verify event listeners are properly namespaced
5. Check browser console for conflicts

## Best Practices

1. **Always validate input**: Check plugin names and data values before sending
2. **Handle errors gracefully**: Show user-friendly error messages
3. **Use feedback elements**: Provide visual feedback during loading/sending
4. **Check payload size**: Ensure data doesn't exceed 512 bytes
5. **Use encoding helpers**: Use `encodeRGB()`, `encodeUint8()`, `encodeUint16()` for proper encoding
6. **Clean up on errors**: Reset UI state if operations fail
7. **Test error scenarios**: Test network failures, invalid data, etc.
8. **Follow naming conventions**: Use plugin name prefixes for CSS classes and JavaScript functions

## API Endpoints

### GET `/api/plugins`

Returns list of available plugins.

**Response:**
```json
{
  "plugins": ["plugin1", "plugin2", "plugin3"]
}
```

### GET `/api/plugin/:pluginName/bundle`

Returns plugin web UI bundle.

**Response:**
```json
{
  "html": "<div>Plugin UI HTML</div>",
  "js": "function plugin_init() {}",
  "css": ".plugin-class { color: red; }"
}
```

### POST `/api/plugin/:pluginName/data`

Sends binary data to plugin.

**Request:**
- Content-Type: `application/octet-stream`
- Body: Raw bytes (max 512 bytes)

**Response:**
```json
{
  "success": true
}
```

## Security Considerations

- **Trusted Code Model**: Plugins are compiled into firmware, so injected HTML/CSS/JS is trusted
- **Input Validation**: Plugin names and data are validated before processing
- **Payload Limits**: 512 byte limit prevents excessive data transmission
- **No eval()**: Uses `new Function()` instead of `eval()` for JavaScript execution (documented security consideration)

## Additional Resources

- Architecture documentation: `lyktparad-espidf/docs/dev-guides/plugin-web-ui-architecture.md`
- End-to-end testing guide: `lyktparad-server/tests/plugin-e2e-testing-guide.md`
- Implementation review: `.cursor/plans/web2/verifications/04-javascript-plugin-communication-implementation-review_20250106.md`
