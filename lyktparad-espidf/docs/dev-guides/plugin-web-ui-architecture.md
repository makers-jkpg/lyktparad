# Plugin Web UI Architecture

**Document Version**: 1.0
**Date**: 2025-01-29
**Status**: Design Phase
**Focus**: Zero-RAM Optimization & Transparent Proxy Logic

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Core Philosophy](#core-philosophy)
3. [System Overview](#system-overview)
4. [Architecture Components](#architecture-components)
5. [Design Decisions](#design-decisions)
6. [Data Flow](#data-flow)
7. [API Design](#api-design)
8. [Protocol Specifications](#protocol-specifications)
9. [Memory Management](#memory-management)
10. [Security Considerations](#security-considerations)
11. [Integration Points](#integration-points)
12. [Error Handling](#error-handling)
13. [Performance Considerations](#performance-considerations)
14. [Implementation Examples](#implementation-examples)
15. [Implementation Phases](#implementation-phases)

---

## Executive Summary

This document describes the architecture for implementing a plugin web UI system that enables plugins to provide dynamic HTML, CSS, and JavaScript content at runtime. The architecture is designed to operate efficiently on resource-constrained ESP32 hardware (C3, P4) by:

- **Zero-RAM Optimization**: Static content served directly from Flash memory (zero Heap usage)
- **Transparent Proxy**: ESP32 forwards binary data without processing (zero CPU cycles for data forwarding)
- **Browser as Processor**: JavaScript handles all encoding/decoding and UI rendering
- **Streaming JSON**: Chunked Transfer Encoding enables serving large bundles with minimal RAM

The system maintains backward compatibility with existing plugins and supports heterogeneous hardware setups where a powerful ESP32-P4 root node handles web serving while lightweight ESP32-C3 child nodes receive commands.

---

## Core Philosophy

### MCU as a Router

The ESP32 acts as a transparent router, not a processor:

- **Routes binary payloads**: ESP32 forwards raw bytes from HTTP requests to mesh commands without understanding content
- **Streams static strings**: Content from Flash is served directly via pointer (zero-copy)
- **Zero processing**: No JSON parsing, no data transformation, no encoding/decoding on ESP32
- **Header insertion only**: Minimal CPU work (memcpy and header insertion)

**Rationale**: Minimizes CPU usage on resource-constrained hardware and enables heterogeneous setups where powerful root nodes handle web serving while lightweight child nodes execute commands.

### Browser as a Processor

The browser handles all computational and rendering work:

- **UI Rendering**: Browser injects HTML/CSS/JS into DOM via `plugin-web-ui.js` module
- **Data Encoding**: JavaScript handles all encoding (JSON, binary) before sending to ESP32
- **State Management**: Browser manages complex UI state and interactions
- **Decoding**: JavaScript decodes responses and updates UI accordingly

**Rationale**: Leverages browser capabilities (native JSON, DOM manipulation, large memory) to offload work from resource-constrained ESP32, enabling rich UIs without taxing the embedded device.

---

## System Overview

### Problem Statement

Currently, plugins cannot provide web UI content dynamically. Plugin-specific HTML/CSS/JS files are only served by the external webserver (Node.js) from static files copied at build time. The embedded webserver (ESP32 root node) cannot serve plugin-specific web content dynamically, and there's no standardized way for plugins to communicate with the web UI via API endpoints.

### Solution Overview

The Plugin Web UI Integration system provides:

1. **Dynamic Content Serving**: Plugins register callbacks that return HTML, CSS, or JavaScript content on-demand
2. **API Communication**: Standardized endpoints for web UI to send data to plugins
3. **Mesh Distribution**: Efficient forwarding of plugin data to all mesh nodes
4. **Dual Webserver Support**: Works with both embedded (ESP32) and external (Node.js) webservers
5. **Backward Compatibility**: Existing plugins without web UI support continue to work normally

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         Web UI (Browser)                        │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  JavaScript Module (plugin-web-ui.js)                    │  │
│  │  - fetchPluginBundle(pluginName)                         │  │
│  │  - sendPluginData(pluginName, data)                      │  │
│  │  - Encoding/Decoding (JSON, Binary)                      │  │
│  │  - DOM Injection (HTML/CSS/JS)                           │  │
│  └──────────────────────────────────────────────────────────┘  │
└────────────────────┬───────────────────────┬────────────────────┘
                     │                       │
         ┌───────────▼──────────┐  ┌────────▼──────────┐
         │  External Webserver  │  │ Embedded Webserver │
         │     (Node.js)        │  │     (ESP32)        │
         │                      │  │                    │
         │  Proxy Routes:       │  │  HTTP Endpoints:   │
         │  - GET /api/plugin/  │  │  - GET /api/plugin/│
         │    <name>/bundle     │  │    <name>/bundle   │
         │  - POST /api/plugin/ │  │  - POST /api/plugin│
         │    <name>/data       │  │    /<name>/data    │
         │                      │  │                    │
         │  UDP Bridge ────────┼──┼─► UDP Protocol     │
         └──────────────────────┘  └────────┬───────────┘
                                             │
                                    ┌────────▼──────────┐
                                    │  Plugin Web UI    │
                                    │  Registration      │
                                    │  System           │
                                    │  - plugin_web_ui.c│
                                    │  - Callback Storage│
                                    └────────┬───────────┘
                                             │
                                    ┌────────▼──────────┐
                                    │  Plugin System    │
                                    │  - plugin_system.c│
                                    │  - Plugin Info    │
                                    └────────┬───────────┘
                                             │
                                    ┌────────▼──────────┐
                                    │  Root Node        │
                                    │  - mesh_root.c    │
                                    │  - Mesh Forwarding│
                                    └────────┬───────────┘
                                             │
                                    ┌────────▼──────────┐
                                    │  Mesh Network     │
                                    │  PLUGIN_CMD_DATA  │
                                    └────────────────────┘
```

---

## Architecture Components

### 1. Plugin Web UI Registration System

**Purpose**: Provides infrastructure for plugins to register web UI callbacks and retrieve content dynamically.

**Components**:
- `plugin_web_ui.h`: Header file with callback type definitions and API
- `plugin_web_ui.c`: Implementation of registration and retrieval system
- Integration with `plugin_system.c`: Stores callbacks alongside plugin info

**Key Functions**:
- `plugin_register_web_ui(const char *name, const plugin_web_ui_callbacks_t *callbacks)`: Register callbacks for a plugin
- `plugin_get_web_bundle(const char *name, char *json_buffer, size_t buffer_size)`: Build JSON bundle (buffer-based, for small content)
- Streaming bundle building via HTTP handler (for large content, uses `httpd_resp_send_chunk()`)

**Design Decision**: Callbacks are stored as optional pointers in plugin info structure to maintain backward compatibility.

**Rationale**:
- Existing plugins continue to work without modification
- Minimal memory overhead (one pointer per plugin)
- Easy to check if plugin has web UI support (NULL check)

### 2. HTTP Endpoints (Embedded Webserver)

**Purpose**: Serve plugin-provided content and handle plugin API communication.

**Endpoints**:
- `GET /api/plugin/<plugin-name>/bundle`: Serve HTML, JS, and CSS as JSON bundle
- `POST /api/plugin/<plugin-name>/data`: Accept plugin data from web UI (raw bytes)

**Implementation**: `mesh_web.c` with ESP-HTTP-Server handlers

**Design Decision**: Single bundle endpoint returning JSON instead of three separate endpoints.

**Rationale**:
- Reduces HTTP connections from 3 to 1 per plugin load
- Minimizes TCP handshaking overhead
- Reduces concurrent connection limits on ESP32
- Enables atomic loading of all plugin UI components

### 3. Root Node Mesh Forwarding

**Purpose**: Forward plugin data from web UI to all mesh nodes as PLUGIN_CMD_DATA commands.

**Implementation**: `mesh_root.c` with `plugin_forward_data_to_mesh()` function

**Command Format**: `[PLUGIN_ID:1] [PLUGIN_CMD_DATA:1] [RAW_DATA:N]`

**Design Decision**: Root node does not process PLUGIN_CMD_DATA commands it receives via mesh (it already has the data).

**Rationale**:
- Prevents root node from processing its own broadcasts
- Reduces unnecessary processing
- Matches existing plugin command handling pattern

### 4. JavaScript Utilities

**Purpose**: Provide JavaScript functions for web UI to interact with plugin system.

**Module**: `plugin-web-ui.js` (to be implemented in external webserver or embedded HTML)

**Key Functions**:
- `fetchPluginBundle(pluginName)`: Fetch HTML/CSS/JS content as JSON bundle
- `sendPluginData(pluginName, data)`: Send data to plugin via POST (raw bytes)

**Design Decision**: JavaScript handles all encoding/decoding (JSON.stringify/parse, binary encoding).

**Rationale**:
- Minimizes backend processing on resource-constrained ESP32
- Leverages JavaScript's native JSON support
- Reduces memory usage on embedded device
- Faster processing in browser environment

### 5. External Webserver Proxy

**Purpose**: Route plugin web UI requests from external webserver to embedded webserver.

**Implementation**: `lyktparad-server/routes/proxy.js` with Express route handlers

**Routes**:
- `GET /api/plugin/:pluginName/bundle`
- `POST /api/plugin/:pluginName/data`

**Design Decision**: Proxy routes use existing UDP bridge protocol for communication.

**Rationale**:
- Reuses existing infrastructure
- Consistent with other API proxying
- No need for new communication protocol

---

## Design Decisions

### Callback Interface Design

**Decision**: Unified callback signature with no parameters

```c
typedef const char *(*plugin_web_content_callback_t)(void);
```

**Rationale**:
- Simplifies callback interface (no format parameter needed)
- Format determined by callback assignment (html_callback, js_callback, css_callback)
- Consistent with ESP-IDF callback patterns
- Easy to implement and understand

**Alternative Considered**: Callback with format parameter `(const char *format)` - rejected because format is redundant (determined by which callback field is used).

### Memory Management: Bit-Masked Flags

**Decision**: Single `uint8_t dynamic_mask` with bit definitions

```c
#define PLUGIN_WEB_HTML_DYNAMIC  (1 << 0)
#define PLUGIN_WEB_JS_DYNAMIC    (1 << 1)
#define PLUGIN_WEB_CSS_DYNAMIC   (1 << 2)
```

**Rationale**:
- Minimizes registry footprint (1 byte vs 3 booleans)
- Efficient bitwise operations for flag checking
- Memory guard: `esp_ptr_in_drom()` safety check before free()

**Alternative Considered**: Three separate boolean fields - rejected due to memory overhead (3 bytes vs 1 byte).

### Bundle Endpoint: Streaming vs Buffer

**Decision**: Streaming JSON builder using Chunked Transfer Encoding for large content, buffer-based for small content

**Rationale**:
- Streaming enables serving 10KB bundle using only a few hundred bytes of RAM
- Avoids OOM on C3 with limited RAM
- Buffer-based approach simpler for small content (< 1KB)
- Fallback to buffer if streaming not feasible

**Alternative Considered**: Always use buffer - rejected due to OOM risk on C3.

### Transparent Proxy Design

**Decision**: ESP32 forwards raw bytes without any processing

**Rationale**:
- Zero CPU cycles for data forwarding (only header insertion and memcpy)
- Enables heterogeneous hardware setups (P4 root, C3 children)
- Simplifies implementation (no encoding/decoding logic)
- JavaScript already handles encoding

**Alternative Considered**: ESP32 processes and validates data - rejected due to CPU cost and complexity.

---

## Data Flow

### Bundle Request Flow

```
Browser → GET /api/plugin/<name>/bundle
    ↓
Embedded Webserver (mesh_web.c)
    ↓
Plugin Web UI System (plugin_web_ui.c)
    ↓
Plugin Callbacks (html_callback, js_callback, css_callback)
    ↓
JSON Bundle Building (streaming or buffer)
    ↓
HTTP Response (Chunked Transfer Encoding or single response)
    ↓
Browser → Parse JSON → Inject HTML/CSS/JS into DOM
```

### Data Request Flow

```
Browser → POST /api/plugin/<name>/data (raw bytes)
    ↓
Embedded Webserver (mesh_web.c)
    ↓
Plugin Name Validation (regex: ^[a-zA-Z0-9_-]+$)
    ↓
Plugin Lookup (plugin_get_by_name)
    ↓
Payload Validation (max 512 bytes)
    ↓
Root Node Mesh Forwarding (mesh_root.c)
    ↓
Command Construction: [PLUGIN_ID:1] [PLUGIN_CMD_DATA:1] [RAW_DATA:N]
    ↓
Mesh Broadcast (all child nodes)
    ↓
Child Nodes → Plugin command_handler (processes raw bytes)
```

---

## API Design

### Bundle Endpoint

**Endpoint**: `GET /api/plugin/<plugin-name>/bundle`

**Request**:
- Method: GET
- Path: `/api/plugin/<plugin-name>/bundle`
- Headers: None required

**Response**:
- Status: 200 OK (success), 404 Not Found (plugin not found or no web UI), 500 Internal Server Error (JSON building failure)
- Content-Type: `application/json; charset=utf-8`
- Body: JSON object `{"html": "...", "js": "...", "css": "..."}` (NULL callbacks omitted)

**Example**:
```json
{
  "html": "<div class=\"plugin-rgb-slider\">...</div>",
  "js": "function plugin_rgb_init() { ... }",
  "css": ".plugin-rgb-slider { ... }"
}
```

### Data Endpoint

**Endpoint**: `POST /api/plugin/<plugin-name>/data`

**Request**:
- Method: POST
- Path: `/api/plugin/<plugin-name>/data`
- Content-Type: `application/octet-stream`
- Body: Raw bytes (e.g., `[0xFF, 0x00, 0x00]` for RGB values)

**Response**:
- Status: 200 OK (success), 400 Bad Request (invalid plugin name or oversized payload), 404 Not Found (plugin not found), 500 Internal Server Error (forwarding failure)
- Body: Empty or status JSON

**Example Request**:
```
POST /api/plugin/rgb_effect/data
Content-Type: application/octet-stream
Content-Length: 3

[0xFF, 0x00, 0x00]
```

---

## Protocol Specifications

### Callback Interface Protocol

**Callback Signature**:
```c
typedef const char *(*plugin_web_content_callback_t)(void);
```

**Return Value**:
- Non-NULL: Pointer to content string (Flash or Heap)
- NULL: Error or content unavailable (omitted from bundle)

**Lifetime Requirements**:
- Flash content: Permanent (pointer valid for lifetime of program)
- Heap content: Valid during callback invocation, freed after HTTP response sent

**Callback Assignment**:
- `html_callback`: Returns HTML content (may be NULL)
- `js_callback`: Returns JavaScript content (may be NULL)
- `css_callback`: Returns CSS content (may be NULL)

**Memory Management Flags**:
- `PLUGIN_WEB_HTML_DYNAMIC (1 << 0)`: HTML is Heap (dynamic)
- `PLUGIN_WEB_JS_DYNAMIC (1 << 1)`: JS is Heap (dynamic)
- `PLUGIN_WEB_CSS_DYNAMIC (1 << 2)`: CSS is Heap (dynamic)
- Bit set (1) = Heap (must free after use)
- Bit clear (0) = Flash (permanent, don't free)

### Bundle JSON Protocol

**Format**: `{"html": "...", "js": "...", "css": "..."}`

**Rules**:
- NULL callbacks are omitted (field not included in JSON)
- Empty content: Include field with empty string `""`
- Content is JSON-escaped (quotes, backslashes, newlines)
- Carriage returns are stripped

**Escaping**:
- `"` → `\"`
- `\` → `\\`
- `\n` → `\n`
- `\r` → (stripped)

### Data Forwarding Protocol

**Mesh Command Format**: `[PLUGIN_ID:1] [PLUGIN_CMD_DATA:1] [RAW_DATA:N]`

**Construction**:
1. Read HTTP request body (raw bytes) via `httpd_req_recv()`
2. Get plugin ID from plugin name using `plugin_get_id_by_name()`
3. Get mesh transmit buffer via `mesh_common_get_tx_buf()`
4. Insert PLUGIN_ID at buffer[0]
5. Insert PLUGIN_CMD_DATA (0x04) at buffer[1]
6. Copy raw bytes to buffer[2+] using `memcpy()` (zero processing)
7. Broadcast to all child nodes via `mesh_send_with_bridge()`

**Max Payload**: 512 bytes (recommended, not enforced by system)
- Leaves room for PLUGIN_ID (1 byte) + PLUGIN_CMD_DATA (1 byte) within 1024 byte total mesh command limit
- Total: 512 + 1 + 1 = 514 bytes < 1024 bytes
- Early validation: Reject requests exceeding 512 bytes with 400 Bad Request

**Transparent Proxy Guarantee**:
- Zero CPU processing: Only header insertion and memcpy
- No encoding/decoding: Raw bytes forwarded as-is
- No validation: Plugins validate in command_handler
- No transformation: Data format is plugin-specific

---

## Memory Management

### Flash vs Heap Strategy

**Flash Content (Static)**:
- Stored in `.rodata` section (Flash memory)
- Zero RAM usage (direct pointer serving)
- Lifetime: Permanent (no management needed)
- Detection: `esp_ptr_in_drom()` macro
- Flag: Bit clear (0) in `dynamic_mask`

**Heap Content (Dynamic)**:
- Allocated at runtime via `malloc()`, `asprintf()`, or `snprintf()`
- RAM usage: Content size (allocated during callback)
- Lifetime: Valid during callback invocation, freed immediately after HTTP response sent
- Detection: Not in Flash (verified before free())
- Flag: Bit set (1) in `dynamic_mask`

### Memory Guard

**Safety Check**: `esp_ptr_in_drom()` before free()

**Purpose**: Prevents attempting to free() Flash pointers if developer accidentally sets dynamic bit for static string

**Implementation**:
```c
if (cb->dynamic_mask & components[i].flag) {
    if (esp_ptr_in_drom(raw_content)) {
        ESP_LOGW(TAG, "Warning: %s marked dynamic but pointer in Flash. Skipping free.", components[i].key);
    } else {
        free((void *)raw_content);
    }
}
```

### Content Lifetime Management

**Flash Content**:
- Lifetime: Permanent (stored in Flash, never freed)
- No management needed
- Pointer remains valid for program lifetime

**Heap Content**:
- Lifetime: Callback invocation duration
- Freed immediately after `httpd_resp_send_chunk()` completes
- Must not be accessed after HTTP response sent

### Memory Constraints

**ESP32 Memory Limitations**:
- C3: ~400KB RAM, ~4MB Flash
- P4: ~512KB RAM, ~16MB Flash

**Content Size Recommendations**:
- Flash content: No practical limit (Flash is large)
- Heap content: Minimize usage, prefer Flash when possible
- Bundle size: < 10KB recommended (streaming handles larger)

**Buffer Size Limits**:
- Streaming: Small buffers (few hundred bytes) for JSON chunks
- Buffer-based: Max 2KB recommended for single buffer (prevents OOM on C3)

---

## Security Considerations

### Plugin Name Validation

**Validation Regex**: `^[a-zA-Z0-9_-]+$`

**Purpose**: Prevents path traversal and command injection attacks

**Implementation**:
- Early validation (before any processing)
- 400 Bad Request for invalid names
- Strict character set (alphanumeric, underscore, hyphen only)

### Namespace Scoping

**CSS Namespace Scoping**:
- Plugins must prefix all CSS classes with plugin name
- Example: `.plugin-rgb-slider` instead of `.slider`
- Purpose: Prevents style bleeding into main dashboard

**JavaScript Namespace Scoping**:
- Plugins must prefix all JavaScript functions with plugin name
- Example: `plugin_rgb_init()` instead of `init()`
- Purpose: Prevents function name conflicts

**Enforcement**: Documentation requirement, not runtime validation (plugins are trusted code)

### Trusted Code Model

**Assumption**: Plugins are compiled into firmware, considered trusted

**Implications**:
- No runtime content validation needed
- No sanitization of HTML/CSS/JS content
- JavaScript content injected directly into DOM
- Requires firmware review process for security

**Alternative Considered**: Untrusted content model with validation - rejected due to complexity and performance cost.

### Error Handling Security

**Error Messages**:
- No sensitive information exposed to client
- Generic error messages (e.g., "Plugin not found" not "Plugin 'xyz' not found in registry")
- Detailed errors logged server-side only

**Error Response Format**:
- Generic messages prevent information disclosure
- No stack traces or internal state exposed

---

## Integration Points

### Plugin System Integration

**Extension Point**: `plugin_info_t` structure

**Storage**: `plugin_info_t.web_ui` pointer (allocated via malloc during registration)

**Lookup**: Use existing `plugin_get_by_name()` function

**Backward Compatibility**: NULL web_ui pointer indicates no web UI support

**Registration Flow**:
1. Plugin calls `plugin_register_web_ui()` during plugin init
2. System looks up plugin by name using `plugin_get_by_name()`
3. Allocates `plugin_web_ui_callbacks_t` structure via malloc
4. Stores callbacks and dynamic_mask in plugin_info_t.web_ui pointer
5. Plugin continues normal operation (web UI is optional)

### Web Server Integration

**HTTP Handlers**: Register in `mesh_web_server_start()` function

**URL Patterns**: `/api/plugin/<plugin-name>/bundle` and `/api/plugin/<plugin-name>/data`

**Handler Registration**:
- Register handlers with base pattern (ESP-IDF may support prefix matching)
- Or use catch-all handler and parse URI manually
- Manual URI parsing required to extract plugin name

**Handler Implementation**:
- Extract plugin name from URI using string operations
- Validate plugin name (regex: `^[a-zA-Z0-9_-]+$`)
- Look up plugin and route to appropriate handler
- Follow existing handler patterns for error handling

### Mesh Forwarding Integration

**Function**: `plugin_forward_data_to_mesh()` in `mesh_root.c`

**Integration**: Called from HTTP handler after payload validation

**Command Construction**: Uses existing `mesh_common_get_tx_buf()` and `mesh_send_with_bridge()`

**Forwarding Flow**:
1. Get plugin ID from plugin name
2. Get mesh transmit buffer (shared buffer, thread-safe)
3. Construct command: [PLUGIN_ID] [PLUGIN_CMD_DATA] [RAW_DATA]
4. Get routing table
5. Broadcast to all child nodes (skip root, index 0)
6. Return success/failure status

### External Webserver Integration

**Proxy Routes**: Add to `lyktparad-server/routes/proxy.js`

**UDP Bridge**: Use existing UDP bridge protocol for communication

**Route Patterns**: Match embedded webserver patterns exactly

---

## Error Handling

### HTTP Status Codes

**200 OK**: Success (bundle returned or data forwarded)

**400 Bad Request**: Invalid plugin name or oversized payload

**404 Not Found**: Plugin not found or plugin has no web UI

**500 Internal Server Error**: JSON building failure, callback failure, or mesh forwarding failure

### Error Response Format

**Bundle Endpoint Errors**:
- 404: Empty response or generic JSON `{"error": "Plugin not found"}`
- 500: Generic JSON `{"error": "Internal server error"}`

**Data Endpoint Errors**:
- 400: Generic JSON `{"error": "Invalid request"}`
- 404: Generic JSON `{"error": "Plugin not found"}`
- 500: Generic JSON `{"error": "Internal server error"}`

### Error Logging

**Server-Side Logging**:
- Detailed errors logged with ESP_LOG functions
- Include plugin name, error codes, and context
- No sensitive information in logs (if applicable)

**Client-Side Handling**:
- JavaScript handles error responses
- Displays user-friendly error messages
- Retry logic for transient failures (optional)

---

## Performance Considerations

### Streaming JSON Performance

**Chunked Transfer Encoding Benefits**:
- Serves 10KB bundle using only a few hundred bytes of RAM
- Avoids OOM on C3 with limited RAM
- Enables serving large plugin UIs without memory constraints

**Streaming Sequence**:
1. Send `{`
2. Send `"html":"`
3. Stream escaped HTML content (chunk by chunk)
4. Send `"`
5. Send `,`
6. Repeat for js and css
7. Send `}`

### Memory Usage Optimization

**Flash Content**: Zero RAM usage (direct pointer serving)

**Heap Content**: Minimal RAM usage (freed immediately after use)

**Streaming**: Small buffers (few hundred bytes) instead of large buffers (10KB+)

### CPU Usage Minimization

**Transparent Proxy**: Zero CPU cycles for data forwarding (only header insertion and memcpy)

**Streaming JSON**: On-the-fly escaping (no large string building)

**Callback Invocation**: Direct function calls (no overhead)

### Scalability Considerations

**Heterogeneous Hardware Support**:
- ESP32-P4 root node: Handles high-concurrency web server and UI bundling
- ESP32-C3 child nodes: Receive and execute lightweight binary commands

**Hardware Scalability**:
- Powerful P4 handles web server (multiple concurrent connections)
- Lightweight C3 receives commands (minimal processing)

**Network Scalability**:
- Mesh broadcast to all child nodes (efficient distribution)
- Single bundle request reduces HTTP overhead

---

## Implementation Examples

### Example: Plugin Web UI Registration (Flash Content)

```c
// Flash content example - all content in .rodata section
static const char rgb_plugin_html_content[] =
    "<div class=\"plugin-rgb-slider\">"
    "  <input type=\"range\" id=\"plugin-rgb-red\" min=\"0\" max=\"255\">"
    "  <input type=\"range\" id=\"plugin-rgb-green\" min=\"0\" max=\"255\">"
    "  <input type=\"range\" id=\"plugin-rgb-blue\" min=\"0\" max=\"255\">"
    "</div>";

static const char *rgb_plugin_html(void) {
    return rgb_plugin_html_content;  // Pointer to Flash (permanent)
}

static const char rgb_plugin_js_content[] =
    "function plugin_rgb_init() {"
    "  document.getElementById('plugin-rgb-red').addEventListener('input', function(e) {"
    "    sendPluginData('rgb_effect', new Uint8Array([parseInt(e.target.value), 0, 0]));"
    "  });"
    "}";

static const char *rgb_plugin_js(void) {
    return rgb_plugin_js_content;  // Pointer to Flash (permanent)
}

static const char rgb_plugin_css_content[] =
    ".plugin-rgb-slider {"
    "  display: flex;"
    "  flex-direction: column;"
    "  gap: 10px;"
    "}";

static const char *rgb_plugin_css(void) {
    return rgb_plugin_css_content;  // Pointer to Flash (permanent)
}

void rgb_plugin_register_web_ui(void) {
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = rgb_plugin_html,
        .js_callback = rgb_plugin_js,
        .css_callback = rgb_plugin_css,
        .dynamic_mask = 0x00  // All Flash (static) - no bits set
    };

    esp_err_t err = plugin_register_web_ui("rgb_effect", &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE("RGB_PLUGIN", "Failed to register web UI: %s", esp_err_to_name(err));
    }
}
```

### Example: Heap Content Callback (Dynamic Content)

```c
// Dynamic content example - content generated at runtime
static const char *sequence_plugin_html(void) {
    // Get current sequence state
    uint16_t sequence_length = sequence_get_length();
    bool is_active = sequence_is_active();

    // Allocate HTML string on Heap
    char *html = NULL;
    int len = asprintf(&html,
        "<div class=\"plugin-sequence-status\">"
        "  <p>Sequence length: %d</p>"
        "  <p>Status: %s</p>"
        "</div>",
        sequence_length,
        is_active ? "Active" : "Inactive");

    if (len < 0 || html == NULL) {
        ESP_LOGE("SEQUENCE_PLUGIN", "Failed to allocate HTML string");
        return NULL;  // Error - omitted from bundle
    }

    return html;  // Caller must free (handled by system if dynamic_mask bit set)
}

void sequence_plugin_register_web_ui(void) {
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = sequence_plugin_html,
        .js_callback = NULL,   // No JS
        .css_callback = NULL,   // No CSS
        .dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC  // HTML is Heap (dynamic) - bit 0 set
    };

    esp_err_t err = plugin_register_web_ui("sequence", &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE("SEQUENCE_PLUGIN", "Failed to register web UI: %s", esp_err_to_name(err));
    }
}
```

### Example: Mixed Flash/Heap Content

```c
// Mixed content example - some Flash, some Heap
static const char effect_plugin_css_content[] =
    ".plugin-effect-container { padding: 20px; }";

static const char *effect_plugin_css(void) {
    return effect_plugin_css_content;  // Flash (static)
}

static const char *effect_plugin_html(void) {
    // Dynamic HTML based on current effect parameters
    uint8_t speed = effect_get_speed();
    char *html = NULL;
    asprintf(&html, "<div class=\"plugin-effect-container\">Speed: %d</div>", speed);
    return html;  // Heap (dynamic)
}

void effect_plugin_register_web_ui(void) {
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = effect_plugin_html,
        .js_callback = NULL,
        .css_callback = effect_plugin_css,
        .dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC  // Only HTML is dynamic (bit 0 set)
        // CSS is Flash (bit 2 clear = 0)
    };

    plugin_register_web_ui("effect", &callbacks);
}
```

### Example: URL Parsing and Plugin Name Extraction

```c
// Helper function to extract plugin name from URL
// URL format: /api/plugin/<plugin-name>/bundle or /api/plugin/<plugin-name>/data
static esp_err_t extract_plugin_name_from_url(const char *uri, char *plugin_name, size_t name_size) {
    const char *prefix = "/api/plugin/";
    const char *suffix_bundle = "/bundle";
    const char *suffix_data = "/data";

    // Check if URI starts with prefix
    if (strncmp(uri, prefix, strlen(prefix)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Find start of plugin name
    const char *name_start = uri + strlen(prefix);

    // Find end of plugin name (either /bundle or /data)
    const char *name_end = NULL;
    if (strstr(name_start, suffix_bundle) != NULL) {
        name_end = strstr(name_start, suffix_bundle);
    } else if (strstr(name_start, suffix_data) != NULL) {
        name_end = strstr(name_start, suffix_data);
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate name length
    size_t name_len = name_end - name_start;
    if (name_len == 0 || name_len >= name_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Copy plugin name
    strncpy(plugin_name, name_start, name_len);
    plugin_name[name_len] = '\0';

    return ESP_OK;
}

// Helper function to validate plugin name
static bool is_valid_plugin_name(const char *name) {
    // Regex: ^[a-zA-Z0-9_-]+$
    if (name == NULL || strlen(name) == 0) {
        return false;
    }

    for (const char *p = name; *p != '\0'; p++) {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-')) {
            return false;
        }
    }

    return true;
}
```

### Example: HTTP Handler for Bundle Endpoint (Streaming)

```c
static esp_err_t api_plugin_bundle_handler(httpd_req_t *req) {
    // Extract plugin name from URL: /api/plugin/<name>/bundle
    char plugin_name[32];
    if (extract_plugin_name_from_url(req->uri, plugin_name, sizeof(plugin_name)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, NULL, 0);
    }

    // Validate plugin name
    if (!is_valid_plugin_name(plugin_name)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, NULL, 0);
    }

    // Check if plugin has web UI
    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin == NULL || plugin->web_ui == NULL) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, NULL, 0);
    }

    // Set Content-Type and enable chunked transfer
    httpd_resp_set_type(req, "application/json; charset=utf-8");

    // Build and send JSON bundle (streaming)
    plugin_web_ui_callbacks_t *cb = (plugin_web_ui_callbacks_t *)plugin->web_ui;

    // Send opening brace
    httpd_resp_send_chunk(req, "{", 1);

    bool first = true;
    const struct {
        const char *key;
        plugin_web_content_callback_t func;
        uint8_t flag;
    } components[] = {
        {"html", cb->html_callback, PLUGIN_WEB_HTML_DYNAMIC},
        {"js",   cb->js_callback,   PLUGIN_WEB_JS_DYNAMIC},
        {"css",  cb->css_callback,  PLUGIN_WEB_CSS_DYNAMIC}
    };

    for (int i = 0; i < 3; i++) {
        if (components[i].func) {
            const char *content = components[i].func();
            if (!content) continue;

            // Add comma separator
            if (!first) {
                httpd_resp_send_chunk(req, ",", 1);
            }

            // Send key and opening quote
            char key_buf[32];
            int key_len = snprintf(key_buf, sizeof(key_buf), "\"%s\":\"", components[i].key);
            httpd_resp_send_chunk(req, key_buf, key_len);

            // Stream escaped content (chunk by chunk)
            // ... (streaming JSON escape implementation)

            // Send closing quote
            httpd_resp_send_chunk(req, "\"", 1);

            // Free dynamic content if needed
            if (cb->dynamic_mask & components[i].flag) {
                if (!esp_ptr_in_drom(content)) {
                    free((void *)content);
                }
            }

            first = false;
        }
    }

    // Send closing brace and finalize
    httpd_resp_send_chunk(req, "}", 1);
    httpd_resp_send_chunk(req, NULL, 0);  // Signal end

    return ESP_OK;
}
```

### Example: HTTP Handler for Data Endpoint (Transparent Proxy)

```c
static esp_err_t api_plugin_data_handler(httpd_req_t *req) {
    // Extract plugin name from URL
    char plugin_name[32];
    if (extract_plugin_name_from_url(req->uri, plugin_name, sizeof(plugin_name)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, NULL, 0);
    }

    // Validate plugin name
    if (!is_valid_plugin_name(plugin_name)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, NULL, 0);
    }

    // Get plugin ID
    uint8_t plugin_id;
    if (plugin_get_id_by_name(plugin_name, &plugin_id) != ESP_OK) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, NULL, 0);
    }

    // Read payload (max 512 bytes recommended)
    uint8_t payload[512];
    int received = httpd_req_recv(req, (char *)payload, sizeof(payload));
    if (received < 0) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, NULL, 0);
    }

    // Validate payload size
    if (received > 512) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, NULL, 0);
    }

    // Forward to mesh (transparent proxy - zero CPU processing)
    if (plugin_forward_data_to_mesh(plugin_name, payload, received) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, NULL, 0);
    }

    httpd_resp_set_status(req, "200 OK");
    return httpd_resp_send(req, NULL, 0);
}
```

### Example: Mesh Forwarding Function

```c
// In mesh_root.c
esp_err_t plugin_forward_data_to_mesh(const char *plugin_name, uint8_t *data, uint16_t len) {
    // Only root node can forward
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get plugin ID
    uint8_t plugin_id;
    if (plugin_get_id_by_name(plugin_name, &plugin_id) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    // Validate total size (PLUGIN_ID + PLUGIN_CMD_DATA + data)
    if (len > 512 || (2 + len) > 1024) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Get mesh transmit buffer
    uint8_t *tx_buf = mesh_common_get_tx_buf();

    // Construct command: [PLUGIN_ID:1] [PLUGIN_CMD_DATA:1] [RAW_DATA:N]
    tx_buf[0] = plugin_id;           // Plugin ID
    tx_buf[1] = PLUGIN_CMD_DATA;    // Command byte (0x04)
    memcpy(&tx_buf[2], data, len);  // Copy raw bytes (zero processing)

    // Prepare mesh data
    mesh_data_t mesh_data;
    mesh_data.data = tx_buf;
    mesh_data.size = 2 + len;  // PLUGIN_ID + CMD + data
    mesh_data.proto = MESH_PROTO_BIN;
    mesh_data.tos = MESH_TOS_P2P;

    // Broadcast to all child nodes
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *)route_table,
                                CONFIG_MESH_ROUTE_TABLE_SIZE * 6,
                                &route_table_size);

    int success_count = 0;
    for (int i = 1; i < route_table_size; i++) {  // Skip root (index 0)
        esp_err_t err = mesh_send_with_bridge(&route_table[i], &mesh_data,
                                               MESH_DATA_P2P, NULL, 0);
        if (err == ESP_OK) {
            success_count++;
        }
    }

    ESP_LOGI("MESH_ROOT", "Plugin data forwarded: %s (%d bytes) to %d/%d nodes",
             plugin_name, len, success_count, route_table_size - 1);

    return ESP_OK;
}
```

---

## Implementation Phases

### Phase 1: Plugin Web UI Registration System
- Create `plugin_web_ui.h` and `plugin_web_ui.c`
- Implement registration and retrieval functions
- Integrate with plugin system

### Phase 2: Bundle Endpoint
- Implement HTTP handler for `GET /api/plugin/<name>/bundle`
- Implement streaming JSON builder
- Add error handling

### Phase 3: Data Endpoint
- Implement HTTP handler for `POST /api/plugin/<name>/data`
- Implement plugin name validation
- Add payload size validation

### Phase 4: Root Node Mesh Forwarding
- Implement `plugin_forward_data_to_mesh()` function
- Integrate with HTTP handler
- Add error handling

### Phase 5: JavaScript Utilities
- Create `plugin-web-ui.js` module
- Implement bundle fetching
- Implement data sending

### Phase 6: External Webserver Proxy
- Add proxy routes to `lyktparad-server`
- Integrate with UDP bridge
- Test end-to-end flow

### Phase 7: Example Plugin
- Create example plugin with web UI
- Document usage patterns
- Test all features

---

## References

- [Plugin System Developer Guide](plugin-system.md)
- [External Webserver Documentation](external-webserver/external-web-server-and-ui.md)
- [Mesh Command Bridge Protocol](external-webserver/mesh-command-bridge.md)
- [ESP-IDF HTTP Server Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html)
- [Research Findings](plugin-web-ui-research-findings.md)
- [Design Decisions](plugin-web-ui-design-decisions.md)
- [End-to-End Review](plugin-web-ui-end-to-end-review.md)

---

## Appendix A: Data Flow Diagrams

### Bundle Request Flow

```
Browser
  │
  ├─► GET /api/plugin/rgb_effect/bundle
  │
  ▼
Embedded Webserver (mesh_web.c)
  │
  ├─► Extract plugin name: "rgb_effect"
  ├─► Validate plugin name (regex)
  ├─► Look up plugin: plugin_get_by_name("rgb_effect")
  │
  ▼
Plugin Web UI System (plugin_web_ui.c)
  │
  ├─► Get callbacks: plugin->web_ui
  ├─► Invoke html_callback() → returns Flash pointer
  ├─► Invoke js_callback() → returns Flash pointer
  ├─► Invoke css_callback() → returns Flash pointer
  │
  ▼
JSON Bundle Building (streaming)
  │
  ├─► Send: {
  ├─► Send: "html":"<div>..."
  ├─► Send: ,"js":"function..."
  ├─► Send: ,"css":".plugin-rgb..."
  ├─► Send: }
  │
  ▼
HTTP Response (Chunked Transfer Encoding)
  │
  ▼
Browser
  │
  ├─► Parse JSON: {html: "...", js: "...", css: "..."}
  ├─► Inject HTML into DOM
  ├─► Inject CSS into <style> tag
  ├─► Execute JavaScript
```

### Data Request Flow

```
Browser
  │
  ├─► POST /api/plugin/rgb_effect/data
  ├─► Content-Type: application/octet-stream
  ├─► Body: [0xFF, 0x00, 0x00] (3 bytes, RGB values)
  │
  ▼
Embedded Webserver (mesh_web.c)
  │
  ├─► Extract plugin name: "rgb_effect"
  ├─► Validate plugin name (regex)
  ├─► Validate payload size (max 512 bytes)
  ├─► Read payload: httpd_req_recv() → [0xFF, 0x00, 0x00]
  │
  ▼
Root Node Mesh Forwarding (mesh_root.c)
  │
  ├─► Get plugin ID: plugin_get_id_by_name() → 0x0B
  ├─► Get mesh buffer: mesh_common_get_tx_buf()
  ├─► Construct command:
  │   ├─► buffer[0] = 0x0B (PLUGIN_ID)
  │   ├─► buffer[1] = 0x04 (PLUGIN_CMD_DATA)
  │   └─► buffer[2+] = [0xFF, 0x00, 0x00] (memcpy, zero processing)
  │
  ▼
Mesh Broadcast
  │
  ├─► Get routing table
  ├─► For each child node:
  │   └─► mesh_send_with_bridge() → [0x0B, 0x04, 0xFF, 0x00, 0x00]
  │
  ▼
Child Nodes (mesh_child.c)
  │
  ├─► Receive mesh command
  ├─► Extract plugin ID: 0x0B
  ├─► Extract command: PLUGIN_CMD_DATA (0x04)
  │
  ▼
Plugin System (plugin_system.c)
  │
  ├─► Look up plugin by ID: plugin_get_by_id(0x0B)
  ├─► Call plugin command_handler()
  │   └─► Receives: [0x04, 0xFF, 0x00, 0x00]
  │
  ▼
Plugin (rgb_effect_plugin.c)
  │
  ├─► Parse raw bytes: [0xFF, 0x00, 0x00]
  ├─► Extract RGB values: R=255, G=0, B=0
  ├─► Apply RGB effect to LEDs
```

---

## Appendix B: Memory Usage Analysis

### Flash Content (Static)

**Memory Usage**: 0 bytes RAM

**Example**:
```c
static const char html_content[] = "<div>...</div>";  // Stored in .rodata (Flash)
// Size: 100 bytes in Flash, 0 bytes in RAM
```

**Serving**: Direct pointer serving, zero-copy from Flash

### Heap Content (Dynamic)

**Memory Usage**: Content size in RAM (temporary)

**Example**:
```c
char *html = asprintf(&html, "<div>Length: %d</div>", length);
// Size: 50 bytes in RAM (allocated), freed after HTTP response
```

**Serving**: Content copied during callback, freed after use

### Streaming vs Buffer Comparison

**10KB Bundle Example**:

**Streaming Approach**:
- RAM usage: ~512 bytes (small buffer for JSON chunks)
- CPU usage: On-the-fly escaping (minimal)
- Scalability: Handles any size (limited by Flash/Heap, not RAM)

**Buffer Approach**:
- RAM usage: ~10KB (entire JSON buffer)
- CPU usage: Build entire JSON, then send (higher)
- Scalability: Limited by available RAM (OOM risk on C3)

**Recommendation**: Use streaming for bundles > 1KB, buffer for smaller bundles.

---

## Appendix C: Security Analysis

### Attack Vectors Considered

1. **Path Traversal**: Prevented by regex validation (no `../` or `/` characters)
2. **Command Injection**: Prevented by regex validation (no shell special characters)
3. **Buffer Overflow**: Prevented by size validation and buffer limits
4. **Information Disclosure**: Prevented by generic error messages
5. **XSS**: Mitigated by trusted code model and namespace scoping

### Security Measures

- **Plugin Name Validation**: Strict regex `^[a-zA-Z0-9_-]+$`
- **Payload Size Validation**: Max 512 bytes (early rejection)
- **Error Message Sanitization**: Generic messages, detailed logging server-side only
- **Memory Guard**: `esp_ptr_in_drom()` check before free()
- **Namespace Scoping**: CSS/JS prefixes prevent conflicts

---

**Copyright (c) 2025 the_louie**
