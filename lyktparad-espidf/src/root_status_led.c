/* Root Status LED Module Implementation
 *
 * This module provides a separate single-color status LED to indicate root node status.
 * The LED blinks in different patterns based on router connection status and mesh node count.
 * The LED is optional and disabled by default (no code compiled if ROOT_STATUS_LED_GPIO is undefined).
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "config/mesh_device_config.h"

#ifdef ROOT_STATUS_LED_GPIO

#include "root_status_led.h"
#include "mesh_common.h"
#include "esp_mesh.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "root_status_led";

/* GPIO level helper macro - applies invert logic if ROOT_STATUS_LED_INVERT is defined
 * When ROOT_STATUS_LED_INVERT is defined: LED ON (true) → GPIO LOW (0), LED OFF (false) → GPIO HIGH (1)
 * When ROOT_STATUS_LED_INVERT is undefined: LED ON (true) → GPIO HIGH (1), LED OFF (false) → GPIO LOW (0)
 */
#ifdef ROOT_STATUS_LED_INVERT
#define ROOT_STATUS_LED_GPIO_LEVEL(state) ((state) ? 0 : 1)
#else
#define ROOT_STATUS_LED_GPIO_LEVEL(state) ((state) ? 1 : 0)
#endif

/* Blinking pattern step structure */
typedef struct {
    uint32_t duration_ms;  /* Duration in milliseconds */
    bool led_state;        /* LED state (true=ON, false=OFF) */
} pattern_step_t;

/* Pattern step arrays - all patterns are 1000ms total cycle */
static const pattern_step_t pattern_startup[] = {
    {250, true},   /* ON 250ms */
    {750, false},  /* OFF 750ms */
};
#define PATTERN_STARTUP_STEPS (sizeof(pattern_startup) / sizeof(pattern_step_t))

static const pattern_step_t pattern_router_only[] = {
    {125, true},   /* ON 125ms */
    {125, false},  /* OFF 125ms */
    {125, true},   /* ON 125ms */
    {625, false},  /* OFF 625ms */
};
#define PATTERN_ROUTER_ONLY_STEPS (sizeof(pattern_router_only) / sizeof(pattern_step_t))

static const pattern_step_t pattern_nodes_only[] = {
    {125, true},   /* ON 125ms */
    {375, false},  /* OFF 375ms */
    {125, true},   /* ON 125ms */
    {375, false},  /* OFF 375ms */
};
#define PATTERN_NODES_ONLY_STEPS (sizeof(pattern_nodes_only) / sizeof(pattern_step_t))

static const pattern_step_t pattern_router_and_nodes[] = {
    {125, true},   /* ON 125ms */
    {125, false},  /* OFF 125ms */
    {125, true},   /* ON 125ms */
    {125, false},  /* OFF 125ms */
    {125, true},   /* ON 125ms */
    {125, false},  /* OFF 125ms */
    {125, true},   /* ON 125ms */
    {125, false},  /* OFF 125ms */
};
#define PATTERN_ROUTER_AND_NODES_STEPS (sizeof(pattern_router_and_nodes) / sizeof(pattern_step_t))

/* Get pattern step array and count */
static const pattern_step_t* get_pattern_steps(root_led_pattern_t pattern, size_t *step_count)
{
    switch (pattern) {
    case ROOT_LED_PATTERN_STARTUP:
        *step_count = PATTERN_STARTUP_STEPS;
        return pattern_startup;
    case ROOT_LED_PATTERN_ROUTER_ONLY:
        *step_count = PATTERN_ROUTER_ONLY_STEPS;
        return pattern_router_only;
    case ROOT_LED_PATTERN_NODES_ONLY:
        *step_count = PATTERN_NODES_ONLY_STEPS;
        return pattern_nodes_only;
    case ROOT_LED_PATTERN_ROUTER_AND_NODES:
        *step_count = PATTERN_ROUTER_AND_NODES_STEPS;
        return pattern_router_and_nodes;
    case ROOT_LED_PATTERN_OFF:
    default:
        *step_count = 0;
        return NULL;
    }
}

/* Runtime state */
static bool is_initialized = false;
static esp_timer_handle_t blink_timer = NULL;
static root_led_pattern_t current_pattern = ROOT_LED_PATTERN_OFF;
static size_t current_step_index = 0;
static size_t current_pattern_step_count = 0;
static const pattern_step_t *current_pattern_steps = NULL;

/**
 * @brief Timer callback for blinking pattern execution
 */
static void root_status_led_blink_timer_cb(void *arg)
{
    if (!is_initialized || current_pattern == ROOT_LED_PATTERN_OFF) {
        return;
    }

    if (current_pattern_steps == NULL || current_pattern_step_count == 0) {
        return;
    }

    /* Get current step */
    const pattern_step_t *step = &current_pattern_steps[current_step_index];

    /* Set LED state */
    gpio_set_level(ROOT_STATUS_LED_GPIO, ROOT_STATUS_LED_GPIO_LEVEL(step->led_state));

    /* Advance to next step */
    current_step_index++;
    if (current_step_index >= current_pattern_step_count) {
        current_step_index = 0;  /* Loop back to beginning */
    }

    /* Schedule next step */
    step = &current_pattern_steps[current_step_index];
    uint64_t delay_us = (uint64_t)step->duration_ms * 1000;
    esp_err_t err = esp_timer_start_once(blink_timer, delay_us);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to restart blink timer: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Stop blinking and turn LED OFF
 */
static void root_status_led_stop_blinking(void)
{
    if (blink_timer != NULL) {
        esp_timer_stop(blink_timer);
    }
    current_pattern = ROOT_LED_PATTERN_OFF;
    current_step_index = 0;
    current_pattern_step_count = 0;
    current_pattern_steps = NULL;
    if (is_initialized) {
        gpio_set_level(ROOT_STATUS_LED_GPIO, ROOT_STATUS_LED_GPIO_LEVEL(false));
    }
}

/**
 * @brief Start blinking with specified pattern
 */
static void root_status_led_start_blinking(root_led_pattern_t pattern)
{
    if (!is_initialized || blink_timer == NULL) {
        return;
    }

    if (pattern == ROOT_LED_PATTERN_OFF) {
        root_status_led_stop_blinking();
        return;
    }

    /* Stop current blinking */
    esp_timer_stop(blink_timer);

    /* Get pattern steps */
    size_t step_count;
    const pattern_step_t *steps = get_pattern_steps(pattern, &step_count);
    if (steps == NULL || step_count == 0) {
        ESP_LOGE(TAG, "Invalid pattern or no steps");
        return;
    }

    /* Update pattern state */
    current_pattern = pattern;
    current_pattern_steps = steps;
    current_pattern_step_count = step_count;
    current_step_index = 0;

    /* Start with first step */
    const pattern_step_t *step = &steps[0];
    gpio_set_level(ROOT_STATUS_LED_GPIO, ROOT_STATUS_LED_GPIO_LEVEL(step->led_state));

    /* Schedule next step */
    if (step_count > 1) {
        step = &steps[1];
        uint64_t delay_us = (uint64_t)step->duration_ms * 1000;
        esp_err_t err = esp_timer_start_once(blink_timer, delay_us);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start blink timer: %s", esp_err_to_name(err));
        }
        current_step_index = 1;
    }
}

/**
 * @brief Initialize the root status LED GPIO
 */
esp_err_t root_status_led_init(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "Root status LED already initialized");
        return ESP_OK;
    }

    /* Reset GPIO pin to default state before configuration */
    gpio_reset_pin(ROOT_STATUS_LED_GPIO);

    /* Configure GPIO pin as output */
    esp_err_t err = gpio_set_direction(ROOT_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO direction: %s", esp_err_to_name(err));
        return err;
    }

    /* Create blinking timer (one-shot, will be restarted for each step) */
    const esp_timer_create_args_t timer_args = {
        .callback = &root_status_led_blink_timer_cb,
        .arg = NULL,
        .name = "root_status_led_blink"
    };
    err = esp_timer_create(&timer_args, &blink_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create blink timer: %s", esp_err_to_name(err));
        blink_timer = NULL;
        return err;
    }

    /* Set initial state - start with STARTUP pattern if root, OFF if not root */
    bool is_root = esp_mesh_is_root();
    if (is_root) {
        current_pattern = ROOT_LED_PATTERN_STARTUP;
        root_status_led_start_blinking(ROOT_LED_PATTERN_STARTUP);
    } else {
        gpio_set_level(ROOT_STATUS_LED_GPIO, ROOT_STATUS_LED_GPIO_LEVEL(false));
        current_pattern = ROOT_LED_PATTERN_OFF;
    }

    is_initialized = true;
    ESP_LOGI(TAG, "Root status LED initialized on GPIO %d (pattern: %s)",
             ROOT_STATUS_LED_GPIO, is_root ? "STARTUP" : "OFF");

    return ESP_OK;
}

/**
 * @brief Set the root status LED state
 */
void root_status_led_set_root(bool is_root)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "Root status LED not initialized, skipping set");
        return;
    }

    if (is_root) {
        /* When setting to root, update status to determine pattern */
        root_status_led_update_status();
    } else {
        /* When setting to non-root, stop blinking and turn LED OFF */
        root_status_led_stop_blinking();
    }
}

/**
 * @brief Update the root status LED based on current mesh role
 */
void root_status_led_update(void)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "Root status LED not initialized, skipping update");
        return;
    }

    bool is_root = esp_mesh_is_root();
    root_status_led_set_root(is_root);
}

/**
 * @brief Update the root status LED pattern based on router connection and node count
 */
void root_status_led_update_status(void)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "Root status LED not initialized, skipping status update");
        return;
    }

    bool is_root = esp_mesh_is_root();
    if (!is_root) {
        /* Not root - LED OFF */
        root_status_led_stop_blinking();
        return;
    }

    /* Get router connection status */
    bool router_connected = mesh_common_is_router_connected();

    /* Get node count (exclude root) */
    int routing_table_size = esp_mesh_get_routing_table_size();
    int node_count = (routing_table_size > 0) ? (routing_table_size - 1) : 0;

    /* Determine pattern based on status */
    root_led_pattern_t new_pattern;
    if (router_connected && node_count == 0) {
        new_pattern = ROOT_LED_PATTERN_ROUTER_ONLY;
    } else if (!router_connected && node_count > 0) {
        new_pattern = ROOT_LED_PATTERN_NODES_ONLY;
    } else if (router_connected && node_count > 0) {
        new_pattern = ROOT_LED_PATTERN_ROUTER_AND_NODES;
    } else {
        /* !router_connected && node_count == 0 */
        new_pattern = ROOT_LED_PATTERN_STARTUP;
    }

    /* Update pattern if changed */
    if (new_pattern != current_pattern) {
        root_status_led_start_blinking(new_pattern);
        ESP_LOGD(TAG, "Pattern changed: router_connected=%d, node_count=%d, pattern=%d",
                 router_connected ? 1 : 0, node_count, new_pattern);
    }
}

#endif /* ROOT_STATUS_LED_GPIO */
