/* Plugin Web UI Header
 *
 * This module provides infrastructure for plugins to register web UI callbacks
 * that provide HTML, CSS, and JavaScript content dynamically. The system uses
 * bit-masked flags to distinguish Flash (static, zero-RAM) from Heap (dynamic)
 * content, supports JSON bundle building with dry-run mode, and integrates
 * seamlessly with the existing plugin system while maintaining backward compatibility.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef PLUGIN_WEB_UI_H
#define PLUGIN_WEB_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Bit-mask flags for distinguishing Flash (Static) vs Heap (Dynamic) content.
 *
 * If a bit is set to 0, the pointer is assumed to be in Flash (RODATA).
 * If a bit is set to 1, the pointer is assumed to be in Heap and will be free()'d after use.
 */
#define PLUGIN_WEB_HTML_DYNAMIC  (1 << 0)  /**< HTML content is dynamic (Heap) */
#define PLUGIN_WEB_JS_DYNAMIC    (1 << 1)  /**< JavaScript content is dynamic (Heap) */
#define PLUGIN_WEB_CSS_DYNAMIC   (1 << 2)  /**< CSS content is dynamic (Heap) */

/**
 * @brief Callback function type for providing web content.
 *
 * This callback is invoked to retrieve HTML, JavaScript, or CSS content for a plugin.
 * The callback should return a pointer to the content string.
 *
 * @return const char* Pointer to the string content (HTML, JS, or CSS).
 *                     - Non-NULL: Content is available (Flash or Heap pointer)
 *                     - NULL: Content is unavailable (omitted from bundle)
 *
 * @note For Flash content: Return pointer to static const string in .rodata section.
 * @note For Heap content: Allocate string via malloc/asprintf, return pointer.
 *       The system will free() Heap content after use (if dynamic_mask bit is set).
 * @note Content must remain valid during HTTP response.
 */
typedef const char *(*plugin_web_content_callback_t)(void);

/**
 * @brief Structure containing web UI callbacks and memory management flags.
 *
 * This structure defines the callbacks that provide HTML, CSS, and JavaScript
 * content for a plugin's web UI. All callbacks are optional (may be NULL).
 * The dynamic_mask field indicates which callbacks return Heap (dynamic) content
 * that must be freed after use.
 */
typedef struct plugin_web_ui_callbacks_s {
    /**
     * @brief HTML content callback (optional, may be NULL)
     *
     * Returns HTML content for the plugin's web UI.
     */
    plugin_web_content_callback_t html_callback;

    /**
     * @brief JavaScript content callback (optional, may be NULL)
     *
     * Returns JavaScript code for the plugin's web UI.
     */
    plugin_web_content_callback_t js_callback;

    /**
     * @brief CSS content callback (optional, may be NULL)
     *
     * Returns CSS styles for the plugin's web UI.
     */
    plugin_web_content_callback_t css_callback;

    /**
     * @brief Bit-masked flags indicating which callbacks return dynamic (Heap) content.
     *
     * Bit set (1) = Heap (dynamic, must free after use)
     * Bit clear (0) = Flash (static, permanent, don't free)
     *
     * Example: If HTML is dynamic and others are static:
     *   dynamic_mask = PLUGIN_WEB_HTML_DYNAMIC;  // 0x01
     */
    uint8_t dynamic_mask;
} plugin_web_ui_callbacks_t;

/**
 * @brief Registers web UI callbacks for a specific plugin.
 *
 * This function registers web UI callbacks for a plugin that has already been
 * registered with the plugin system. The callbacks are stored in the plugin's
 * info structure and can be retrieved later for building JSON bundles.
 *
 * @param name The unique name of the plugin (must be registered via plugin_register()).
 * @param callbacks Pointer to the callback structure. May be NULL (no web UI).
 *                  Individual callbacks within the structure may also be NULL.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if name or callbacks is NULL
 * @return ESP_ERR_NOT_FOUND if plugin with name is not registered
 * @return ESP_ERR_NO_MEM if memory allocation fails
 *
 * @note Registration happens after plugin registration (plugin must exist first).
 * @note If web UI is already registered for the plugin, it will be overwritten.
 * @note The callbacks structure is copied to allocated memory, so the original
 *       structure can be freed after registration.
 */
esp_err_t plugin_register_web_ui(const char *name, const plugin_web_ui_callbacks_t *callbacks);

/**
 * @brief Builds a JSON bundle for a specific plugin.
 *
 * This function builds a JSON object containing HTML, JavaScript, and CSS content
 * for a plugin's web UI. The function supports dry-run mode (when json_buffer is NULL)
 * to calculate the required buffer size without building the JSON string.
 *
 * JSON Format: {"html": "...", "js": "...", "css": "..."}
 * - NULL callbacks are omitted from the JSON object
 * - Content is JSON-escaped (quotes, backslashes, newlines)
 * - Carriage returns are stripped
 *
 * @param name The unique name of the plugin (must be registered).
 * @param json_buffer Buffer to store the resulting JSON string.
 *                    - Non-NULL: Build JSON and write to buffer
 *                    - NULL: Dry-run mode, calculate required size only
 * @param buffer_size Size of the provided buffer (ignored if json_buffer is NULL).
 * @param required_size Output parameter for required buffer size (non-NULL).
 *                      Set to the size needed for the JSON string (including null terminator).
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if name or required_size is NULL
 * @return ESP_ERR_NOT_FOUND if plugin is not registered or has no web UI callbacks
 * @return ESP_ERR_NO_MEM if buffer is too small (when json_buffer is not NULL)
 *
 * @note Dry-run mode: When json_buffer is NULL, function calculates required_size
 *       without building JSON. This requires invoking callbacks twice (once for
 *       dry-run, once for actual), which is acceptable since callbacks should be fast.
 * @note Dynamic content (Heap) is freed after copying to JSON buffer.
 * @note Flash content is never freed (memory guard prevents accidental free).
 */
esp_err_t plugin_get_web_bundle(const char *name, char *json_buffer, size_t buffer_size, size_t *required_size);

#endif // PLUGIN_WEB_UI_H
