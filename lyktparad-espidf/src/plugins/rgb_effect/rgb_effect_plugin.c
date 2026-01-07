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
#include "plugin_light.h"
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
    /* RGB effect auto-starts on activation, command handler is minimal */
    (void)data;
    (void)len;
    return ESP_OK;
}

static bool rgb_effect_is_active(void)
{
    return rgb_effect_running;
}

static esp_err_t rgb_effect_init(void)
{
    /* No initialization needed - core heartbeat counter handles timing */
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
