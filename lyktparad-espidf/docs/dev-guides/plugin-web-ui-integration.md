# Plugin Web UI Integration Guide

**Document Version**: 1.0
**Date**: 2025-01-29
**Status**: Implementation Complete

---

## Introduction

This guide explains how to integrate web UI support into your plugins. The plugin web UI system enables plugins to provide dynamic HTML, CSS, and JavaScript content at runtime, allowing users to interact with plugins through a web interface.

### Benefits

- **Zero-RAM Optimization**: Static content stored in Flash memory uses zero Heap RAM
- **Transparent Proxy**: ESP32 forwards binary data without processing (zero CPU cycles)
- **Dynamic Content**: Plugins can generate content based on current state
- **Backward Compatible**: Existing plugins without web UI continue to work normally

### When to Use Web UI

Use web UI when your plugin needs:
- User interaction (controls, settings, configuration)
- Real-time status display
- Visual feedback
- Complex state management

---

## Quick Start

### Minimal Example

Here's a minimal example that demonstrates the basic integration:

```c
#include "plugin_web_ui.h"

/* HTML content - Flash memory (zero RAM) */
static const char my_plugin_html_content[] =
    "<div class=\"plugin-my-plugin-container\">\n"
    "  <h3>My Plugin</h3>\n"
    "  <button id=\"plugin-my-plugin-button\">Click Me</button>\n"
    "</div>\n";

static const char *my_plugin_html(void) {
    return my_plugin_html_content;  /* Flash pointer */
}

/* JavaScript content - Flash memory (zero RAM) */
static const char my_plugin_js_content[] =
    "(function() {\n"
    "  const button = document.getElementById('plugin-my-plugin-button');\n"
    "  if (button) {\n"
    "    button.addEventListener('click', function() {\n"
    "      console.log('Button clicked');\n"
    "    });\n"
    "  }\n"
    "})();\n";

static const char *my_plugin_js(void) {
    return my_plugin_js_content;  /* Flash pointer */
}

/* Register web UI during plugin initialization */
static void my_plugin_register_web_ui(void) {
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = my_plugin_html,
        .js_callback = my_plugin_js,
        .css_callback = NULL,  /* No CSS needed */
        .dynamic_mask = 0x00   /* All Flash (static) - no bits set */
    };

    esp_err_t err = plugin_register_web_ui("my_plugin", &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE("MY_PLUGIN", "Failed to register web UI: %s", esp_err_to_name(err));
    }
}

/* Call from plugin init callback */
static esp_err_t my_plugin_init(void) {
    my_plugin_register_web_ui();
    return ESP_OK;
}
```

### Flash vs Heap Comparison

**Flash Content (Recommended)**:
```c
/* Zero RAM usage - stored in .rodata section (Flash) */
static const char html_content[] = "<div>...</div>";
static const char *my_plugin_html(void) {
    return html_content;  /* Flash pointer */
}

plugin_web_ui_callbacks_t callbacks = {
    .html_callback = my_plugin_html,
    .dynamic_mask = 0x00  /* All Flash - no bits set */
};
```

**Heap Content (When Dynamic)**:
```c
/* RAM usage = content size - allocated during callback */
static const char *my_plugin_html(void) {
    char *html = NULL;
    int len = asprintf(&html, "<div>Length: %d</div>", get_length());
    if (len < 0 || html == NULL) {
        return NULL;  /* Error - omitted from bundle */
    }
    return html;  /* Heap pointer - system will free() after use */
}

plugin_web_ui_callbacks_t callbacks = {
    .html_callback = my_plugin_html,
    .dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC  /* HTML is Heap - bit 0 set */
};
```

**Memory Usage Comparison**:
- Flash: 100 bytes HTML = 0 bytes RAM (stored in Flash)
- Heap: 100 bytes HTML = 100 bytes RAM (allocated during callback, freed after HTTP response)

---

## API Reference

### `plugin_register_web_ui()`

Registers web UI callbacks for a plugin.

```c
esp_err_t plugin_register_web_ui(const char *name, const plugin_web_ui_callbacks_t *callbacks);
```

**Parameters**:
- `name`: Plugin name (must match registered plugin name)
- `callbacks`: Pointer to callback structure

**Returns**: `ESP_OK` on success, error code on failure

**Error Codes**:
- `ESP_ERR_INVALID_ARG`: Invalid parameters (NULL name or callbacks)
- `ESP_ERR_NOT_FOUND`: Plugin name not found
- `ESP_ERR_NO_MEM`: Memory allocation failed

**Usage**:
```c
plugin_web_ui_callbacks_t callbacks = {
    .html_callback = my_plugin_html,
    .js_callback = my_plugin_js,
    .css_callback = my_plugin_css,
    .dynamic_mask = 0x00  /* All Flash */
};
esp_err_t err = plugin_register_web_ui("my_plugin", &callbacks);
```

### `plugin_web_ui_callbacks_t` Structure

```c
typedef struct {
    plugin_web_content_callback_t html_callback;  /* Optional, may be NULL */
    plugin_web_content_callback_t js_callback;    /* Optional, may be NULL */
    plugin_web_content_callback_t css_callback;   /* Optional, may be NULL */
    uint8_t dynamic_mask;  /* Bit-masked flags for Flash vs Heap */
} plugin_web_ui_callbacks_t;
```

**Callback Signature**:
```c
typedef const char *(*plugin_web_content_callback_t)(void);
```

**Return Values**:
- Non-NULL: Pointer to content string (Flash or Heap)
- NULL: Content unavailable (omitted from bundle)

### Bit-Mask Flags

```c
#define PLUGIN_WEB_HTML_DYNAMIC  (1 << 0)  /* HTML is Heap (dynamic) */
#define PLUGIN_WEB_JS_DYNAMIC    (1 << 1)  /* JavaScript is Heap (dynamic) */
#define PLUGIN_WEB_CSS_DYNAMIC   (1 << 2)  /* CSS is Heap (dynamic) */
```

**Usage Examples**:
- All Flash: `dynamic_mask = 0x00` (no bits set)
- HTML Heap, others Flash: `dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC` (0x01)
- All Heap: `dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC | PLUGIN_WEB_JS_DYNAMIC | PLUGIN_WEB_CSS_DYNAMIC` (0x07)

### Bundle Endpoint

**Endpoint**: `GET /api/plugin/<plugin-name>/bundle`

**Response**: JSON object `{"html": "...", "js": "...", "css": "..."}`

- NULL callbacks are omitted from JSON
- Content is JSON-escaped (quotes, backslashes, newlines)
- Content-Type: `application/json; charset=utf-8`

### Data Endpoint

**Endpoint**: `POST /api/plugin/<plugin-name>/data`

**Request**:
- Content-Type: `application/octet-stream`
- Body: Raw bytes (e.g., `[0xFF, 0x00, 0x00]` for RGB values)

**Response**: `200 OK` on success

**Max Payload**: 512 bytes recommended (not enforced by system)

---

## Flash vs Heap Demo

### Side-by-Side Comparison

**Flash Content (Zero RAM)**:
```c
/* Content stored in .rodata section (Flash memory) */
static const char html_content[] =
    "<div class=\"plugin-example-container\">\n"
    "  <p>Static content</p>\n"
    "</div>\n";

static const char *example_html(void) {
    return html_content;  /* Flash pointer - permanent */
}

plugin_web_ui_callbacks_t callbacks = {
    .html_callback = example_html,
    .dynamic_mask = 0x00  /* is_dynamic = false - Flash */
};
```

**Memory Usage**: 0 bytes RAM (content in Flash)

**Heap Content (Dynamic)**:
```c
/* Content allocated at runtime (Heap memory) */
static const char *example_html(void) {
    uint8_t value = get_current_value();
    char *html = NULL;
    int len = asprintf(&html,
        "<div class=\"plugin-example-container\">\n"
        "  <p>Current value: %d</p>\n"
        "</div>\n",
        value);
    if (len < 0 || html == NULL) {
        return NULL;  /* Error */
    }
    return html;  /* Heap pointer - system will free() after use */
}

plugin_web_ui_callbacks_t callbacks = {
    .html_callback = example_html,
    .dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC  /* is_dynamic = true - Heap */
};
```

**Memory Usage**: Content size in RAM (allocated during callback, freed after HTTP response)

### "Aha!" Moment Explanation

**Zero-RAM UIs**: When you use `static const char*` for content and set `dynamic_mask = 0x00`, the content is stored in Flash memory (`.rodata` section). The ESP32 serves this content directly from Flash via pointer - **zero bytes of Heap RAM are used**. This enables rich UIs on memory-constrained devices like ESP32-C3 (400KB RAM).

**When to Use Each**:
- **Flash**: Use for static content (HTML templates, CSS styles, JavaScript code)
- **Heap**: Use only when content must be generated dynamically (e.g., current state values)

---

## Binary Communication Protocol

### Raw Bytes Encoding in JavaScript

JavaScript encodes data as binary (Uint8Array) before sending to ESP32:

```javascript
// RGB values as 3 bytes: [R, G, B]
const rgbBytes = PluginWebUI.encodeRGB(255, 0, 0);  // Red
PluginWebUI.sendPluginData('rgb_effect', rgbBytes);
```

### C Struct Definition for Receiving Data

Plugins receive raw bytes via `PLUGIN_CMD_DATA` command handler:

```c
static esp_err_t my_plugin_command_handler(uint8_t *data, uint16_t len) {
    if (data == NULL || len < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd = data[0];
    if (cmd == PLUGIN_CMD_DATA) {
        /* Data format: [R:1][G:1][B:1] (3 bytes) */
        if (len < 4) {  /* cmd byte (1) + R (1) + G (1) + B (1) = 4 bytes */
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t r = data[1];
        uint8_t g = data[2];
        uint8_t b = data[3];

        /* Process RGB values */
        plugin_set_rgb(r, g, b);
        return ESP_OK;
    }

    return ESP_OK;
}
```

### Binary Helper Table

**JavaScript Uint8Array → C Struct Mapping**:

| JavaScript Position | C Struct Member | Type | Notes |
|---------------------|-----------------|------|-------|
| `Uint8Array[0]` | `struct.r` | `uint8_t` | Single byte, no endianness |
| `Uint8Array[1]` | `struct.g` | `uint8_t` | Single byte, no endianness |
| `Uint8Array[2]` | `struct.b` | `uint8_t` | Single byte, no endianness |

**Example: RGB Plugin (3 bytes)**:
```c
/* C struct (conceptual - not actually defined) */
struct rgb_data {
    uint8_t r;  /* Uint8Array[0] */
    uint8_t g;  /* Uint8Array[1] */
    uint8_t b;  /* Uint8Array[2] */
};
```

**Endianness Considerations**:
- Single bytes: No endianness concerns
- Multi-byte values: Use little-endian (LSB first) for consistency

**Alignment Considerations**:
- Packed bytes: No alignment concerns (bytes are packed)
- Struct members: Use `__attribute__((packed))` if needed

### Common Integration Bugs and How to Avoid Them

1. **Length Mismatch**: Always validate data length before accessing bytes
   ```c
   if (len < expected_size) {
       return ESP_ERR_INVALID_SIZE;
   }
   ```

2. **Index Off-by-One**: Remember `data[0]` is command byte, data starts at `data[1]`
   ```c
   uint8_t r = data[1];  /* Not data[0] */
   ```

3. **Endianness Issues**: For multi-byte values, use network byte order or little-endian consistently
   ```c
   uint16_t value = data[1] | (data[2] << 8);  /* Little-endian */
   ```

4. **Buffer Overflow**: Always validate length before accessing array elements
   ```c
   if (len < 4) return ESP_ERR_INVALID_SIZE;  /* Before accessing data[3] */
   ```

---

## JavaScript API

### `loadPluginBundle(pluginName)`

Loads HTML/CSS/JS bundle for a plugin and injects into DOM.

```javascript
try {
    const bundle = await PluginWebUI.loadPluginBundle('rgb_effect');
    console.log('Bundle loaded:', bundle);
} catch (error) {
    console.error('Failed to load bundle:', error);
}
```

**Returns**: Promise resolving to bundle object `{html: "...", js: "...", css: "..."}`

**Errors**: Throws on network errors, HTTP errors (404, 500), or parsing errors

### `sendPluginData(pluginName, data)`

Sends raw bytes data to plugin via POST endpoint.

```javascript
// RGB values as 3 bytes
const rgbBytes = PluginWebUI.encodeRGB(255, 0, 0);
try {
    await PluginWebUI.sendPluginData('rgb_effect', rgbBytes);
    console.log('RGB sent successfully');
} catch (error) {
    console.error('Failed to send RGB:', error);
}
```

**Parameters**:
- `pluginName`: String, plugin name
- `data`: `Uint8Array`, `ArrayBuffer`, or number array (0-255)

**Returns**: Promise resolving to success response

**Errors**: Throws on network errors, HTTP errors (400, 404, 500), or validation errors

### Encoding Helpers

**`encodeRGB(r, g, b)`**: Encodes RGB values as `Uint8Array[3]`
```javascript
const rgbBytes = PluginWebUI.encodeRGB(255, 0, 0);  // Red
// Returns: Uint8Array([255, 0, 0])
```

**`encodeUint8(value)`**: Encodes single byte value as `Uint8Array[1]`
```javascript
const byte = PluginWebUI.encodeUint8(128);
// Returns: Uint8Array([128])
```

**`encodeUint16(value)`**: Encodes 16-bit value as `Uint8Array[2]` (little-endian)
```javascript
const word = PluginWebUI.encodeUint16(0x1234);
// Returns: Uint8Array([0x34, 0x12])  // Little-endian
```

---

## Best Practices

### Use Static Content When Possible

**Recommended**: Flash content (zero RAM)
```c
static const char html_content[] = "<div>...</div>";
static const char *my_plugin_html(void) {
    return html_content;  /* Flash pointer */
}
```

**Avoid**: Heap content unless necessary
```c
/* Only use Heap when content must be dynamic */
static const char *my_plugin_html(void) {
    char *html = asprintf(&html, "<div>Value: %d</div>", get_value());
    return html;  /* Heap pointer - uses RAM */
}
```

### Keep Content Size Reasonable

- Recommended: < 10KB per content type (HTML, JS, CSS)
- Large content: Use streaming (handled automatically by system)
- Very large content: Consider splitting into multiple plugins

### Encode Data Efficiently

**Recommended**: Raw bytes (minimal size)
```javascript
const rgbBytes = PluginWebUI.encodeRGB(255, 0, 0);  // 3 bytes
PluginWebUI.sendPluginData('rgb_effect', rgbBytes);
```

**Avoid**: JSON encoding (larger size, requires parsing)
```javascript
// Not recommended - larger payload, requires JSON parsing
const jsonData = JSON.stringify({r: 255, g: 0, b: 0});  // ~20 bytes
```

### Namespace Scoping

**CSS Classes**: Prefix with plugin name
```css
/* Good: Namespaced */
.plugin-rgb-effect-container { ... }
.plugin-rgb-effect-slider { ... }

/* Bad: Global (may conflict) */
.container { ... }
.slider { ... }
```

**JavaScript Functions**: Prefix with plugin name
```javascript
// Good: Namespaced
function plugin_rgb_effect_init() { ... }

// Bad: Global (may conflict)
function init() { ... }
```

---

## Examples

### RGB Effect Plugin Example (Complete Code)

This example demonstrates a complete plugin with web UI support:

```c
#include "plugin_system.h"
#include "plugin_web_ui.h"
#include "plugin_light.h"
#include "mesh_commands.h"

/* HTML content - Flash memory */
static const char rgb_effect_html_content[] =
    "<div class=\"plugin-rgb-effect-container\">\n"
    "  <h3>RGB Effect Control</h3>\n"
    "  <div class=\"plugin-rgb-effect-controls\">\n"
    "    <label for=\"plugin-rgb-effect-red\">Red: <span id=\"plugin-rgb-effect-red-value\">0</span></label>\n"
    "    <input type=\"range\" id=\"plugin-rgb-effect-red\" min=\"0\" max=\"255\" value=\"0\" class=\"plugin-rgb-effect-slider\">\n"
    "    <label for=\"plugin-rgb-effect-green\">Green: <span id=\"plugin-rgb-effect-green-value\">0</span></label>\n"
    "    <input type=\"range\" id=\"plugin-rgb-effect-green\" min=\"0\" max=\"255\" value=\"0\" class=\"plugin-rgb-effect-slider\">\n"
    "    <label for=\"plugin-rgb-effect-blue\">Blue: <span id=\"plugin-rgb-effect-blue-value\">0</span></label>\n"
    "    <input type=\"range\" id=\"plugin-rgb-effect-blue\" min=\"0\" max=\"255\" value=\"0\" class=\"plugin-rgb-effect-slider\">\n"
    "  </div>\n"
    "</div>\n";

static const char *rgb_effect_html(void) {
    return rgb_effect_html_content;
}

/* CSS content - Flash memory */
static const char rgb_effect_css_content[] =
    ".plugin-rgb-effect-container { padding: 20px; background: #f5f5f5; border-radius: 8px; }\n"
    ".plugin-rgb-effect-controls { display: flex; flex-direction: column; gap: 15px; }\n"
    ".plugin-rgb-effect-slider { width: 100%; height: 8px; border-radius: 4px; background: #ddd; }\n";

static const char *rgb_effect_css(void) {
    return rgb_effect_css_content;
}

/* JavaScript content - Flash memory */
static const char rgb_effect_js_content[] =
    "(function() {\n"
    "  'use strict';\n"
    "\n"
    "  // Wait for PluginWebUI to be available\n"
    "  if (typeof PluginWebUI === 'undefined') {\n"
    "    console.error('[RGB Effect] PluginWebUI not available');\n"
    "    return;\n"
    "  }\n"
    "\n"
    "  // Get slider elements\n"
    "  const redSlider = document.getElementById('plugin-rgb-effect-red');\n"
    "  const greenSlider = document.getElementById('plugin-rgb-effect-green');\n"
    "  const blueSlider = document.getElementById('plugin-rgb-effect-blue');\n"
    "  const redValue = document.getElementById('plugin-rgb-effect-red-value');\n"
    "  const greenValue = document.getElementById('plugin-rgb-effect-green-value');\n"
    "  const blueValue = document.getElementById('plugin-rgb-effect-blue-value');\n"
    "\n"
    "  if (!redSlider || !greenSlider || !blueSlider) {\n"
    "    console.error('[RGB Effect] Slider elements not found');\n"
    "    return;\n"
    "  }\n"
    "\n"
    "  // Update value display\n"
    "  function updateValueDisplay() {\n"
    "    if (redValue) redValue.textContent = redSlider.value;\n"
    "    if (greenValue) greenValue.textContent = greenSlider.value;\n"
    "    if (blueValue) blueValue.textContent = blueSlider.value;\n"
    "  }\n"
    "\n"
    "  // Send RGB values to plugin\n"
    "  function sendRGB() {\n"
    "    const r = parseInt(redSlider.value, 10);\n"
    "    const g = parseInt(greenSlider.value, 10);\n"
    "    const b = parseInt(blueSlider.value, 10);\n"
    "\n"
    "    // Encode as 3 bytes: [R, G, B]\n"
    "    const rgbBytes = PluginWebUI.encodeRGB(r, g, b);\n"
    "\n"
    "    // Send to plugin via API\n"
    "    PluginWebUI.sendPluginData('rgb_effect', rgbBytes)\n"
    "      .then(() => {\n"
    "        console.log('[RGB Effect] RGB sent:', r, g, b);\n"
    "      })\n"
    "      .catch((error) => {\n"
    "        console.error('[RGB Effect] Failed to send RGB:', error);\n"
    "      });\n"
    "  }\n"
    "\n"
    "  // Add event listeners\n"
    "  redSlider.addEventListener('input', function() {\n"
    "    updateValueDisplay();\n"
    "    sendRGB();\n"
    "  });\n"
    "\n"
    "  greenSlider.addEventListener('input', function() {\n"
    "    updateValueDisplay();\n"
    "    sendRGB();\n"
    "  });\n"
    "\n"
    "  blueSlider.addEventListener('input', function() {\n"
    "    updateValueDisplay();\n"
    "    sendRGB();\n"
    "  });\n"
    "\n"
    "  // Initialize value displays\n"
    "  updateValueDisplay();\n"
    "\n"
    "  console.log('[RGB Effect] Web UI initialized');\n"
    "})();\n";

static const char *rgb_effect_js(void) {
    return rgb_effect_js_content;
}

/* Command handler - processes PLUGIN_CMD_DATA */
static esp_err_t rgb_effect_command_handler(uint8_t *data, uint16_t len) {
    if (data == NULL || len < 1) {
        ESP_LOGE("RGB_EFFECT", "Invalid command data");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd = data[0];

    /* Handle PLUGIN_CMD_DATA (0x04) - raw bytes RGB values */
    if (cmd == PLUGIN_CMD_DATA) {
        /* Data format: [R:1][G:1][B:1] (3 bytes) */
        if (len < 4) {  /* cmd byte (1) + R (1) + G (1) + B (1) = 4 bytes */
            ESP_LOGE("RGB_EFFECT", "PLUGIN_CMD_DATA: Invalid length (%d, expected 4)", len);
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t r = data[1];
        uint8_t g = data[2];
        uint8_t b = data[3];

        /* Set RGB LED directly */
        plugin_set_rgb(r, g, b);

        ESP_LOGI("RGB_EFFECT", "RGB set via web UI: R=%d G=%d B=%d", r, g, b);
        return ESP_OK;
    }

    /* Other commands handled by dedicated callbacks */
    return ESP_OK;
}

/* Register web UI */
static void rgb_effect_register_web_ui(void) {
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = rgb_effect_html,
        .js_callback = rgb_effect_js,
        .css_callback = rgb_effect_css,
        .dynamic_mask = 0x00  /* All Flash (static) - no bits set */
    };

    esp_err_t err = plugin_register_web_ui("rgb_effect", &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE("RGB_EFFECT", "Failed to register web UI: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("RGB_EFFECT", "Web UI registered for RGB effect plugin");
    }
}

/* Plugin init */
static esp_err_t rgb_effect_init(void) {
    /* No initialization needed - core heartbeat counter handles timing */

    /* Register web UI callbacks */
    rgb_effect_register_web_ui();

    return ESP_OK;
}
```

### Flash Content Example

Complete example using Flash content (zero RAM):

```c
static const char html_content[] = "<div class=\"plugin-example\">...</div>";
static const char js_content[] = "function plugin_example_init() { ... }";
static const char css_content[] = ".plugin-example { ... }";

static const char *example_html(void) { return html_content; }
static const char *example_js(void) { return js_content; }
static const char *example_css(void) { return css_content; }

void example_register_web_ui(void) {
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = example_html,
        .js_callback = example_js,
        .css_callback = example_css,
        .dynamic_mask = 0x00  /* All Flash */
    };
    plugin_register_web_ui("example", &callbacks);
}
```

### Heap Content Example

Example using Heap content (dynamic):

```c
static const char *example_html(void) {
    uint8_t value = get_current_value();
    char *html = NULL;
    asprintf(&html, "<div>Value: %d</div>", value);
    return html;  /* Heap pointer */
}

void example_register_web_ui(void) {
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = example_html,
        .js_callback = NULL,
        .css_callback = NULL,
        .dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC  /* HTML is Heap */
    };
    plugin_register_web_ui("example", &callbacks);
}
```

### Binary Encoding Example

Example of binary data communication:

**JavaScript Side**:
```javascript
// Encode RGB values as 3 bytes
const rgbBytes = PluginWebUI.encodeRGB(255, 0, 0);

// Send to plugin
PluginWebUI.sendPluginData('rgb_effect', rgbBytes);
```

**C Side**:
```c
static esp_err_t rgb_effect_command_handler(uint8_t *data, uint16_t len) {
    if (data[0] == PLUGIN_CMD_DATA && len >= 4) {
        uint8_t r = data[1];  /* Uint8Array[0] */
        uint8_t g = data[2];  /* Uint8Array[1] */
        uint8_t b = data[3];  /* Uint8Array[2] */
        plugin_set_rgb(r, g, b);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}
```

---

## Troubleshooting

### Bundle Not Loading

**Symptoms**: `404 Not Found` when loading bundle

**Solutions**:
1. Verify plugin is registered: Check plugin registration logs
2. Verify web UI is registered: Check `plugin_register_web_ui()` return value
3. Verify plugin name matches: Use exact plugin name (case-sensitive)

### Data Not Reaching Plugin

**Symptoms**: Data sent but plugin doesn't receive it

**Solutions**:
1. Verify `PLUGIN_CMD_DATA` handler: Check command handler processes `cmd == PLUGIN_CMD_DATA`
2. Verify data format: Check binary encoding matches C struct layout
3. Verify data length: Check length validation in command handler
4. Check mesh forwarding: Verify root node is forwarding data correctly

### Memory Issues

**Symptoms**: OOM errors, crashes

**Solutions**:
1. Use Flash content: Prefer `static const char*` with `dynamic_mask = 0x00`
2. Free Heap content: Ensure `dynamic_mask` bits are set for Heap content
3. Reduce content size: Keep HTML/CSS/JS under 10KB each
4. Check memory guard: Verify `esp_ptr_in_drom()` check before free()

---

## References

- [Plugin Web UI Architecture](plugin-web-ui-architecture.md) - Complete architecture documentation
- [Plugin System Developer Guide](plugin-system.md) - Plugin system API reference
- [Design Decisions](plugin-web-ui-design-decisions.md) - Design rationale and trade-offs

---

**Copyright (c) 2025 the_louie**
