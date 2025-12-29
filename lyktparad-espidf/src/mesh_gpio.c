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

#define GPIO_TAG "mesh_gpio"

/* Track initialization state to make function idempotent */
static bool gpio_initialized = false;

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

    gpio_initialized = true;
    ESP_LOGI(GPIO_TAG, "GPIO pins configured: GPIO %d (Force Root), GPIO %d (Force Mesh)",
             MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH);

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

    /* Truth table implementation:
     * - Pin A=LOW (0), Pin B=HIGH (1): Force root node -> return true
     * - All other combinations: Default to mesh node -> return false
     */
    bool force_root = (level_root == 0) && (level_mesh == 1);

    if (force_root) {
        ESP_LOGI(GPIO_TAG, "GPIO forcing root node behavior (GPIO %d=LOW, GPIO %d=HIGH)",
                 MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH);
    } else if (level_root == 0 && level_mesh == 0) {
        ESP_LOGW(GPIO_TAG, "GPIO conflict detected (both pins LOW), defaulting to mesh node behavior");
    } else if (level_root == 1 && level_mesh == 0) {
        ESP_LOGI(GPIO_TAG, "GPIO forcing mesh node behavior (GPIO %d=HIGH, GPIO %d=LOW)",
                 MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH);
    } else {
        ESP_LOGI(GPIO_TAG, "GPIO defaulting to mesh node behavior (GPIO %d=HIGH, GPIO %d=HIGH)",
                 MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH);
    }

    return force_root;
}
