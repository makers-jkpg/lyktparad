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
static uint8_t rgb_effect_counter = 0;  /* Local heartbeat counter (0-255, wraps) */
static esp_timer_handle_t rgb_effect_timer = NULL;  /* Local timer for drift correction */
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
 * Calculates color index from counter (modulo 6) and sets RGB LED.
 */
static void rgb_effect_update_color(void)
{
    uint8_t color_index = rgb_effect_counter % 6;
    uint32_t color = rgb_effect_colors[color_index];
    uint8_t r, g, b;
    extract_rgb(color, &r, &g, &b);
    plugin_set_rgb(r, g, b);
}

/*******************************************************
 *                Timer Management
 *******************************************************/

/**
 * @brief Timer callback to increment counter and update color
 *
 * This callback increments the local heartbeat counter every
 * MESH_CONFIG_HEARTBEAT_INTERVAL milliseconds and updates the RGB LED.
 * When a heartbeat is received from the root node, drift is corrected
 * and the timer is reset.
 */
static void rgb_effect_timer_callback(void *arg)
{
    (void)arg;

    /* Check if plugin is active (double-check for safety) */
    if (!plugin_is_active("rgb_effect")) {
        ESP_LOGW(TAG, "RGB effect timer callback called but plugin is not active, stopping timer");
        if (rgb_effect_timer != NULL) {
            esp_timer_stop(rgb_effect_timer);
        }
        return;
    }

    /* Increment counter (wraps at 255 automatically) */
    rgb_effect_counter++;

    /* Update color based on counter */
    rgb_effect_update_color();

    ESP_LOGD(TAG, "Local timer tick - counter: %u, color_index: %u", rgb_effect_counter, rgb_effect_counter % 6);
}

/**
 * @brief Start local timer for drift correction
 *
 * Creates and starts a periodic timer that increments the counter
 * every MESH_CONFIG_HEARTBEAT_INTERVAL milliseconds.
 *
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t rgb_effect_timer_start(void)
{
    if (rgb_effect_timer != NULL) {
        ESP_LOGD(TAG, "Timer already created");
        /* Restart timer if it exists */
        esp_timer_stop(rgb_effect_timer);
        esp_err_t err = esp_timer_start_periodic(rgb_effect_timer, (uint64_t)MESH_CONFIG_HEARTBEAT_INTERVAL * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart timer: %s", esp_err_to_name(err));
            return err;
        }
        return ESP_OK;
    }

    esp_timer_create_args_t args = {
        .callback = &rgb_effect_timer_callback,
        .arg = NULL,
        .name = "rgb_effect_timer",
    };

    esp_err_t err = esp_timer_create(&args, &rgb_effect_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        rgb_effect_timer = NULL;
        return err;
    }

    err = esp_timer_start_periodic(rgb_effect_timer, (uint64_t)MESH_CONFIG_HEARTBEAT_INTERVAL * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
        esp_timer_delete(rgb_effect_timer);
        rgb_effect_timer = NULL;
        return err;
    }

    ESP_LOGI(TAG, "RGB effect timer started with interval %dms", MESH_CONFIG_HEARTBEAT_INTERVAL);
    return ESP_OK;
}

/**
 * @brief Stop local timer and reset state
 *
 * Stops and deletes the timer, resets counter and running state.
 *
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t rgb_effect_timer_stop(void)
{
    if (rgb_effect_timer != NULL) {
        esp_timer_stop(rgb_effect_timer);
        esp_timer_delete(rgb_effect_timer);
        rgb_effect_timer = NULL;
    }

    rgb_effect_counter = 0;
    rgb_effect_running = false;

    ESP_LOGI(TAG, "RGB effect timer stopped and state cleared");
    return ESP_OK;
}

/*******************************************************
 *                Heartbeat Handler
 *******************************************************/

esp_err_t rgb_effect_plugin_handle_heartbeat(uint8_t pointer, uint8_t counter)
{
    (void)pointer;  /* Pointer is unused for this plugin, kept for sequence plugin compatibility */

    /* Only process heartbeat if plugin is active */
    if (!plugin_is_active("rgb_effect")) {
        ESP_LOGD(TAG, "Heartbeat received but RGB effect plugin not active, ignoring");
        return ESP_OK;
    }

    /* Store old counter for drift logging */
    uint8_t old_counter = rgb_effect_counter;

    /* Calculate drift: difference between local counter and received counter */
    int8_t drift = (int8_t)(rgb_effect_counter - counter);

    /* Handle wrap-around: if drift is large (e.g., 127), counter may have wrapped */
    /* For simplicity, we trust the received counter and update local counter */
    rgb_effect_counter = counter;

    /* Reset local timer to synchronize with root node */
    /* Stop timer before restarting to reset internal timing */
    if (rgb_effect_timer != NULL) {
        esp_timer_stop(rgb_effect_timer);
        esp_err_t err = esp_timer_start_periodic(rgb_effect_timer, (uint64_t)MESH_CONFIG_HEARTBEAT_INTERVAL * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart timer after heartbeat: %s", esp_err_to_name(err));
            return err;
        }
    }

    /* Update color based on corrected counter */
    rgb_effect_update_color();

    /* Log drift correction if significant (more than 1) */
    if (drift > 1 || drift < -1) {
        ESP_LOGW(TAG, "Drift correction: local=%u, received=%u, drift=%d",
                 old_counter, counter, drift);
    } else {
        ESP_LOGD(TAG, "Heartbeat received - counter: %u, color_index: %u", counter, counter % 6);
    }

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
    /* Initialize timer */
    return rgb_effect_timer_start();
}

static esp_err_t rgb_effect_deinit(void)
{
    /* Stop and cleanup */
    return rgb_effect_timer_stop();
}

static esp_err_t rgb_effect_on_activate(void)
{
    /* Initialize counter to 0 */
    rgb_effect_counter = 0;

    /* Start local timer */
    esp_err_t err = rgb_effect_timer_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer on activation: %s", esp_err_to_name(err));
        return err;
    }

    /* Set initial color (index 0, red) */
    rgb_effect_update_color();

    /* Set running flag */
    rgb_effect_running = true;

    ESP_LOGI(TAG, "RGB effect plugin activated - counter: %u", rgb_effect_counter);
    return ESP_OK;
}

static esp_err_t rgb_effect_on_deactivate(void)
{
    /* Stop local timer */
    esp_err_t err = rgb_effect_timer_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop timer on deactivation: %s", esp_err_to_name(err));
    }

    /* Reset RGB LED to off */
    plugin_set_rgb(0, 0, 0);

    /* Counter and running flag are reset in timer_stop() */

    ESP_LOGI(TAG, "RGB effect plugin deactivated");
    return ESP_OK;
}

static esp_err_t rgb_effect_on_pause(void)
{
    /* Stop local timer */
    if (rgb_effect_timer != NULL) {
        esp_timer_stop(rgb_effect_timer);
    }

    /* Preserve counter and color state for resume */
    /* Keep running flag (for resume) */

    ESP_LOGI(TAG, "RGB effect plugin paused - counter: %u", rgb_effect_counter);
    return ESP_OK;
}

static esp_err_t rgb_effect_on_reset(void)
{
    /* Reset counter to 0 */
    rgb_effect_counter = 0;

    /* Stop and restart timer */
    if (rgb_effect_timer != NULL) {
        esp_timer_stop(rgb_effect_timer);
        esp_err_t err = esp_timer_start_periodic(rgb_effect_timer, (uint64_t)MESH_CONFIG_HEARTBEAT_INTERVAL * 1000ULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart timer on reset: %s", esp_err_to_name(err));
            return err;
        }
    }

    /* Set color to index 0 (red) */
    rgb_effect_update_color();

    ESP_LOGI(TAG, "RGB effect plugin reset - counter: %u", rgb_effect_counter);
    return ESP_OK;
}

static esp_err_t rgb_effect_on_start(void)
{
    /* Resume from pause or restart effect */
    if (rgb_effect_running) {
        /* Restart timer if paused */
        if (rgb_effect_timer != NULL) {
            esp_timer_stop(rgb_effect_timer);
            esp_err_t err = esp_timer_start_periodic(rgb_effect_timer, (uint64_t)MESH_CONFIG_HEARTBEAT_INTERVAL * 1000ULL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restart timer on start: %s", esp_err_to_name(err));
                return err;
            }
        }
    } else {
        /* Start from beginning (similar to activate) */
        return rgb_effect_on_activate();
    }

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
        .command_id = 0, /* Will be assigned by plugin system */
        .callbacks = {
            .command_handler = rgb_effect_command_handler,
            .timer_callback = rgb_effect_timer_callback,
            .init = rgb_effect_init,
            .deinit = rgb_effect_deinit,
            .is_active = rgb_effect_is_active,
            .on_activate = rgb_effect_on_activate,
            .on_deactivate = rgb_effect_on_deactivate,
            .on_start = rgb_effect_on_start,
            .on_pause = rgb_effect_on_pause,
            .on_reset = rgb_effect_on_reset,
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
