# Plugin Web UI Architecture - Research Findings

**Document Version**: 1.0
**Date**: 2025-01-29
**Status**: Research Phase Complete

---

## Overview

This document contains research findings from Phase 1 of the Plugin Web UI Architecture design process. The research focused on callback-based web content serving patterns, ESP-IDF HTTP server capabilities, memory management strategies, and analysis of existing system components.

---

## 1. Callback-Based Web Content Serving Patterns

### Embedded Systems Patterns

**Finding**: Callback-based content generation is a common pattern in embedded web servers.

**Patterns Identified**:
1. **Lazy Evaluation**: Content generated on-demand when requested, not pre-computed
2. **Pointer-Based Serving**: Direct pointer serving from Flash (zero-copy) for static content
3. **Lifetime Management**: Clear lifetime contracts for callback-returned strings

**Best Practices**:
- Use const pointers for Flash content (permanent lifetime)
- Document lifetime requirements clearly
- Use memory guards to prevent double-free errors

**References**:
- ESP-IDF HTTP server examples use callback patterns for dynamic content
- Common pattern: `const char *get_content(void)` callback signature

### Memory-Efficient String Handling

**Finding**: String pointer approach is preferred over string copy for embedded systems.

**Rationale**:
- Copy approach: Memory overhead (duplicates content in RAM)
- Pointer approach: No copying, but requires lifetime management
- Flash content: Permanent lifetime, pointer approach ideal
- Heap content: Temporary lifetime, must be freed after use

**Recommendation**: Use pointer approach with clear lifetime contracts and memory guards.

---

## 2. ESP-IDF HTTP Server Callback Patterns

### httpd_resp_send_chunk() API

**Finding**: ESP-IDF HTTP server supports Chunked Transfer Encoding via `httpd_resp_send_chunk()`.

**API Usage**:
```c
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *chunk, ssize_t len);
```

**Capabilities**:
- Streams response data in chunks
- No need to build entire response in memory
- Enables serving large responses with minimal RAM usage

**Limitations**:
- Must call `httpd_resp_send_chunk()` multiple times (one per chunk)
- Final chunk: Pass `NULL` or empty string to signal end
- Content-Type must be set before first chunk

**Best Practices**:
- Use small chunks (few hundred bytes) to minimize RAM usage
- Escape JSON on-the-fly during chunk sending
- Handle errors gracefully (chunk send failures)

### Streaming Response Patterns

**Pattern**: Build response incrementally, send chunks as they're ready

**Example Flow**:
1. Send opening JSON brace `{`
2. Send key and opening quote `"html":"`
3. Stream escaped content chunk by chunk
4. Send closing quote `"`
5. Send comma `,`
6. Repeat for next field
7. Send closing brace `}`

**Benefits**:
- Minimal RAM usage (only small buffers needed)
- Enables serving large content (10KB+) on C3
- Avoids OOM errors

---

## 3. Memory Management Patterns

### Flash Memory Detection

**Finding**: ESP-IDF provides macros for Flash memory detection.

**Macros**:
- `esp_ptr_in_drom(ptr)`: Returns true if pointer is in Flash (data)
- `esp_ptr_in_irom(ptr)`: Returns true if pointer is in Flash (instructions)

**Usage**:
```c
if (esp_ptr_in_drom(ptr)) {
    // Pointer is in Flash, don't free()
} else {
    // Pointer is in Heap, safe to free()
    free(ptr);
}
```

**Portability**:
- Works across ESP32 variants (C3, P4, S3)
- Standard ESP-IDF API, no variant-specific code needed

**Recommendation**: Use `esp_ptr_in_drom()` as memory guard before free().

### Flash vs Heap Content Serving

**Flash Content**:
- Stored in `.rodata` section (read-only data in Flash)
- Zero RAM usage (direct pointer serving)
- Lifetime: Permanent (no management needed)
- Detection: `esp_ptr_in_drom()` returns true

**Heap Content**:
- Allocated at runtime (malloc, asprintf, snprintf)
- RAM usage: Content size
- Lifetime: Temporary (must be freed after use)
- Detection: `esp_ptr_in_drom()` returns false

**Strategy**: Prefer Flash content when possible, use Heap only when dynamic content needed.

---

## 4. Existing System Component Analysis

### Plugin System Architecture

**Structure**: `plugin_info_t` contains plugin metadata and callbacks

**Key Fields**:
- `name`: Plugin name (unique identifier)
- `command_id`: Assigned plugin ID (0x0B-0xEE)
- `callbacks`: Plugin callback functions
- `user_data`: Optional user data pointer

**Extension Point**: Can add `web_ui` pointer field to `plugin_info_t` for backward compatibility

**Lookup Functions**:
- `plugin_get_by_name()`: Look up plugin by name
- `plugin_get_by_id()`: Look up plugin by plugin ID

**Recommendation**: Use existing lookup functions, add `web_ui` pointer to `plugin_info_t`.

### Web Server Implementation

**HTTP Server**: ESP-IDF HTTP server (`esp_http_server.h`)

**Handler Registration**: `httpd_register_uri_handler()` with URI patterns

**Existing Patterns**:
- URI handlers registered in `mesh_web_server_start()`
- Handler functions return `esp_err_t`
- Error handling via HTTP status codes

**URL Pattern Matching**: ESP-IDF supports wildcard patterns (e.g., `/api/plugin/*/bundle`)

**Recommendation**: Follow existing handler patterns, register new handlers in `mesh_web_server_start()`.

### Mesh Forwarding Mechanisms

**Command Format**: `[PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N]`

**Forwarding Function**: `mesh_send_with_bridge()` in `mesh_common.c`

**Broadcast Pattern**: Iterate through routing table, send to each child node

**Existing Pattern**:
```c
uint8_t *tx_buf = mesh_common_get_tx_buf();
tx_buf[0] = plugin_id;
tx_buf[1] = cmd;
// ... copy data to tx_buf[2+]
mesh_data_t data = { ... };
mesh_send_with_bridge(&route_table[i], &data, ...);
```

**Recommendation**: Follow existing broadcast pattern for PLUGIN_CMD_DATA forwarding.

### External Webserver Proxy

**Implementation**: Express.js routes in `lyktparad-server/routes/proxy.js`

**UDP Bridge Protocol**: Existing protocol for ESP32 communication

**Proxy Pattern**: Forward HTTP requests to ESP32 via UDP, return response

**Recommendation**: Add new proxy routes following existing patterns.

---

## 5. Security Considerations

### Plugin Name Validation

**Requirement**: Prevent path traversal and command injection

**Validation Strategy**: Strict regex `^[a-zA-Z0-9_-]+$`

**Implementation**: Validate early, before any processing

**Error Handling**: 400 Bad Request for invalid names

### Content Validation

**Finding**: Plugins are trusted code (compiled into firmware)

**Implication**: No runtime content validation needed

**Trade-off**: Simpler architecture, but requires firmware review process

**Recommendation**: Document trusted code model, require namespace scoping for CSS/JS.

---

## 6. Performance Considerations

### Memory Usage

**Flash Content**: Zero RAM usage (direct pointer serving)

**Heap Content**: Content size in RAM (freed after use)

**Streaming**: Small buffers (few hundred bytes) vs large buffers (10KB+)

**Recommendation**: Prefer Flash content, use streaming for large bundles.

### CPU Usage

**Transparent Proxy**: Minimal CPU (header insertion + memcpy only)

**Streaming JSON**: On-the-fly escaping (no large string building)

**Callback Invocation**: Direct function calls (no overhead)

**Recommendation**: Maintain zero-CPU processing for data forwarding.

---

## 7. Portability Analysis

### ESP32 Variants

**C3**: ~400KB RAM, ~4MB Flash
- Streaming essential for large bundles
- Flash content preferred

**P4**: ~512KB RAM, ~16MB Flash
- More headroom for buffers
- Can handle larger content

**S3**: Similar to P4

**Recommendation**: Design for C3 constraints (most restrictive), works on all variants.

### ESP-IDF Macros

**Finding**: `esp_ptr_in_drom()` works across all ESP32 variants

**Portability**: Standard ESP-IDF API, no variant-specific code needed

**Recommendation**: Use standard ESP-IDF APIs for portability.

---

## Conclusions

1. **Callback Pattern**: Use pointer-based callbacks with clear lifetime contracts
2. **Streaming**: Essential for large content on C3, use Chunked Transfer Encoding
3. **Memory Guard**: Use `esp_ptr_in_drom()` before free() to prevent errors
4. **Flash Preference**: Prefer Flash content (zero RAM) when possible
5. **Transparent Proxy**: Maintain zero-CPU processing for data forwarding
6. **Security**: Strict plugin name validation, trusted code model
7. **Portability**: Use standard ESP-IDF APIs, design for C3 constraints

---

**Copyright (c) 2025 the_louie**
