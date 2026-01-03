/* Root Status LED Module Implementation
 *
 * This module provides a separate single-color status LED to indicate root node status.
 * The LED is ON when the node is the root node, and OFF when it is a non-root node.
 * The LED updates immediately on role changes.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "root_status_led.h"
#include "config/mesh_device_config.h"
#include "esp_mesh.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "root_status_led";

/* GPIO pin for root status LED - configurable via mesh_device_config.h */
#ifndef ROOT_STATUS_LED_GPIO
#define ROOT_STATUS_LED_GPIO 3  /* Default GPIO 3 */
#endif

static bool is_initialized = false;

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

    /* Set initial state based on current mesh role */
    bool is_root = esp_mesh_is_root();
    gpio_set_level(ROOT_STATUS_LED_GPIO, is_root ? 1 : 0);

    is_initialized = true;
    ESP_LOGI(TAG, "Root status LED initialized on GPIO %d (current state: %s)",
             ROOT_STATUS_LED_GPIO, is_root ? "ON (root)" : "OFF (non-root)");

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

    gpio_set_level(ROOT_STATUS_LED_GPIO, is_root ? 1 : 0);
    ESP_LOGI(TAG, "Root status LED set to %s", is_root ? "ON (root)" : "OFF (non-root)");
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
