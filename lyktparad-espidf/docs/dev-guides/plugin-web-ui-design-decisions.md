# Plugin Web UI Architecture - Design Decisions

**Document Version**: 1.0
**Date**: 2025-01-29
**Status**: Design Phase Complete

---

## Overview

This document records all major design decisions made during the Plugin Web UI Architecture design process. Each decision includes the rationale, alternatives considered, and trade-offs.

---

## 1. Callback Interface Design

### Decision: Unified Callback Signature with No Parameters

**Design**:
```c
typedef const char *(*plugin_web_content_callback_t)(void);
```

**Rationale**:
- Simplifies callback interface (no format parameter needed)
- Format determined by callback assignment (html_callback, js_callback, css_callback)
- Consistent with ESP-IDF callback patterns
- Easy to implement and understand

**Alternatives Considered**:
1. **Callback with format parameter**: `const char *(*callback)(const char *format)`
   - Rejected: Format is redundant (determined by which callback field is used)
   - Rejected: Adds unnecessary parameter complexity

2. **Separate callback types**: `html_callback_t`, `js_callback_t`, `css_callback_t`
   - Rejected: Unnecessary type complexity
   - Rejected: Unified type is simpler and sufficient

**Trade-offs**:
- **Pros**: Simple, consistent, easy to use
- **Cons**: None significant

---

## 2. Memory Management: Bit-Masked Flags

### Decision: Single `uint8_t dynamic_mask` with Bit Definitions

**Design**:
```c
#define PLUGIN_WEB_HTML_DYNAMIC  (1 << 0)
#define PLUGIN_WEB_JS_DYNAMIC    (1 << 1)
#define PLUGIN_WEB_CSS_DYNAMIC   (1 << 2)

typedef struct {
    plugin_web_content_callback_t html_callback;
    plugin_web_content_callback_t js_callback;
    plugin_web_content_callback_t css_callback;
    uint8_t dynamic_mask;  // Bit-masked flags
} plugin_web_ui_callbacks_t;
```

**Rationale**:
- Minimizes registry footprint (1 byte vs 3 booleans = 3 bytes)
- Efficient bitwise operations for flag checking
- Memory guard: `esp_ptr_in_drom()` safety check before free()

**Alternatives Considered**:
1. **Three separate boolean fields**: `bool html_is_dynamic`, `bool js_is_dynamic`, `bool css_is_dynamic`
   - Rejected: Memory overhead (3 bytes vs 1 byte)
   - Rejected: Less efficient (3 boolean checks vs 1 bitwise operation)

2. **Enum-based approach**: Single enum with combinations
   - Rejected: Less flexible, harder to extend
   - Rejected: Bit-mask is more standard for flags

**Trade-offs**:
- **Pros**: Minimal memory footprint, efficient operations, standard pattern
- **Cons**: Slightly more complex bitwise operations (but well-documented)

---

## 3. Bundle Endpoint: Streaming vs Buffer

### Decision: Streaming JSON Builder Using Chunked Transfer Encoding for Large Content, Buffer-Based for Small Content

**Design**:
- **Streaming**: Use `httpd_resp_send_chunk()` for large content (> 1KB)
- **Buffer**: Use single buffer for small content (< 1KB)
- **Fallback**: Buffer approach if streaming not feasible

**Rationale**:
- Streaming enables serving 10KB bundle using only a few hundred bytes of RAM
- Avoids OOM on C3 with limited RAM
- Buffer-based approach simpler for small content
- Fallback ensures functionality even if streaming has issues

**Alternatives Considered**:
1. **Always use buffer**: Build entire JSON in single buffer
   - Rejected: OOM risk on C3 (10KB buffer may not be available)
   - Rejected: Doesn't scale to larger plugin UIs

2. **Always use streaming**: Always use Chunked Transfer Encoding
   - Rejected: More complex for small content
   - Rejected: Unnecessary overhead for < 1KB content

**Trade-offs**:
- **Pros**: Handles both small and large content efficiently, avoids OOM
- **Cons**: More complex implementation (two code paths)

---

## 4. Transparent Proxy Design

### Decision: ESP32 Forwards Raw Bytes Without Any Processing

**Design**:
- Read HTTP request body (raw bytes)
- Insert PLUGIN_ID at buffer[0]
- Insert PLUGIN_CMD_DATA (0x04) at buffer[1]
- Copy raw bytes to buffer[2+]
- Broadcast to mesh

**Rationale**:
- Zero CPU cycles for data forwarding (only header insertion and memcpy)
- Enables heterogeneous hardware setups (P4 root, C3 children)
- Simplifies implementation (no encoding/decoding logic)
- JavaScript already handles encoding

**Alternatives Considered**:
1. **ESP32 processes and validates data**: Parse, validate, transform data
   - Rejected: CPU cost (parsing, validation, transformation)
   - Rejected: Complexity (need to understand plugin-specific protocols)
   - Rejected: Doesn't scale to heterogeneous hardware

2. **ESP32 decodes and re-encodes**: Convert between formats
   - Rejected: CPU cost (decoding and encoding)
   - Rejected: Memory cost (intermediate buffers)
   - Rejected: Unnecessary (JavaScript already encoded)

**Trade-offs**:
- **Pros**: Zero CPU processing, simple implementation, enables heterogeneous hardware
- **Cons**: No validation on ESP32 (but plugins validate in command_handler)

---

## 5. URL Pattern Matching

### Decision: Manual URI Parsing in Handler (ESP-IDF Doesn't Support Wildcards)

**Design**:
- Register handler with base pattern: `/api/plugin/*/bundle` (if supported) or catch-all
- Parse URI manually in handler to extract plugin name
- Extract plugin name between `/api/plugin/` and `/bundle` or `/data`

**Rationale**:
- ESP-IDF HTTP server doesn't support wildcard patterns directly
- Manual parsing is straightforward (string operations)
- Early validation prevents invalid requests

**Alternatives Considered**:
1. **Register handler for each plugin**: Dynamic handler registration
   - Rejected: Not scalable (would need to register handlers for each plugin)
   - Rejected: Complex (handler registration during plugin init)

2. **Use query parameters**: `/api/plugin/bundle?name=<plugin-name>`
   - Rejected: Less RESTful
   - Rejected: Doesn't match design specification

**Trade-offs**:
- **Pros**: Works with ESP-IDF limitations, straightforward implementation
- **Cons**: Manual parsing required (but simple string operations)

---

## 6. Plugin Name Validation

### Decision: Strict Regex Validation `^[a-zA-Z0-9_-]+$`

**Design**:
- Validate plugin name against regex: `^[a-zA-Z0-9_-]+$`
- Early validation (before any processing)
- 400 Bad Request for invalid names

**Rationale**:
- Prevents path traversal attacks (no `../` or `/` characters)
- Prevents command injection (no special shell characters)
- Simple and efficient (regex match)

**Alternatives Considered**:
1. **Whitelist approach**: Check against registered plugin names only
   - Rejected: Doesn't prevent injection if plugin name contains special chars
   - Rejected: Need regex anyway for URL extraction

2. **Blacklist approach**: Block known dangerous characters
   - Rejected: Less secure (might miss edge cases)
   - Rejected: Whitelist (regex) is more secure

**Trade-offs**:
- **Pros**: Secure, simple, efficient
- **Cons**: None significant

---

## 7. Content Lifetime Management

### Decision: Pointer Approach with Lifetime Contracts

**Design**:
- **Flash content**: Permanent lifetime (pointer valid for program lifetime)
- **Heap content**: Temporary lifetime (valid during callback invocation, freed after HTTP response)

**Rationale**:
- Pointer approach: No copying overhead
- Flash content: Permanent, no management needed
- Heap content: Freed immediately after use (minimal RAM usage)

**Alternatives Considered**:
1. **String copy approach**: Copy all content to new buffers
   - Rejected: Memory overhead (duplicates content in RAM)
   - Rejected: CPU overhead (copying large strings)

2. **Reference counting**: Track content references
   - Rejected: Complexity (reference counting overhead)
   - Rejected: Unnecessary (lifetime is clear: callback invocation duration)

**Trade-offs**:
- **Pros**: Efficient (no copying), clear lifetime contracts
- **Cons**: Requires careful lifetime management (but well-documented)

---

## 8. Memory Guard Design

### Decision: Use `esp_ptr_in_drom()` Before free()

**Design**:
```c
if (cb->dynamic_mask & components[i].flag) {
    if (esp_ptr_in_drom(raw_content)) {
        ESP_LOGW(TAG, "Warning: %s marked dynamic but pointer in Flash. Skipping free.", components[i].key);
    } else {
        free((void *)raw_content);
    }
}
```

**Rationale**:
- Prevents attempting to free() Flash pointers
- Safety check if developer accidentally sets dynamic bit for static string
- Prevents crashes and undefined behavior

**Alternatives Considered**:
1. **Trust developer**: Assume dynamic_mask is always correct
   - Rejected: Unsafe (crashes if wrong)
   - Rejected: No defense against mistakes

2. **Always check**: Check pointer location regardless of flag
   - Accepted: Current design (always check if flag is set)

**Trade-offs**:
- **Pros**: Safe, prevents crashes, helpful warnings
- **Cons**: Slight overhead (pointer check), but negligible

---

## 9. JSON Escaping Strategy

### Decision: Simple JSON Escape Function

**Design**:
```c
static size_t json_escape_copy(char *dest, const char *src, size_t dest_size) {
    // Escape: " -> \", \ -> \\, \n -> \n, strip \r
}
```

**Rationale**:
- Intentionally simple (escapes quotes, backslashes, newlines)
- Ensures valid JSON structure
- Strips carriage returns (not needed in JSON strings)
- On-the-fly escaping during streaming (no large buffers)

**Alternatives Considered**:
1. **Comprehensive escaping**: Escape all control characters, Unicode, etc.
   - Rejected: Complexity (most content doesn't need it)
   - Rejected: CPU cost (checking every character)
   - Rejected: Plugins are trusted code (content is controlled)

2. **No escaping**: Assume content is already JSON-safe
   - Rejected: Unsafe (quotes and backslashes break JSON)
   - Rejected: Invalid JSON if content contains special characters

**Trade-offs**:
- **Pros**: Simple, efficient, handles common cases
- **Cons**: Doesn't handle all edge cases (but sufficient for HTML/CSS/JS content)

---

## 10. Namespace Scoping Strategy

### Decision: Documentation Requirement, Not Runtime Validation

**Design**:
- **CSS**: Plugins must prefix classes with plugin name (e.g., `.plugin-rgb-slider`)
- **JavaScript**: Plugins must prefix functions with plugin name (e.g., `plugin_rgb_init()`)
- **Enforcement**: Documentation requirement, code review process

**Rationale**:
- Plugins are trusted code (compiled into firmware)
- Runtime validation would be complex and expensive
- Documentation and code review are sufficient
- Prevents style bleeding and function name conflicts

**Alternatives Considered**:
1. **Runtime validation**: Parse CSS/JS and validate namespace prefixes
   - Rejected: Complex (CSS/JS parsing)
   - Rejected: CPU cost (parsing on every request)
   - Rejected: Unnecessary (trusted code model)

2. **Build-time validation**: Validate during compilation
   - Rejected: Complex (need CSS/JS parsers in build system)
   - Rejected: Not standard practice

**Trade-offs**:
- **Pros**: Simple, no runtime overhead, sufficient for trusted code
- **Cons**: Relies on code review (but acceptable for trusted code)

---

## 11. Error Handling Strategy

### Decision: Generic Error Messages, Detailed Server-Side Logging

**Design**:
- **Client**: Generic error messages (e.g., "Plugin not found", "Internal server error")
- **Server**: Detailed logging with ESP_LOG (plugin name, error codes, context)
- **Security**: No sensitive information exposed to client

**Rationale**:
- Prevents information disclosure (no internal state exposed)
- Detailed logging helps debugging
- Generic messages prevent attack vector enumeration

**Alternatives Considered**:
1. **Detailed error messages**: Include plugin names, error codes, stack traces
   - Rejected: Information disclosure risk
   - Rejected: Helps attackers enumerate system

2. **No error messages**: Return empty responses
   - Rejected: Poor user experience
   - Rejected: Hard to debug

**Trade-offs**:
- **Pros**: Secure, debuggable, user-friendly
- **Cons**: None significant

---

## 12. Max Payload Limit

### Decision: 512 Bytes Recommended (Not Enforced by System)

**Design**:
- **Recommended limit**: 512 bytes for PLUGIN_CMD_DATA payload
- **Total command**: 512 + 1 (PLUGIN_ID) + 1 (PLUGIN_CMD_DATA) = 514 bytes < 1024 bytes
- **Enforcement**: Early validation, 400 Bad Request if exceeded
- **Rationale**: Leaves room within 1024 byte mesh command limit

**Alternatives Considered**:
1. **No limit**: Allow up to 1022 bytes (1024 - 2 header bytes)
   - Rejected: Too close to limit (no safety margin)
   - Rejected: Risk of exceeding limit with headers

2. **Strict enforcement**: Hard limit, plugins cannot exceed
   - Rejected: Too restrictive (some plugins may need more)
   - Rejected: Recommended limit with validation is sufficient

**Trade-offs**:
- **Pros**: Safety margin, flexible (recommendation not hard limit)
- **Cons**: Plugins might exceed (but they validate in command_handler)

---

## 13. Storage Location for Web UI Callbacks

### Decision: Extension to `plugin_info_t` Structure

**Design**:
- Add `plugin_web_ui_callbacks_t *web_ui` pointer to `plugin_info_t`
- Allocate via `malloc()` during registration
- NULL pointer indicates no web UI support

**Rationale**:
- Backward compatible (existing plugins have NULL pointer)
- Minimal memory overhead (one pointer per plugin)
- Easy to check if plugin has web UI (NULL check)
- Integrates cleanly with existing plugin system

**Alternatives Considered**:
1. **Separate registry**: Maintain separate array for web UI callbacks
   - Rejected: More complex (need to maintain two registries)
   - Rejected: Harder to keep in sync

2. **Inline structure**: Embed `plugin_web_ui_callbacks_t` directly in `plugin_info_t`
   - Rejected: Memory overhead (all plugins pay cost, even without web UI)
   - Rejected: Pointer approach is more memory-efficient

**Trade-offs**:
- **Pros**: Backward compatible, memory-efficient, clean integration
- **Cons**: Requires malloc (but only for plugins with web UI)

---

## Summary

All design decisions prioritize:
1. **Memory efficiency**: Zero-RAM for Flash content, minimal RAM for Heap
2. **CPU efficiency**: Zero processing for data forwarding, streaming for large content
3. **Simplicity**: Simple interfaces, clear contracts, easy to use
4. **Security**: Strict validation, trusted code model, no information disclosure
5. **Backward compatibility**: Existing plugins continue to work

---

**Copyright (c) 2025 the_louie**
