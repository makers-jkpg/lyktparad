/* Effect Strobe Plugin Implementation
 *
 * This module implements strobe effect functionality as a plugin.
 * The strobe effect automatically starts when the plugin is activated.
 *
 * Copyright (c) 2025 Arvind
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "effect_strobe_plugin.h"
#include "plugin_system.h"
#include "plugin_light.h"
#include "config/mesh_device_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "effect_strobe_plugin";

/* Plugin ID storage (assigned during registration) */
static uint8_t strobe_plugin_id = 0;

/* Hardcoded default strobe parameters */
static const struct {
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
    uint16_t duration_on_ms;
    uint16_t duration_off_ms;
} strobe_defaults = {
    .r_on = 255, .g_on = 255, .b_on = 255,  /* White */
    .r_off = 0, .g_off = 0, .b_off = 0,      /* Black */
    .duration_on_ms = 100,
    .duration_off_ms = 100
};

/* State variables */
static esp_timer_handle_t strobe_timer = NULL;
static bool strobe_is_on = false;
static bool strobe_running = false;
static bool strobe_paused = false;

/* Forward declarations */
static void strobe_timer_callback(void *arg);
static esp_err_t strobe_timer_start(void);
static esp_err_t strobe_timer_stop(void);
static esp_err_t strobe_start(void);
static esp_err_t strobe_stop(void);

/*******************************************************
 *                Timer Management
 *******************************************************/

static esp_err_t strobe_timer_start(void)
{
    if (strobe_timer != NULL) {
        ESP_LOGD(TAG, "Timer already created");
        return ESP_OK;
    }

    esp_timer_create_args_t args = {
        .callback = &strobe_timer_callback,
        .arg = NULL,
        .name = "effect_strobe_timer",
    };

    esp_err_t err = esp_timer_create(&args, &strobe_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        strobe_timer = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Strobe timer created");
    return ESP_OK;
}

static esp_err_t strobe_timer_stop(void)
{
    if (strobe_timer != NULL) {
        esp_timer_stop(strobe_timer);
        esp_timer_delete(strobe_timer);
        strobe_timer = NULL;
    }

    strobe_is_on = false;
    strobe_running = false;

    ESP_LOGI(TAG, "Strobe timer stopped and state cleared");
    return ESP_OK;
}

/*******************************************************
 *                Timer Callback
 *******************************************************/

static void strobe_timer_callback(void *arg)
{
    /* Check if paused - if so, don't process timer callback */
    if (strobe_paused) {
        return;
    }
    (void)arg;

    /* Check if effect is running (strobe_running is set before timer starts) */
    if (!strobe_running) {
        return;
    }

    /* Check if strobe plugin is active (double-check for safety) */
    bool is_active = plugin_is_active("effect_strobe");
    if (!is_active) {
        ESP_LOGW(TAG, "Strobe timer callback called but plugin is not active, stopping timer");
        strobe_timer_stop();
        return;
    }

    if (!strobe_is_on) {
        /* Turn ON */
        plugin_set_rgb(strobe_defaults.r_on, strobe_defaults.g_on, strobe_defaults.b_on);
        strobe_is_on = true;
        /* Schedule next toggle after duration_on */
        if (strobe_timer != NULL) {
            esp_timer_start_once(strobe_timer, (uint64_t)strobe_defaults.duration_on_ms * 1000ULL);
        }
        return;
    } else {
        /* Turn OFF */
        plugin_set_rgb(strobe_defaults.r_off, strobe_defaults.g_off, strobe_defaults.b_off);
        strobe_is_on = false;
        /* Schedule next toggle after duration_off */
        if (strobe_timer != NULL) {
            esp_timer_start_once(strobe_timer, (uint64_t)strobe_defaults.duration_off_ms * 1000ULL);
        }
        return;
    }
}

/*******************************************************
 *                Effect Control
 *******************************************************/

static esp_err_t strobe_start(void)
{
    if (strobe_running && !strobe_paused) {
        ESP_LOGD(TAG, "Strobe effect already running");
        return ESP_OK;
    }

    /* Initialize state */
    strobe_is_on = false;
    strobe_running = true;
    strobe_paused = false; /* Clear pause flag when starting */

    /* Ensure timer exists */
    esp_err_t timer_create_err = strobe_timer_start();
    if (timer_create_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create/start strobe timer");
        strobe_running = false;
        return ESP_FAIL;
    }

    /* Start immediately */
    esp_timer_start_once(strobe_timer, 1);

    ESP_LOGI(TAG, "Strobe effect started: on(%d,%d,%d) off(%d,%d,%d) on_ms=%u off_ms=%u",
             strobe_defaults.r_on, strobe_defaults.g_on, strobe_defaults.b_on,
             strobe_defaults.r_off, strobe_defaults.g_off, strobe_defaults.b_off,
             strobe_defaults.duration_on_ms, strobe_defaults.duration_off_ms);
    return ESP_OK;
}

static esp_err_t strobe_stop(void)
{
    esp_err_t err = strobe_timer_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop strobe timer: %s", esp_err_to_name(err));
    }

    /* Set LED to off */
    plugin_set_rgb(0, 0, 0);

    ESP_LOGI(TAG, "Strobe effect stopped");
    return ESP_OK;
}

/*******************************************************
 *                Plugin Callbacks
 *******************************************************/

static esp_err_t effect_strobe_on_pause(void)
{
    if (!strobe_running) {
        ESP_LOGD(TAG, "Strobe effect not running, nothing to pause");
        return ESP_OK;
    }

    /* Stop timer */
    if (strobe_timer != NULL) {
        esp_timer_stop(strobe_timer);
    }

    /* Set paused flag to prevent timer callback from continuing */
    strobe_paused = true;

    ESP_LOGI(TAG, "Strobe effect paused (is_on=%d)", strobe_is_on);
    return ESP_OK;
}

static esp_err_t effect_strobe_on_reset(void)
{
    /* Stop timer */
    if (strobe_timer != NULL) {
        esp_timer_stop(strobe_timer);
    }

    /* Reset state */
    strobe_is_on = false;
    strobe_running = false;
    strobe_paused = false;

    /* Reset RGB LED to off */
    plugin_set_rgb(0, 0, 0);

    ESP_LOGI(TAG, "Strobe effect reset");
    return ESP_OK;
}

static esp_err_t effect_strobe_on_stop(void)
{
    /* Reset state (calls on_reset logic) */
    esp_err_t err = effect_strobe_on_reset();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Strobe effect stopped");
    return ESP_OK;
}

static esp_err_t strobe_command_handler(uint8_t *data, uint16_t len)
{
    /* Effect auto-starts on activation, command handler is minimal */
    (void)data;
    (void)len;
    return ESP_OK;
}

static bool strobe_is_active(void)
{
    return strobe_running;
}

static esp_err_t strobe_init(void)
{
    /* Initialize timer */
    return strobe_timer_start();
}

static esp_err_t strobe_deinit(void)
{
    /* Stop and cleanup */
    return strobe_timer_stop();
}

static esp_err_t strobe_on_activate(void)
{
    /* Auto-start strobe effect */
    ESP_LOGD(TAG, "Effect strobe plugin activated, starting strobe effect");
    return strobe_start();
}

static esp_err_t strobe_on_deactivate(void)
{
    /* Stop strobe effect */
    ESP_LOGD(TAG, "Effect strobe plugin deactivated, stopping strobe effect");
    return strobe_stop();
}

static esp_err_t strobe_on_start(void)
{
    /* Start strobe effect (strobe_start() is idempotent - safe to call if already running) */
    ESP_LOGD(TAG, "Effect strobe plugin START command received, starting strobe effect");
    return strobe_start();
}

/*******************************************************
 *                Plugin Registration
 *******************************************************/

void effect_strobe_plugin_register(void)
{
    plugin_info_t info = {
        .name = "effect_strobe",
        .command_id = 0, /* Will be assigned by plugin system */
        .callbacks = {
            .command_handler = strobe_command_handler,
            .timer_callback = strobe_timer_callback,
            .init = strobe_init,
            .deinit = strobe_deinit,
            .is_active = strobe_is_active,
            .on_activate = strobe_on_activate,
            .on_deactivate = strobe_on_deactivate,
            .on_start = strobe_on_start,
            .on_pause = effect_strobe_on_pause,
            .on_reset = effect_strobe_on_reset,
            .on_stop = effect_strobe_on_stop,
        },
        .user_data = NULL,
    };

    uint8_t assigned_cmd_id;
    esp_err_t err = plugin_register(&info, &assigned_cmd_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register effect_strobe plugin: %s", esp_err_to_name(err));
    } else {
        strobe_plugin_id = assigned_cmd_id;
        ESP_LOGI(TAG, "Effect strobe plugin registered with plugin ID 0x%02X", strobe_plugin_id);
    }
}
