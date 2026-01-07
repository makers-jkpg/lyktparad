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
#include "mesh_common.h"
#include "config/mesh_config.h"
#include "config/mesh_device_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "effect_strobe_plugin";

/* Plugin ID storage (assigned during registration) */
static uint8_t strobe_plugin_id = 0;

/* Strobe parameters - calculated from heartbeat interval */
/* 4 strobes per heartbeat cycle: strobe_period = MESH_CONFIG_HEARTBEAT_INTERVAL / 4 */
/* Each strobe: on/off durations = strobe_period / 2 = MESH_CONFIG_HEARTBEAT_INTERVAL / 8 */
static const struct {
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
} strobe_defaults = {
    .r_on = 255, .g_on = 255, .b_on = 255,  /* White */
    .r_off = 0, .g_off = 0, .b_off = 0,      /* Black */
};

/* Calculate strobe timing from heartbeat interval */
#define STROBE_PERIOD_MS (MESH_CONFIG_HEARTBEAT_INTERVAL / 4)  /* 4 strobes per heartbeat cycle */
#define STROBE_DURATION_ON_MS (MESH_CONFIG_HEARTBEAT_INTERVAL / 8)  /* Half of strobe period */
#define STROBE_DURATION_OFF_MS (MESH_CONFIG_HEARTBEAT_INTERVAL / 8)  /* Half of strobe period */

/* State variables */
static esp_timer_handle_t strobe_timer = NULL;
static uint8_t strobe_last_counter = 0; /* Last counter value seen */
static int64_t strobe_cycle_start_us = 0; /* Cycle start time in microseconds */
static bool strobe_running = false;
static bool strobe_paused = false;
static const uint32_t strobe_update_interval_ms = 20; /* Update interval for smooth updates */

/* Forward declarations */
static void strobe_timer_callback(void *arg);
static esp_err_t strobe_timer_start(void);
static esp_err_t strobe_timer_stop(void);
static esp_err_t strobe_start(void);
static esp_err_t strobe_stop(void);

/*******************************************************
 *                Heartbeat Handler
 *******************************************************/

/**
 * @brief Handle heartbeat from root node for strobe effect synchronization
 *
 * This function processes heartbeat messages to synchronize the strobe effect
 * cycle across all mesh nodes. It corrects drift by resetting the cycle start time
 * when the counter changes, ensuring perfect synchronization.
 *
 * The heartbeat handler only corrects synchronization - the local heartbeat timer
 * continues to drive the main timing. This ensures graceful degradation during
 * mesh disconnection.
 *
 * @param pointer Heartbeat pointer (unused for this plugin, for sequence plugin compatibility)
 * @param counter Heartbeat counter value (0-255, wraps)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t effect_strobe_plugin_handle_heartbeat(uint8_t pointer, uint8_t counter)
{
    (void)pointer;  /* Pointer is unused for this plugin, kept for sequence plugin compatibility */

    /* Only process heartbeat if plugin is active */
    if (!plugin_is_active("effect_strobe")) {
        ESP_LOGD(TAG, "Heartbeat received but strobe plugin not active, ignoring");
        return ESP_OK;
    }

    /* Read current local heartbeat counter (should match received counter after mesh_common_set_local_heartbeat_counter()) */
    uint8_t current_counter = mesh_common_get_local_heartbeat_counter();

    /* Check if counter has changed (indicating mesh synchronization or new cycle) */
    /* Counter normally increments by 1 each heartbeat interval, but may jump when synchronized */
    if (current_counter != strobe_last_counter) {
        /* Counter changed - reset cycle start time to synchronize with mesh (correct drift) */
        strobe_cycle_start_us = esp_timer_get_time();
        strobe_last_counter = current_counter;
        ESP_LOGD(TAG, "Heartbeat received - counter: %u, cycle start time reset for synchronization", current_counter);
    } else {
        ESP_LOGD(TAG, "Heartbeat received - counter: %u (no change, already synchronized)", current_counter);
    }

    return ESP_OK;
}

/*******************************************************
 *                Timer Management
 *******************************************************/

static esp_err_t strobe_timer_start(void)
{
    if (strobe_timer != NULL) {
        /* Timer already exists, start periodic timer with update interval */
        esp_err_t err = esp_timer_start_periodic(strobe_timer, (uint64_t)strobe_update_interval_ms * 1000ULL);
        if (err == ESP_OK) {
            ESP_LOGD(TAG, "Strobe timer started (periodic, %dms)", strobe_update_interval_ms);
        } else if (err == ESP_ERR_INVALID_STATE) {
            /* Timer already running, that's okay */
            ESP_LOGD(TAG, "Strobe timer already running");
        } else {
            ESP_LOGE(TAG, "Failed to start strobe timer: %s", esp_err_to_name(err));
            return err;
        }
        /* Reinitialize cycle start time and counter when restarting existing timer */
        strobe_cycle_start_us = esp_timer_get_time();
        strobe_last_counter = mesh_common_get_local_heartbeat_counter();
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

    err = esp_timer_start_periodic(strobe_timer, (uint64_t)strobe_update_interval_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start strobe timer: %s", esp_err_to_name(err));
        esp_timer_delete(strobe_timer);
        strobe_timer = NULL;
        return err;
    }

    /* Initialize cycle start time and counter */
    strobe_cycle_start_us = esp_timer_get_time();
    strobe_last_counter = mesh_common_get_local_heartbeat_counter();

    ESP_LOGI(TAG, "Strobe timer created and started (periodic, %dms, synchronized to heartbeat)", strobe_update_interval_ms);
    return ESP_OK;
}

static esp_err_t strobe_timer_stop(void)
{
    if (strobe_timer != NULL) {
        esp_timer_stop(strobe_timer);
        /* Don't delete timer - keep it for reuse */
    }

    strobe_last_counter = 0;
    strobe_cycle_start_us = 0;
    strobe_running = false;

    ESP_LOGI(TAG, "Strobe timer stopped");
    return ESP_OK;
}

/*******************************************************
 *                Timer Callback
 *******************************************************/

/**
 * @brief Timer callback synchronized to heartbeat counter
 *
 * This callback fires every strobe_update_interval_ms (20ms) when the plugin is active.
 * It reads the synchronized local heartbeat counter to determine the strobe cycle progress.
 * Each complete strobe cycle (4 strobes) takes exactly 1 heartbeat interval (MESH_CONFIG_HEARTBEAT_INTERVAL).
 * Each strobe period = MESH_CONFIG_HEARTBEAT_INTERVAL / 4.
 * Each strobe on/off duration = MESH_CONFIG_HEARTBEAT_INTERVAL / 8.
 * The counter is used to synchronize cycles across all nodes, while the timer provides smooth updates.
 */
static void strobe_timer_callback(void *arg)
{
    (void)arg;

    /* Check if paused - if so, don't process timer callback */
    if (strobe_paused) {
        return;
    }

    /* Check if effect is running */
    if (!strobe_running) {
        return;
    }

    /* Check if strobe plugin is active (double-check for safety) */
    if (!plugin_is_active("effect_strobe")) {
        ESP_LOGW(TAG, "Strobe timer callback called but plugin is not active, stopping timer");
        strobe_timer_stop();
        return;
    }

    /* Read synchronized local heartbeat counter */
    uint8_t counter = mesh_common_get_local_heartbeat_counter();

    /* Check if counter has changed (new cycle started or synchronized after mesh reconnect) */
    /* Counter normally increments by 1 each heartbeat interval, but may jump when synchronized */
    if (counter != strobe_last_counter) {
        /* Counter changed - new cycle started or resynchronized, reset cycle start time */
        strobe_cycle_start_us = esp_timer_get_time();
        strobe_last_counter = counter;
    }

    /* Calculate elapsed time since cycle start */
    int64_t current_time_us = esp_timer_get_time();
    int64_t elapsed_us = current_time_us - strobe_cycle_start_us;
    uint32_t cycle_progress_ms = (uint32_t)(elapsed_us / 1000ULL);

    /* Wrap cycle progress to heartbeat interval (1000ms) */
    if (cycle_progress_ms >= MESH_CONFIG_HEARTBEAT_INTERVAL) {
        /* Cycle complete, reset to start of cycle */
        cycle_progress_ms = cycle_progress_ms % MESH_CONFIG_HEARTBEAT_INTERVAL;
        strobe_cycle_start_us = current_time_us - (int64_t)cycle_progress_ms * 1000ULL;
    }

    /* Calculate position within strobe cycle */
    uint32_t strobe_duration_ms = STROBE_DURATION_ON_MS + STROBE_DURATION_OFF_MS; /* STROBE_PERIOD_MS per strobe */
    uint32_t strobe_position_ms = cycle_progress_ms % strobe_duration_ms; /* Position within current strobe */

    /* Determine if strobe is on or off based on position within strobe */
    bool strobe_is_on = (strobe_position_ms < STROBE_DURATION_ON_MS);

    /* Update RGB LED based on strobe state */
    if (strobe_is_on) {
        plugin_set_rgb(strobe_defaults.r_on, strobe_defaults.g_on, strobe_defaults.b_on);
    } else {
        plugin_set_rgb(strobe_defaults.r_off, strobe_defaults.g_off, strobe_defaults.b_off);
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
    strobe_running = true;
    strobe_paused = false; /* Clear pause flag when starting */

    /* Ensure timer exists and is started (this also initializes cycle start time and counter) */
    if (strobe_timer_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create/start strobe timer");
        strobe_running = false;
        return ESP_FAIL;
    }

    /* Set initial color to off (start of first strobe) */
    plugin_set_rgb(strobe_defaults.r_off, strobe_defaults.g_off, strobe_defaults.b_off);

    ESP_LOGI(TAG, "Strobe effect started: on(%d,%d,%d) off(%d,%d,%d) on_ms=%u off_ms=%u (4 strobes per %ums)",
             strobe_defaults.r_on, strobe_defaults.g_on, strobe_defaults.b_on,
             strobe_defaults.r_off, strobe_defaults.g_off, strobe_defaults.b_off,
             STROBE_DURATION_ON_MS, STROBE_DURATION_OFF_MS,
             MESH_CONFIG_HEARTBEAT_INTERVAL);
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

    ESP_LOGI(TAG, "Strobe effect paused");
    return ESP_OK;
}

static esp_err_t effect_strobe_on_reset(void)
{
    /* Stop timer */
    if (strobe_timer != NULL) {
        esp_timer_stop(strobe_timer);
    }

    /* Reset state */
    strobe_last_counter = 0;
    strobe_cycle_start_us = 0;
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
        .is_default = false,  /* effect_strobe is not the default plugin */
        .command_id = 0, /* Will be assigned by plugin system */
        .callbacks = {
            .command_handler = strobe_command_handler,
            .timer_callback = strobe_timer_callback,
            .heartbeat_handler = effect_strobe_plugin_handle_heartbeat,
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
