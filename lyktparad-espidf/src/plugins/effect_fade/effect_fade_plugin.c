/* Effect Fade Plugin Implementation
 *
 * This module implements fade effect functionality as a plugin.
 * The fade effect automatically starts when the plugin is activated.
 *
 * Copyright (c) 2025 Arvind
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "effect_fade_plugin.h"
#include "plugin_system.h"
#include "plugin_light.h"
#include "mesh_common.h"
#include "config/mesh_config.h"
#include "config/mesh_device_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "effect_fade_plugin";

/* Plugin ID storage (assigned during registration) */
static uint8_t fade_plugin_id = 0;

/* Hardcoded default fade parameters - total cycle = 1000ms (1 heartbeat interval) */
static const struct {
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
    uint16_t fade_in_ms;
    uint16_t fade_out_ms;
    uint16_t hold_ms;
} fade_defaults = {
    .r_on = 255, .g_on = 255, .b_on = 255,  /* White */
    .r_off = 0, .g_off = 0, .b_off = 0,      /* Black */
    .fade_in_ms = 400,  /* fade_in: 400ms */
    .fade_out_ms = 400, /* fade_out: 400ms */
    .hold_ms = 200      /* hold: 200ms (total = 1000ms) */
};

/* State variables */
static esp_timer_handle_t fade_timer = NULL;
static uint8_t fade_last_counter = 0; /* Last counter value seen */
static int64_t fade_cycle_start_us = 0; /* Cycle start time in microseconds */
static bool fade_running = false;
static bool fade_paused = false;
static const uint32_t fade_update_interval_ms = 20; /* Update interval for smooth interpolation */

/* Forward declarations */
static void fade_timer_callback(void *arg);
static esp_err_t fade_timer_start(void);
static esp_err_t fade_timer_stop(void);
static esp_err_t fade_start(void);
static esp_err_t fade_stop(void);

/*******************************************************
 *                Helper Functions
 *******************************************************/

static inline uint8_t interp_u8(uint8_t start, uint8_t end, uint32_t elapsed, uint32_t total)
{
    if (total == 0) return end;
    if (elapsed >= total) return end;
    uint32_t s = start;
    uint32_t e = end;
    return (uint8_t)((s * (total - elapsed) + e * elapsed) / total);
}

/*******************************************************
 *                Timer Management
 *******************************************************/

static esp_err_t fade_timer_start(void)
{
    if (fade_timer != NULL) {
        /* Timer already exists, start periodic timer with update interval for smooth interpolation */
        esp_err_t err = esp_timer_start_periodic(fade_timer, (uint64_t)fade_update_interval_ms * 1000ULL);
        if (err == ESP_OK) {
            ESP_LOGD(TAG, "Fade timer started (periodic, %dms)", fade_update_interval_ms);
        } else if (err == ESP_ERR_INVALID_STATE) {
            /* Timer already running, that's okay */
            ESP_LOGD(TAG, "Fade timer already running");
        } else {
            ESP_LOGE(TAG, "Failed to start fade timer: %s", esp_err_to_name(err));
            return err;
        }
        /* Reinitialize cycle start time and counter when restarting existing timer */
        fade_cycle_start_us = esp_timer_get_time();
        fade_last_counter = mesh_common_get_local_heartbeat_counter();
        return ESP_OK;
    }

    esp_timer_create_args_t args = {
        .callback = &fade_timer_callback,
        .arg = NULL,
        .name = "effect_fade_timer",
    };

    esp_err_t err = esp_timer_create(&args, &fade_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        fade_timer = NULL;
        return err;
    }

    err = esp_timer_start_periodic(fade_timer, (uint64_t)fade_update_interval_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start fade timer: %s", esp_err_to_name(err));
        esp_timer_delete(fade_timer);
        fade_timer = NULL;
        return err;
    }

    /* Initialize cycle start time */
    fade_cycle_start_us = esp_timer_get_time();
    fade_last_counter = mesh_common_get_local_heartbeat_counter();

    ESP_LOGI(TAG, "Fade timer created and started (periodic, %dms, synchronized to heartbeat)", fade_update_interval_ms);
    return ESP_OK;
}

static esp_err_t fade_timer_stop(void)
{
    if (fade_timer != NULL) {
        esp_timer_stop(fade_timer);
        /* Don't delete timer - keep it for reuse */
    }

    fade_last_counter = 0;
    fade_cycle_start_us = 0;
    fade_running = false;

    ESP_LOGI(TAG, "Fade timer stopped");
    return ESP_OK;
}

/*******************************************************
 *                Timer Callback
 *******************************************************/

/**
 * @brief Timer callback synchronized to heartbeat counter
 *
 * This callback fires every fade_update_interval_ms (20ms) when the plugin is active.
 * It reads the synchronized local heartbeat counter to determine the fade cycle progress.
 * Each complete fade cycle (fade_in + hold + fade_out) takes exactly 1 heartbeat interval (1000ms).
 * The counter is used to synchronize cycles across all nodes, while the timer provides smooth interpolation.
 */
static void fade_timer_callback(void *arg)
{
    (void)arg;

    /* Check if paused - if so, don't process timer callback */
    if (fade_paused) {
        return;
    }

    /* Check if effect is running */
    if (!fade_running) {
        return;
    }

    /* Check if fade plugin is active (double-check for safety) */
    if (!plugin_is_active("effect_fade")) {
        ESP_LOGW(TAG, "Fade timer callback called but plugin is not active, stopping timer");
        fade_timer_stop();
        return;
    }

    /* Read synchronized local heartbeat counter */
    uint8_t counter = mesh_common_get_local_heartbeat_counter();

    /* Check if counter has changed (new cycle started or synchronized after mesh reconnect) */
    /* Counter normally increments by 1 each heartbeat interval, but may jump when synchronized */
    if (counter != fade_last_counter) {
        /* Counter changed - new cycle started or resynchronized, reset cycle start time */
        fade_cycle_start_us = esp_timer_get_time();
        fade_last_counter = counter;
    }

    /* Calculate elapsed time since cycle start */
    int64_t current_time_us = esp_timer_get_time();
    int64_t elapsed_us = current_time_us - fade_cycle_start_us;
    uint32_t cycle_progress_ms = (uint32_t)(elapsed_us / 1000ULL);

    /* Wrap cycle progress to heartbeat interval (1000ms) */
    if (cycle_progress_ms >= MESH_CONFIG_HEARTBEAT_INTERVAL) {
        /* Cycle complete, reset to start of cycle */
        cycle_progress_ms = cycle_progress_ms % MESH_CONFIG_HEARTBEAT_INTERVAL;
        fade_cycle_start_us = current_time_us - (int64_t)cycle_progress_ms * 1000ULL;
    }

    /* Determine phase and calculate RGB values */
    uint8_t r, g, b;

    if (cycle_progress_ms < fade_defaults.fade_in_ms) {
        /* Phase 1: fade_in (on -> off) */
        uint32_t elapsed = cycle_progress_ms;
        uint32_t total = fade_defaults.fade_in_ms;
        r = interp_u8(fade_defaults.r_on, fade_defaults.r_off, elapsed, total);
        g = interp_u8(fade_defaults.g_on, fade_defaults.g_off, elapsed, total);
        b = interp_u8(fade_defaults.b_on, fade_defaults.b_off, elapsed, total);
    } else if (cycle_progress_ms < fade_defaults.fade_in_ms + fade_defaults.hold_ms) {
        /* Phase 2: hold (off) */
        r = fade_defaults.r_off;
        g = fade_defaults.g_off;
        b = fade_defaults.b_off;
    } else {
        /* Phase 3: fade_out (off -> on) */
        uint32_t elapsed = cycle_progress_ms - fade_defaults.fade_in_ms - fade_defaults.hold_ms;
        uint32_t total = fade_defaults.fade_out_ms;
        r = interp_u8(fade_defaults.r_off, fade_defaults.r_on, elapsed, total);
        g = interp_u8(fade_defaults.g_off, fade_defaults.g_on, elapsed, total);
        b = interp_u8(fade_defaults.b_off, fade_defaults.b_on, elapsed, total);
    }

    /* Update RGB LED */
    plugin_set_rgb(r, g, b);
}

/*******************************************************
 *                Effect Control
 *******************************************************/

static esp_err_t fade_start(void)
{
    if (fade_running && !fade_paused) {
        ESP_LOGD(TAG, "Fade effect already running");
        return ESP_OK;
    }

    /* Initialize state */
    fade_running = true;
    fade_paused = false; /* Clear pause flag when starting */

    /* Ensure timer exists and is started (this also initializes cycle start time and counter) */
    if (fade_timer_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create/start fade timer");
        fade_running = false;
        return ESP_FAIL;
    }

    /* Set initial color to 'on' values (start of fade_in phase) */
    plugin_set_rgb(fade_defaults.r_on, fade_defaults.g_on, fade_defaults.b_on);

    ESP_LOGI(TAG, "Fade effect started: on(%d,%d,%d) off(%d,%d,%d) in_ms=%u out_ms=%u hold_ms=%u (cycle=%ums)",
             fade_defaults.r_on, fade_defaults.g_on, fade_defaults.b_on,
             fade_defaults.r_off, fade_defaults.g_off, fade_defaults.b_off,
             fade_defaults.fade_in_ms, fade_defaults.fade_out_ms, fade_defaults.hold_ms,
             fade_defaults.fade_in_ms + fade_defaults.hold_ms + fade_defaults.fade_out_ms);
    return ESP_OK;
}

static esp_err_t fade_stop(void)
{
    esp_err_t err = fade_timer_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop fade timer: %s", esp_err_to_name(err));
    }

    /* Set LED to off */
    plugin_set_rgb(0, 0, 0);

    ESP_LOGI(TAG, "Fade effect stopped");
    return ESP_OK;
}

/*******************************************************
 *                Plugin Callbacks
 *******************************************************/

static esp_err_t effect_fade_on_pause(void)
{
    if (!fade_running) {
        ESP_LOGD(TAG, "Fade effect not running, nothing to pause");
        return ESP_OK;
    }

    /* Stop timer */
    if (fade_timer != NULL) {
        esp_timer_stop(fade_timer);
    }

    /* Set paused flag to prevent timer callback from continuing */
    fade_paused = true;

    ESP_LOGI(TAG, "Fade effect paused");
    return ESP_OK;
}

static esp_err_t effect_fade_on_reset(void)
{
    /* Stop timer */
    if (fade_timer != NULL) {
        esp_timer_stop(fade_timer);
    }

    /* Reset state */
    fade_last_counter = 0;
    fade_cycle_start_us = 0;
    fade_running = false;
    fade_paused = false;

    /* Reset RGB LED to off */
    plugin_set_rgb(0, 0, 0);

    ESP_LOGI(TAG, "Fade effect reset");
    return ESP_OK;
}

static esp_err_t effect_fade_on_stop(void)
{
    /* Reset state (calls on_reset logic) */
    esp_err_t err = effect_fade_on_reset();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Fade effect stopped");
    return ESP_OK;
}

static esp_err_t fade_command_handler(uint8_t *data, uint16_t len)
{
    /* Effect auto-starts on activation, command handler is minimal */
    (void)data;
    (void)len;
    return ESP_OK;
}

static bool fade_is_active(void)
{
    return fade_running;
}

static esp_err_t fade_init(void)
{
    /* Initialize timer */
    return fade_timer_start();
}

static esp_err_t fade_deinit(void)
{
    /* Stop and cleanup */
    return fade_timer_stop();
}

static esp_err_t fade_on_activate(void)
{
    /* Auto-start fade effect */
    ESP_LOGD(TAG, "Effect fade plugin activated, starting fade effect");
    return fade_start();
}

static esp_err_t fade_on_deactivate(void)
{
    /* Stop fade effect */
    ESP_LOGD(TAG, "Effect fade plugin deactivated, stopping fade effect");
    return fade_stop();
}

static esp_err_t fade_on_start(void)
{
    /* Start fade effect (fade_start() is idempotent - safe to call if already running) */
    ESP_LOGD(TAG, "Effect fade plugin START command received, starting fade effect");
    return fade_start();
}

/*******************************************************
 *                Plugin Registration
 *******************************************************/

void effect_fade_plugin_register(void)
{
    plugin_info_t info = {
        .name = "effect_fade",
        .command_id = 0, /* Will be assigned by plugin system */
        .callbacks = {
            .command_handler = fade_command_handler,
            .timer_callback = fade_timer_callback,
            .init = fade_init,
            .deinit = fade_deinit,
            .is_active = fade_is_active,
            .on_activate = fade_on_activate,
            .on_deactivate = fade_on_deactivate,
            .on_start = fade_on_start,
            .on_pause = effect_fade_on_pause,
            .on_reset = effect_fade_on_reset,
            .on_stop = effect_fade_on_stop,
        },
        .user_data = NULL,
    };

    uint8_t assigned_cmd_id;
    esp_err_t err = plugin_register(&info, &assigned_cmd_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register effect_fade plugin: %s", esp_err_to_name(err));
    } else {
        fade_plugin_id = assigned_cmd_id;
        ESP_LOGI(TAG, "Effect fade plugin registered with plugin ID 0x%02X", fade_plugin_id);
    }
}
