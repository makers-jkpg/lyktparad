/* Plugin Web UI Implementation
 *
 * This module implements the Plugin Web UI Registration System, enabling plugins
 * to register web UI callbacks that provide HTML, CSS, and JavaScript content
 * dynamically. The system uses bit-masked flags to distinguish Flash (static,
 * zero-RAM) from Heap (dynamic) content, supports JSON bundle building with
 * dry-run mode, and integrates seamlessly with the existing plugin system.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "plugin_web_ui.h"
#include "plugin_system.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_http_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "PLUGIN_WEB_UI";

/* Streaming configuration constants */
#define STREAMING_THRESHOLD_BYTES  PLUGIN_WEB_STREAMING_THRESHOLD_BYTES  /* Use streaming for bundles > 1KB */
#define STREAMING_CHUNK_SIZE       (512U)   /* Chunk buffer size for streaming (minimizes RAM) */
#define MAX_BUNDLE_SIZE_BYTES      PLUGIN_WEB_MAX_BUNDLE_SIZE_BYTES      /* Maximum bundle size (10KB) to prevent OOM */

/**
 * @brief Internal helper to determine if a pointer resides in Flash (DROM).
 *
 * This acts as a safety check for the dynamic_mask flags. Before attempting
 * to free() a pointer, we verify it's not in Flash to prevent crashes.
 *
 * The function uses esp_ptr_in_drom() macro from ESP-IDF, which provides
 * portable Flash detection across all ESP32 variants. This macro correctly
 * identifies pointers in Flash memory regardless of whether the code is running
 * on ESP32, ESP32-C3, ESP32-S3, ESP32-P4, or other variants.
 *
 * @param ptr Pointer to check (may be NULL)
 * @return true if pointer is in Flash (DROM), false if in Heap or NULL
 * @note Uses esp_ptr_in_drom() from esp_memory_utils.h for cross-variant portability
 * @note This approach works correctly on all ESP32 variants without variant-specific code
 */
static bool is_ptr_in_flash(const void *ptr)
{
    if (ptr == NULL) {
        return false;
    }
    return esp_ptr_in_drom(ptr);
}

/**
 * @brief Simple JSON string escape helper.
 *
 * Escapes quotes, backslashes, and newlines to ensure a valid JSON value.
 * Strips carriage returns as they're not needed in JSON strings.
 *
 * @param dest Destination buffer for escaped string
 * @param src Source string to escape
 * @param dest_size Size of destination buffer
 * @return Number of bytes written (excluding null terminator)
 */
static size_t json_escape_copy(char *dest, const char *src, size_t dest_size)
{
    if (dest == NULL || src == NULL || dest_size == 0) {
        return 0;
    }

    size_t written = 0;
    const char *p = src;

    while (*p != '\0' && written < dest_size - 1) {
        switch (*p) {
            case '"':
                /* Escape quote: " -> \" */
                if (written < dest_size - 2) {
                    dest[written++] = '\\';
                    dest[written++] = '"';
                } else {
                    /* Buffer too small, stop */
                    goto done;
                }
                break;

            case '\\':
                /* Escape backslash: \ -> \\ */
                if (written < dest_size - 2) {
                    dest[written++] = '\\';
                    dest[written++] = '\\';
                } else {
                    /* Buffer too small, stop */
                    goto done;
                }
                break;

            case '\n':
                /* Escape newline: \n -> \n (represented as two characters in JSON) */
                if (written < dest_size - 2) {
                    dest[written++] = '\\';
                    dest[written++] = 'n';
                } else {
                    /* Buffer too small, stop */
                    goto done;
                }
                break;

            case '\r':
                /* Strip carriage returns */
                p++;
                continue;

            default:
                /* Copy character as-is */
                dest[written++] = *p;
                break;
        }
        p++;
    }

done:
    /* Null terminate */
    if (written < dest_size) {
        dest[written] = '\0';
    } else if (dest_size > 0) {
        dest[dest_size - 1] = '\0';
    }

    return written;
}

/**
 * @brief Calculate the size needed for JSON-escaped string.
 *
 * This is used in dry-run mode to calculate buffer size without building the string.
 *
 * @param src Source string
 * @return Size needed for escaped string (excluding null terminator)
 */
static size_t json_escape_size(const char *src)
{
    if (src == NULL) {
        return 0;
    }

    size_t size = 0;
    const char *p = src;

    while (*p != '\0') {
        switch (*p) {
            case '"':
            case '\\':
            case '\n':
                /* Each of these needs 2 characters in escaped form */
                size += 2;
                break;

            case '\r':
                /* Carriage returns are stripped, don't count */
                break;

            default:
                /* Regular character */
                size += 1;
                break;
        }
        p++;
    }

    return size;
}

/**
 * @brief Stream JSON-escaped content directly to HTTP response.
 *
 * This function reads source content, escapes special characters (quotes, backslashes,
 * newlines), strips carriage returns, and sends the escaped content in chunks via
 * httpd_resp_send_chunk(). This enables serving large content with minimal RAM usage
 * (only a small chunk buffer is allocated, not the entire escaped content).
 *
 * @param req HTTP request handle for sending chunks
 * @param src Source string to escape and stream
 * @return ESP_OK on success
 * @return ESP_FAIL if httpd_resp_send_chunk() fails
 *
 * @note Uses a small internal buffer (STREAMING_CHUNK_SIZE bytes) for chunking
 * @note Handles all escape cases: quotes, backslashes, newlines, carriage returns
 * @note Escape sequences are never split across chunk boundaries (single char lookahead)
 */
static esp_err_t json_escape_and_send_chunk(httpd_req_t *req, const char *src)
{
    if (req == NULL || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Small buffer for chunking (minimizes RAM usage) */
    char chunk_buf[STREAMING_CHUNK_SIZE];
    size_t chunk_offset = 0;
    const char *p = src;

    while (*p != '\0') {
        /* Escape special characters */
        switch (*p) {
            case '"':
                /* Escape quote: " -> \" */
                if (chunk_offset >= STREAMING_CHUNK_SIZE - 2) {
                    /* Send current chunk first to make room for 2-byte escape sequence */
                    esp_err_t err = httpd_resp_send_chunk(req, chunk_buf, chunk_offset);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to send chunk: 0x%x", err);
                        return ESP_FAIL;
                    }
                    chunk_offset = 0;
                }
                chunk_buf[chunk_offset++] = '\\';
                chunk_buf[chunk_offset++] = '"';
                break;

            case '\\':
                /* Escape backslash: \ -> \\ */
                if (chunk_offset >= STREAMING_CHUNK_SIZE - 2) {
                    /* Send current chunk first to make room for 2-byte escape sequence */
                    esp_err_t err = httpd_resp_send_chunk(req, chunk_buf, chunk_offset);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to send chunk: 0x%x", err);
                        return ESP_FAIL;
                    }
                    chunk_offset = 0;
                }
                chunk_buf[chunk_offset++] = '\\';
                chunk_buf[chunk_offset++] = '\\';
                break;

            case '\n':
                /* Escape newline: \n -> \n (represented as two characters in JSON) */
                if (chunk_offset >= STREAMING_CHUNK_SIZE - 2) {
                    /* Send current chunk first to make room for 2-byte escape sequence */
                    esp_err_t err = httpd_resp_send_chunk(req, chunk_buf, chunk_offset);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to send chunk: 0x%x", err);
                        return ESP_FAIL;
                    }
                    chunk_offset = 0;
                }
                chunk_buf[chunk_offset++] = '\\';
                chunk_buf[chunk_offset++] = 'n';
                break;

            case '\r':
                /* Strip carriage returns (skip character) */
                p++;
                continue;

            default:
                /* Copy character as-is - check if buffer is full */
                if (chunk_offset >= STREAMING_CHUNK_SIZE) {
                    /* Buffer is full, send chunk first */
                    esp_err_t err = httpd_resp_send_chunk(req, chunk_buf, chunk_offset);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to send chunk: 0x%x", err);
                        return ESP_FAIL;
                    }
                    chunk_offset = 0;
                }
                chunk_buf[chunk_offset++] = *p;
                break;
        }
        p++;
    }

    /* Send remaining chunk if any */
    if (chunk_offset > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, chunk_buf, chunk_offset);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send final chunk: 0x%x", err);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

/**
 * @brief Builds and streams a JSON bundle for a plugin using chunked transfer encoding.
 *
 * This function builds a JSON object containing HTML, JavaScript, and CSS content
 * for a plugin's web UI and streams it directly to the HTTP response using
 * httpd_resp_send_chunk(). This enables serving large bundles with minimal RAM usage
 * (only small buffers are used for chunking, not the entire JSON string).
 *
 * JSON Format: {"html": "...", "js": "...", "css": "..."}
 * - NULL callbacks are omitted from the JSON object
 * - Content is JSON-escaped and streamed chunk by chunk
 * - Carriage returns are stripped
 *
 * @param req HTTP request handle for sending chunks
 * @param name The unique name of the plugin (must be registered)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if req or name is NULL
 * @return ESP_ERR_NOT_FOUND if plugin is not registered or has no web UI callbacks
 * @return ESP_FAIL if streaming fails
 *
 * @note Content-Type and CORS headers must be set before calling this function
 * @note Dynamic content (Heap) is freed after streaming
 * @note Flash content is never freed (memory guard prevents accidental free)
 * @note Final chunk (NULL) is sent to signal end of response
 */
esp_err_t plugin_get_web_bundle_streaming(httpd_req_t *req, const char *name)
{
    if (req == NULL || name == NULL) {
        ESP_LOGE(TAG, "Bundle streaming failed: req or name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Look up plugin by name */
    const plugin_info_t *plugin = plugin_get_by_name(name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "Bundle streaming failed: Plugin '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }

    /* Check if plugin has web UI callbacks */
    if (plugin->web_ui == NULL) {
        ESP_LOGE(TAG, "Bundle streaming failed: Plugin '%s' has no web UI callbacks", name);
        return ESP_ERR_NOT_FOUND;
    }

    plugin_web_ui_callbacks_t *cb = (plugin_web_ui_callbacks_t *)plugin->web_ui;
    esp_err_t err;

    /* Send opening brace */
    err = httpd_resp_send_chunk(req, "{", 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send opening brace: 0x%x", err);
        return ESP_FAIL;
    }

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

    /* Process each component (html, js, css) */
    for (int i = 0; i < 3; i++) {
        if (components[i].func != NULL) {
            const char *content = components[i].func();
            if (content == NULL) {
                /* Callback returned NULL, skip this component */
                continue;
            }

            /* Add comma separator if not first field */
            if (!first) {
                err = httpd_resp_send_chunk(req, ",", 1);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send comma: 0x%x", err);
                    /* Free dynamic content before returning error */
                    if (cb->dynamic_mask & components[i].flag) {
                        if (!is_ptr_in_flash(content)) {
                            free((void *)content);
                        }
                    }
                    return ESP_FAIL;
                }
            }

            /* Send field key and opening quote: "html":" */
            char key_buf[32];
            int key_len = snprintf(key_buf, sizeof(key_buf), "\"%s\":\"", components[i].key);
            if (key_len < 0 || key_len >= (int)sizeof(key_buf)) {
                ESP_LOGE(TAG, "Failed to format key for %s", components[i].key);
                /* Free dynamic content before returning error */
                if (cb->dynamic_mask & components[i].flag) {
                    if (!is_ptr_in_flash(content)) {
                        free((void *)content);
                    }
                }
                return ESP_FAIL;
            }
            err = httpd_resp_send_chunk(req, key_buf, key_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send key: 0x%x", err);
                /* Free dynamic content before returning error */
                if (cb->dynamic_mask & components[i].flag) {
                    if (!is_ptr_in_flash(content)) {
                        free((void *)content);
                    }
                }
                return ESP_FAIL;
            }

            /* Stream escaped content */
            err = json_escape_and_send_chunk(req, content);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stream escaped content for %s: 0x%x", components[i].key, err);
                /* Free dynamic content before returning error */
                if (cb->dynamic_mask & components[i].flag) {
                    if (!is_ptr_in_flash(content)) {
                        free((void *)content);
                    }
                }
                return ESP_FAIL;
            }

            /* Send closing quote */
            err = httpd_resp_send_chunk(req, "\"", 1);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send closing quote: 0x%x", err);
                /* Free dynamic content before returning error */
                if (cb->dynamic_mask & components[i].flag) {
                    if (!is_ptr_in_flash(content)) {
                        free((void *)content);
                    }
                }
                return ESP_FAIL;
            }

            /* Free dynamic content after successful streaming */
            if (cb->dynamic_mask & components[i].flag) {
                if (!is_ptr_in_flash(content)) {
                    free((void *)content);
                } else {
                    ESP_LOGW(TAG, "Warning: %s marked dynamic but pointer in Flash. Skipping free.", components[i].key);
                }
            }

            first = false;
        }
    }

    /* Send closing brace */
    err = httpd_resp_send_chunk(req, "}", 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send closing brace: 0x%x", err);
        return ESP_FAIL;
    }

    /* Send final chunk (NULL) to signal end of response */
    err = httpd_resp_send_chunk(req, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finalize chunked response: 0x%x", err);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t plugin_register_web_ui(const char *name, const plugin_web_ui_callbacks_t *callbacks)
{
    /* Validate input parameters */
    if (name == NULL) {
        ESP_LOGE(TAG, "Web UI registration failed: name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (callbacks == NULL) {
        ESP_LOGE(TAG, "Web UI registration failed: callbacks is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Look up plugin by name */
    const plugin_info_t *plugin = plugin_get_by_name(name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "Web UI registration failed: Plugin '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }

    /* Cast to non-const to modify web_ui pointer (registry is not const) */
    plugin_info_t *mutable_plugin = (plugin_info_t *)plugin;

    /* Free existing web_ui if already registered (prevent memory leak) */
    if (mutable_plugin->web_ui != NULL) {
        free(mutable_plugin->web_ui);
        mutable_plugin->web_ui = NULL;
    }

    /* Allocate storage for callbacks */
    plugin_web_ui_callbacks_t *web_ui = (plugin_web_ui_callbacks_t *)malloc(sizeof(plugin_web_ui_callbacks_t));
    if (web_ui == NULL) {
        ESP_LOGE(TAG, "Web UI registration failed: Memory allocation failed for plugin '%s'", name);
        return ESP_ERR_NO_MEM;
    }

    /* Copy callbacks structure to allocated memory */
    memcpy(web_ui, callbacks, sizeof(plugin_web_ui_callbacks_t));

    /* Store pointer in plugin info */
    mutable_plugin->web_ui = web_ui;

    ESP_LOGI(TAG, "Web UI registered for plugin: %s (Mask: 0x%02X)", name, callbacks->dynamic_mask);

    return ESP_OK;
}

esp_err_t plugin_get_web_bundle(const char *name, char *json_buffer, size_t buffer_size, size_t *required_size)
{
    /* Validate input parameters */
    if (name == NULL) {
        ESP_LOGE(TAG, "Bundle retrieval failed: name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (required_size == NULL) {
        ESP_LOGE(TAG, "Bundle retrieval failed: required_size is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Look up plugin by name */
    const plugin_info_t *plugin = plugin_get_by_name(name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "Bundle retrieval failed: Plugin '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }

    /* Check if plugin has web UI callbacks */
    if (plugin->web_ui == NULL) {
        ESP_LOGE(TAG, "Bundle retrieval failed: Plugin '%s' has no web UI callbacks", name);
        return ESP_ERR_NOT_FOUND;
    }

    plugin_web_ui_callbacks_t *cb = (plugin_web_ui_callbacks_t *)plugin->web_ui;

    /* Detect dry-run mode (json_buffer is NULL) */
    bool dry_run = (json_buffer == NULL);

    /* Calculate required size */
    size_t total_size = 1; /* Opening brace */
    int field_count = 0;

    const struct {
        const char *key;
        plugin_web_content_callback_t func;
        uint8_t flag;
    } components[] = {
        {"html", cb->html_callback, PLUGIN_WEB_HTML_DYNAMIC},
        {"js",   cb->js_callback,   PLUGIN_WEB_JS_DYNAMIC},
        {"css",  cb->css_callback,  PLUGIN_WEB_CSS_DYNAMIC}
    };

    /* First pass: Calculate size and optionally invoke callbacks for dry-run */
    for (int i = 0; i < 3; i++) {
        if (components[i].func != NULL) {
            const char *content = components[i].func();
            if (content == NULL) {
                /* Callback returned NULL, skip this component */
                continue;
            }

            /* Add comma separator if not first field */
            if (field_count > 0) {
                total_size += 1; /* Comma */
            }
            field_count++;

            /* Add field key and quotes: "html":" */
            size_t key_len = strlen(components[i].key);
            total_size += 1 + key_len + 3; /* "key":" */

            /* Add escaped content size */
            size_t content_size = json_escape_size(content);
            total_size += content_size;

            /* Add closing quote */
            total_size += 1; /* " */

            /* Free dynamic content if needed (only in dry-run, actual run frees after copying) */
            if (dry_run && (cb->dynamic_mask & components[i].flag)) {
                if (!is_ptr_in_flash(content)) {
                    free((void *)content);
                } else {
                    ESP_LOGW(TAG, "Warning: %s marked dynamic but pointer in Flash. Skipping free.", components[i].key);
                }
            }
        }
    }

    total_size += 1; /* Closing brace */
    total_size += 1; /* Null terminator */

    /* Store required size */
    *required_size = total_size;

    /* If dry-run mode, return now */
    if (dry_run) {
        return ESP_OK;
    }

    /* Validate buffer size */
    if (buffer_size < total_size) {
        ESP_LOGE(TAG, "Bundle retrieval failed: Buffer too small (%zu < %zu)", buffer_size, total_size);
        return ESP_ERR_NO_MEM;
    }

    /* Second pass: Build JSON */
    size_t offset = 0;
    bool first = true;

    /* Start JSON object */
    offset += snprintf(json_buffer + offset, buffer_size - offset, "{");

    for (int i = 0; i < 3; i++) {
        if (components[i].func != NULL) {
            const char *content = components[i].func();
            if (content == NULL) {
                /* Callback returned NULL, skip this component */
                continue;
            }

            /* Add comma separator if not first field */
            if (!first) {
                if (offset < buffer_size) {
                    json_buffer[offset++] = ',';
                } else {
                    goto buffer_overflow;
                }
            }

            /* Add field key and opening quote: "html":" */
            int written = snprintf(json_buffer + offset, buffer_size - offset, "\"%s\":\"", components[i].key);
            if (written < 0 || (size_t)written >= buffer_size - offset) {
                goto buffer_overflow;
            }
            offset += written;

            /* Escape and copy content */
            size_t escaped_len = json_escape_copy(json_buffer + offset, content, buffer_size - offset);
            offset += escaped_len;

            /* Add closing quote */
            if (offset < buffer_size) {
                json_buffer[offset++] = '\"';
            } else {
                goto buffer_overflow;
            }

            /* Free dynamic content if needed */
            if (cb->dynamic_mask & components[i].flag) {
                if (!is_ptr_in_flash(content)) {
                    free((void *)content);
                } else {
                    ESP_LOGW(TAG, "Warning: %s marked dynamic but pointer in Flash. Skipping free.", components[i].key);
                }
            }

            first = false;
        }
    }

    /* Close JSON object */
    if (offset < buffer_size) {
        json_buffer[offset++] = '}';
    } else {
        goto buffer_overflow;
    }

    /* Null terminate */
    if (offset < buffer_size) {
        json_buffer[offset] = '\0';
    } else {
        json_buffer[buffer_size - 1] = '\0';
    }

    return ESP_OK;

buffer_overflow:
    ESP_LOGE(TAG, "Bundle retrieval failed: Buffer overflow during JSON building");
    return ESP_ERR_NO_MEM;
}
