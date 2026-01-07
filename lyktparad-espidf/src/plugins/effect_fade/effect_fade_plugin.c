/* Effect Fade Plugin Implementation
 *
 * This module implements fade effect functionality as a plugin.
 * The fade effect automatically starts when the plugin is activated.
 *
 * Copyright (c) 2025 the_louie
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
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "effect_fade_plugin";

/* Plugin ID storage (assigned during registration) */
static uint8_t fade_plugin_id = 0;

/* Hardcoded default fade parameters */
static const struct {
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
    uint16_t fade_in_ms;
    uint16_t fade_out_ms;
    uint16_t hold_ms;
} fade_defaults = {
    .r_on = 255, .g_on = 255, .b_on = 255,  /* White */
    .r_off = 0, .g_off = 0, .b_off = 0,      /* Black */
    .fade_in_ms = 500,
    .fade_out_ms = 500,
    .hold_ms = 200
};

/* State variables */
static esp_timer_handle_t fade_timer = NULL;
static uint8_t fade_phase = 0; /* 0=idle, 1=fade_in, 2=hold, 3=fade_out */
static uint32_t fade_elapsed_ms = 0;
static bool fade_running = false;
static const uint32_t fade_step_ms = 20; /* step resolution for fades */

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
        ESP_LOGD(TAG, "Timer already created");
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

    ESP_LOGI(TAG, "Fade timer created");
    return ESP_OK;
}

static esp_err_t fade_timer_stop(void)
{
    if (fade_timer != NULL) {
        esp_timer_stop(fade_timer);
        esp_timer_delete(fade_timer);
        fade_timer = NULL;
    }

    fade_phase = 0;
    fade_elapsed_ms = 0;
    fade_running = false;

    ESP_LOGI(TAG, "Fade timer stopped and state cleared");
    return ESP_OK;
}

/*******************************************************
 *                Timer Callback
 *******************************************************/

static void fade_timer_callback(void *arg)
{
    (void)arg;
    /* Check if fade plugin is active */
    if (!plugin_is_active("effect_fade")) {
        ESP_LOGW(TAG, "Fade timer callback called but plugin is not active, stopping timer");
        fade_timer_stop();
        return;
    }

    if (!fade_running) {
        return;
    }

    if (fade_phase == 1) { /* fade_in: from on -> off */
        if (fade_defaults.fade_in_ms == 0) {
            plugin_set_rgb_led(fade_defaults.r_off, fade_defaults.g_off, fade_defaults.b_off);
            fade_phase = 2; /* go to hold */
            fade_elapsed_ms = 0;
            if (fade_defaults.hold_ms > 0) {
                if (fade_timer != NULL) esp_timer_start_once(fade_timer, (uint64_t)fade_defaults.hold_ms * 1000ULL);
                return;
            }
        } else {
            uint32_t elapsed = fade_elapsed_ms;
            uint32_t total = fade_defaults.fade_in_ms;
            uint8_t r = interp_u8(fade_defaults.r_on, fade_defaults.r_off, elapsed, total);
            uint8_t g = interp_u8(fade_defaults.g_on, fade_defaults.g_off, elapsed, total);
            uint8_t b = interp_u8(fade_defaults.b_on, fade_defaults.b_off, elapsed, total);
            plugin_set_rgb_led(r, g, b);

            fade_elapsed_ms += fade_step_ms;
            if (fade_elapsed_ms >= fade_defaults.fade_in_ms) {
                plugin_set_rgb_led(fade_defaults.r_off, fade_defaults.g_off, fade_defaults.b_off);
                fade_phase = 2; /* hold */
                fade_elapsed_ms = 0;
                if (fade_defaults.hold_ms > 0) {
                    if (fade_timer != NULL) esp_timer_start_once(fade_timer, (uint64_t)fade_defaults.hold_ms * 1000ULL);
                    return;
                }
            } else {
                if (fade_timer != NULL) esp_timer_start_once(fade_timer, (uint64_t)fade_step_ms * 1000ULL);
                return;
            }
        }
    }

    if (fade_phase == 2) { /* hold between fades */
        if (fade_defaults.fade_out_ms > 0) {
            fade_phase = 3; /* start fade_out */
            fade_elapsed_ms = 0;
            if (fade_timer != NULL) esp_timer_start_once(fade_timer, 1);
            return;
        } else {
            plugin_set_rgb_led(fade_defaults.r_on, fade_defaults.g_on, fade_defaults.b_on);
            fade_phase = 1;
            fade_elapsed_ms = 0;
            if (fade_timer != NULL) esp_timer_start_once(fade_timer, 1);
            return;
        }
    }

    if (fade_phase == 3) { /* fade_out: from off -> on */
        if (fade_defaults.fade_out_ms == 0) {
            plugin_set_rgb_led(fade_defaults.r_on, fade_defaults.g_on, fade_defaults.b_on);
            fade_phase = 1;
            fade_elapsed_ms = 0;
            if (fade_timer != NULL) esp_timer_start_once(fade_timer, 1);
            return;
        } else {
            uint32_t elapsed = fade_elapsed_ms;
            uint32_t total = fade_defaults.fade_out_ms;
            uint8_t r = interp_u8(fade_defaults.r_off, fade_defaults.r_on, elapsed, total);
            uint8_t g = interp_u8(fade_defaults.g_off, fade_defaults.g_on, elapsed, total);
            uint8_t b = interp_u8(fade_defaults.b_off, fade_defaults.b_on, elapsed, total);
            plugin_set_rgb_led(r, g, b);

            fade_elapsed_ms += fade_step_ms;
            if (fade_elapsed_ms >= fade_defaults.fade_out_ms) {
                plugin_set_rgb_led(fade_defaults.r_on, fade_defaults.g_on, fade_defaults.b_on);
                fade_phase = 1;
                fade_elapsed_ms = 0;
                if (fade_timer != NULL) esp_timer_start_once(fade_timer, 1);
                return;
            } else {
                if (fade_timer != NULL) esp_timer_start_once(fade_timer, (uint64_t)fade_step_ms * 1000ULL);
                return;
            }
        }
    }
}

/*******************************************************
 *                Effect Control
 *******************************************************/

static esp_err_t fade_start(void)
{
    if (fade_running) {
        ESP_LOGD(TAG, "Fade effect already running");
        return ESP_OK;
    }

    /* Initialize state */
    fade_phase = 1; /* start with fade_in (on -> off) */
    fade_elapsed_ms = 0;
    fade_running = true;

    /* Ensure timer exists */
    if (fade_timer_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create/start fade timer");
        fade_running = false;
        return ESP_FAIL;
    }

    /* Set initial color to 'on' values, then start immediately */
    plugin_set_rgb_led(fade_defaults.r_on, fade_defaults.g_on, fade_defaults.b_on);
    esp_timer_start_once(fade_timer, 1);

    ESP_LOGI(TAG, "Fade effect started: on(%d,%d,%d) off(%d,%d,%d) in_ms=%u out_ms=%u hold_ms=%u",
             fade_defaults.r_on, fade_defaults.g_on, fade_defaults.b_on,
             fade_defaults.r_off, fade_defaults.g_off, fade_defaults.b_off,
             fade_defaults.fade_in_ms, fade_defaults.fade_out_ms, fade_defaults.hold_ms);
    return ESP_OK;
}

static esp_err_t fade_stop(void)
{
    esp_err_t err = fade_timer_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop fade timer: %s", esp_err_to_name(err));
    }

    /* Set LED to off */
    plugin_set_rgb_led(0, 0, 0);

    ESP_LOGI(TAG, "Fade effect stopped");
    return ESP_OK;
}

/*******************************************************
 *                Plugin Callbacks
 *******************************************************/

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
