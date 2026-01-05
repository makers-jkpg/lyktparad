/* Plugin-Safe LED Control Implementation
 *
 * This module implements plugin-safe wrapper functions for LED control.
 * These functions check if a plugin is active before allowing LED control.
 * Only the active plugin can control LEDs.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "plugin_light.h"
#include "plugin_system.h"
#include "light_neopixel.h"
#include "light_common_cathode.h"
#include "esp_log.h"

static const char *TAG = "plugin_light";

esp_err_t plugin_light_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    const char *active = plugin_get_active();
    if (active == NULL) {
        ESP_LOGW(TAG, "LED control blocked: no active plugin");
        return ESP_ERR_INVALID_STATE;
    }

    /* Note: We cannot determine which plugin is calling this function
     * directly. We rely on plugins to only call this when they are active.
     * Enforcement is done at the plugin level (timer callbacks, command handlers).
     */
    return mesh_light_set_rgb(r, g, b);
}

esp_err_t plugin_set_rgb_led(int r, int g, int b)
{
    const char *active = plugin_get_active();
    if (active == NULL) {
        ESP_LOGW(TAG, "LED control blocked: no active plugin");
        return ESP_ERR_INVALID_STATE;
    }

    /* Note: We cannot determine which plugin is calling this function
     * directly. We rely on plugins to only call this when they are active.
     * Enforcement is done at the plugin level (timer callbacks, command handlers).
     */
    set_rgb_led(r, g, b);
    return ESP_OK;
}
