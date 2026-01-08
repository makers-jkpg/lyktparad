/* RGB Effect Plugin Implementation
 *
 * This module implements RGB color cycling effect functionality as a plugin.
 * The effect cycles through 6 colors (red, yellow, green, cyan, blue, magenta)
 * synchronized across all mesh nodes using heartbeat counter mechanism.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "rgb_effect_plugin.h"
#include "plugin_system.h"
#include "plugin_web_ui.h"
#include "plugin_light.h"
#include "mesh_commands.h"
#include "mesh_common.h"
#include "config/mesh_config.h"
#include "config/mesh_device_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "rgb_effect_plugin";

/* Plugin ID storage (assigned during registration) */
static uint8_t rgb_effect_plugin_id = 0;

/* Color array: red, yellow, green, cyan, blue, magenta */
static const uint32_t rgb_effect_colors[6] = {
    0xff0000,  /* Red */
    0xffff00,  /* Yellow */
    0x00ff00,  /* Green */
    0x00ffff,  /* Cyan */
    0x0000ff,  /* Blue */
    0xff00ff   /* Magenta */
};

/* State variables */
static bool rgb_effect_running = false;  /* Running state flag */

/* Forward declarations */
static void rgb_effect_register_web_ui(void);

/*******************************************************
 *                Helper Functions
 *******************************************************/

/**
 * @brief Extract RGB components from 24-bit color value
 *
 * @param color 24-bit color value (0xRRGGBB format)
 * @param r Output parameter for red component (0-255)
 * @param g Output parameter for green component (0-255)
 * @param b Output parameter for blue component (0-255)
 */
static inline void extract_rgb(uint32_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)((color >> 16) & 0xFF);
    *g = (uint8_t)((color >> 8) & 0xFF);
    *b = (uint8_t)(color & 0xFF);
}

/**
 * @brief Update RGB LED based on current counter value
 *
 * Calculates color index from core local heartbeat counter (modulo 6) and sets RGB LED.
 * Uses mesh_common_get_local_heartbeat_counter() to get the current counter value.
 */
static void rgb_effect_update_color(void)
{
    uint8_t counter = mesh_common_get_local_heartbeat_counter();
    uint8_t color_index = counter % 6;
    uint32_t color = rgb_effect_colors[color_index];
    uint8_t r, g, b;
    extract_rgb(color, &r, &g, &b);
    plugin_set_rgb(r, g, b);
}

/*******************************************************
 *                Heartbeat Handler
 *******************************************************/

esp_err_t rgb_effect_plugin_handle_heartbeat(uint8_t pointer, uint8_t counter)
{
    (void)pointer;  /* Pointer is unused for this plugin, kept for sequence plugin compatibility */
    (void)counter;  /* Counter is now managed by core, we use mesh_common_get_local_heartbeat_counter() */

    /* Only process heartbeat if plugin is active */
    if (!plugin_is_active("rgb_effect")) {
        ESP_LOGD(TAG, "Heartbeat received but RGB effect plugin not active, ignoring");
        return ESP_OK;
    }

    /* Update color based on core local heartbeat counter (core handles synchronization) */
    rgb_effect_update_color();

    uint8_t current_counter = mesh_common_get_local_heartbeat_counter();
    ESP_LOGD(TAG, "Heartbeat received - counter: %u, color_index: %u", current_counter, current_counter % 6);

    return ESP_OK;
}

/*******************************************************
 *                Plugin Callbacks
 *******************************************************/

static esp_err_t rgb_effect_command_handler(uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 1) {
        ESP_LOGE(TAG, "Invalid command data");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd = data[0];

    /* Handle PLUGIN_CMD_DATA (0x04) - raw bytes RGB values */
    if (cmd == PLUGIN_CMD_DATA) {
        /* Data format: [R:1][G:1][B:1] (3 bytes) */
        if (len < 4) {  /* cmd byte (1) + R (1) + G (1) + B (1) = 4 bytes */
            ESP_LOGE(TAG, "PLUGIN_CMD_DATA: Invalid length (%d, expected 4)", len);
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t r = data[1];
        uint8_t g = data[2];
        uint8_t b = data[3];

        /* Set RGB LED directly */
        plugin_set_rgb(r, g, b);

        ESP_LOGI(TAG, "RGB set via web UI: R=%d G=%d B=%d", r, g, b);
        return ESP_OK;
    }

    /* Other commands handled by dedicated callbacks */
    return ESP_OK;
}

static bool rgb_effect_is_active(void)
{
    return rgb_effect_running;
}

static esp_err_t rgb_effect_init(void)
{
    /* No initialization needed - core heartbeat counter handles timing */

    /* Register web UI callbacks */
    rgb_effect_register_web_ui();

    return ESP_OK;
}

static esp_err_t rgb_effect_deinit(void)
{
    /* No cleanup needed - core heartbeat counter handles timing */
    return ESP_OK;
}

static esp_err_t rgb_effect_on_activate(void)
{
    /* Set initial color based on current core counter */
    rgb_effect_update_color();

    /* Set running flag */
    rgb_effect_running = true;

    uint8_t counter = mesh_common_get_local_heartbeat_counter();
    ESP_LOGI(TAG, "RGB effect plugin activated - counter: %u", counter);
    return ESP_OK;
}

static esp_err_t rgb_effect_on_deactivate(void)
{
    /* Reset RGB LED to off */
    plugin_set_rgb(0, 0, 0);

    /* Reset running flag */
    rgb_effect_running = false;

    ESP_LOGI(TAG, "RGB effect plugin deactivated");
    return ESP_OK;
}

static esp_err_t rgb_effect_on_pause(void)
{
    /* Preserve color state for resume (core counter continues running) */
    /* Keep running flag (for resume) */

    uint8_t counter = mesh_common_get_local_heartbeat_counter();
    ESP_LOGI(TAG, "RGB effect plugin paused - counter: %u", counter);
    return ESP_OK;
}

static esp_err_t rgb_effect_on_reset(void)
{
    /* Update color based on current core counter (core counter continues running) */
    rgb_effect_update_color();

    uint8_t counter = mesh_common_get_local_heartbeat_counter();
    ESP_LOGI(TAG, "RGB effect plugin reset - counter: %u", counter);
    return ESP_OK;
}

static esp_err_t rgb_effect_on_stop(void)
{
    /* Reset RGB LED to off */
    plugin_set_rgb(0, 0, 0);

    /* Reset running flag */
    rgb_effect_running = false;

    ESP_LOGI(TAG, "RGB effect plugin stopped");
    return ESP_OK;
}

static esp_err_t rgb_effect_on_start(void)
{
    /* Resume from pause or restart effect */
    if (!rgb_effect_running) {
        /* Start from beginning (similar to activate) */
        return rgb_effect_on_activate();
    }

    /* Update color based on current core counter */
    rgb_effect_update_color();

    ESP_LOGI(TAG, "RGB effect plugin START command received");
    return ESP_OK;
}

/*******************************************************
 *                Web UI Callbacks
 *******************************************************/

/* HTML content - Flash memory (zero RAM) */
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

static const char *rgb_effect_html(void)
{
    return rgb_effect_html_content;
}

/* CSS content - Flash memory (zero RAM) */
static const char rgb_effect_css_content[] =
    ".plugin-rgb-effect-container {\n"
    "  padding: 20px;\n"
    "  background: #f5f5f5;\n"
    "  border-radius: 8px;\n"
    "  margin: 20px 0;\n"
    "}\n"
    ".plugin-rgb-effect-controls {\n"
    "  display: flex;\n"
    "  flex-direction: column;\n"
    "  gap: 15px;\n"
    "}\n"
    ".plugin-rgb-effect-controls label {\n"
    "  display: flex;\n"
    "  justify-content: space-between;\n"
    "  align-items: center;\n"
    "  font-weight: 500;\n"
    "}\n"
    ".plugin-rgb-effect-slider {\n"
    "  width: 100%;\n"
    "  height: 8px;\n"
    "  border-radius: 4px;\n"
    "  background: #ddd;\n"
    "  outline: none;\n"
    "  -webkit-appearance: none;\n"
    "}\n"
    ".plugin-rgb-effect-slider::-webkit-slider-thumb {\n"
    "  -webkit-appearance: none;\n"
    "  appearance: none;\n"
    "  width: 20px;\n"
    "  height: 20px;\n"
    "  border-radius: 50%;\n"
    "  background: #667eea;\n"
    "  cursor: pointer;\n"
    "}\n"
    ".plugin-rgb-effect-slider::-moz-range-thumb {\n"
    "  width: 20px;\n"
    "  height: 20px;\n"
    "  border-radius: 50%;\n"
    "  background: #667eea;\n"
    "  cursor: pointer;\n"
    "  border: none;\n"
    "}\n";

static const char *rgb_effect_css(void)
{
    return rgb_effect_css_content;
}

/* JavaScript content - Flash memory (zero RAM) */
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

static const char *rgb_effect_js(void)
{
    return rgb_effect_js_content;
}

/**
 * @brief Register web UI callbacks for RGB effect plugin
 *
 * This function registers web UI callbacks that provide HTML, CSS, and JavaScript
 * content for the RGB effect plugin's web interface. All content is stored in
 * Flash memory (static const char*) for zero-RAM optimization.
 */
static void rgb_effect_register_web_ui(void)
{
    plugin_web_ui_callbacks_t callbacks = {
        .html_callback = rgb_effect_html,
        .js_callback = rgb_effect_js,
        .css_callback = rgb_effect_css,
        .dynamic_mask = 0x00  /* All Flash (static) - no bits set */
    };

    esp_err_t err = plugin_register_web_ui("rgb_effect", &callbacks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register web UI: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Web UI registered for RGB effect plugin");
    }
}

/*******************************************************
 *                Plugin Registration
 *******************************************************/

void rgb_effect_plugin_register(void)
{
    plugin_info_t info = {
        .name = "rgb_effect",
        .is_default = true,  /* rgb_effect is the default plugin */
        .command_id = 0, /* Will be assigned by plugin system */
        .callbacks = {
            .command_handler = rgb_effect_command_handler,
            .timer_callback = NULL,  /* No timer callback - uses core heartbeat counter */
            .heartbeat_handler = rgb_effect_plugin_handle_heartbeat,
            .init = rgb_effect_init,
            .deinit = rgb_effect_deinit,
            .is_active = rgb_effect_is_active,
            .on_activate = rgb_effect_on_activate,
            .on_deactivate = rgb_effect_on_deactivate,
            .on_start = rgb_effect_on_start,
            .on_pause = rgb_effect_on_pause,
            .on_reset = rgb_effect_on_reset,
            .on_stop = rgb_effect_on_stop,
        },
        .user_data = NULL,
    };

    uint8_t assigned_cmd_id;
    esp_err_t err = plugin_register(&info, &assigned_cmd_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register rgb_effect plugin: %s", esp_err_to_name(err));
    } else {
        rgb_effect_plugin_id = assigned_cmd_id;
        ESP_LOGI(TAG, "RGB effect plugin registered with plugin ID 0x%02X", rgb_effect_plugin_id);
    }
}
