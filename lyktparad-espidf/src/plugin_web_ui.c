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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "PLUGIN_WEB_UI";

/**
 * @brief Internal helper to determine if a pointer resides in Flash (DROM).
 *
 * This acts as a safety check for the dynamic_mask flags. Before attempting
 * to free() a pointer, we verify it's not in Flash to prevent crashes.
 *
 * @param ptr Pointer to check
 * @return true if pointer is in Flash, false if in Heap
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
