/* Mesh GPIO Module Implementation
 *
 * This module implements GPIO-based root node forcing functionality.
 * GPIO pins are read at startup to determine if root node behavior should be forced.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "mesh_gpio.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GPIO_TAG "mesh_gpio"
#define GPIO_STABILIZATION_DELAY_MS 50  /* Delay to allow pull-up resistors to stabilize */

/* Track initialization state to make function idempotent */
static bool gpio_initialized = false;

bool mesh_gpio_is_initialized(void)
{
    return gpio_initialized;
}

/*******************************************************
 *                GPIO Initialization
 *******************************************************/

esp_err_t mesh_gpio_init(void)
{
    if (gpio_initialized) {
        ESP_LOGD(GPIO_TAG, "GPIO already initialized, skipping");
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << MESH_GPIO_FORCE_ROOT) | (1ULL << MESH_GPIO_FORCE_MESH),
        .pull_down_en = 0,
        .pull_up_en = 1,  /* Enable internal pull-up resistors */
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(GPIO_TAG, "Failed to configure GPIO pins: %s", esp_err_to_name(err));
        return err;
    }

    /* Allow pull-up resistors to stabilize before reading pin values
     * Internal pull-ups need time to charge any stray capacitance on floating pins
     * 50ms is more than sufficient for internal pull-ups to stabilize
     */
    vTaskDelay(pdMS_TO_TICKS(GPIO_STABILIZATION_DELAY_MS));

    gpio_initialized = true;
    ESP_LOGI(GPIO_TAG, "GPIO pins configured with pull-ups: GPIO %d (Force Root), GPIO %d (Force Mesh) - stabilized after %d ms",
             MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH, GPIO_STABILIZATION_DELAY_MS);

    return ESP_OK;
}

/*******************************************************
 *                GPIO Reading
 *******************************************************/

bool mesh_gpio_read_root_force(void)
{
    if (!gpio_initialized) {
        ESP_LOGW(GPIO_TAG, "GPIO not initialized, defaulting to mesh node behavior");
        return false;
    }

    int level_root = gpio_get_level(MESH_GPIO_FORCE_ROOT);
    int level_mesh = gpio_get_level(MESH_GPIO_FORCE_MESH);

    ESP_LOGD(GPIO_TAG, "GPIO pin states: GPIO %d (Force Root)=%d, GPIO %d (Force Mesh)=%d",
             MESH_GPIO_FORCE_ROOT, level_root, MESH_GPIO_FORCE_MESH, level_mesh);

    /* Simplified logic: Only one pin should be tied to GND at a time for forcing behavior
     * - GPIO 5 (Force Root) LOW (and GPIO 4 HIGH): Force root node -> return true
     * - GPIO 4 (Force Mesh) LOW (and GPIO 5 HIGH): Force mesh node -> return false
     * - Both HIGH (both floating with pull-ups): Normal root election -> return false
     * - Both LOW (conflict): Normal root election -> return false (handled as normal election in mesh_common.c)
     */
    bool force_root = false;

    if (level_root == 0 && level_mesh == 0) {
        /* Both pins LOW - conflict condition, will default to normal root election */
        ESP_LOGW(GPIO_TAG, "GPIO conflict detected (both pins LOW), will default to normal root election");
        force_root = false;
    } else if (level_root == 0) {
        /* Force Root pin is LOW (tied to GND), Force Mesh pin is HIGH (floating with pull-up) */
        ESP_LOGI(GPIO_TAG, "GPIO forcing root node behavior (GPIO %d=LOW, GPIO %d=HIGH/floating)",
                 MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH);
        force_root = true;
    } else if (level_mesh == 0) {
        /* Force Mesh pin is LOW (tied to GND), Force Root pin is HIGH (floating with pull-up) */
        ESP_LOGI(GPIO_TAG, "GPIO forcing mesh node behavior (GPIO %d=HIGH/floating, GPIO %d=LOW)",
                 MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH);
        force_root = false;
    } else {
        /* Both pins HIGH - both floating with pull-ups active (normal operation, no forcing) */
        ESP_LOGI(GPIO_TAG, "GPIO defaulting to normal root election (both pins HIGH/floating)");
        force_root = false;
    }

    return force_root;
}
