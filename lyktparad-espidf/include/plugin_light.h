/* Plugin-Safe LED Control Header
 *
 * This module provides plugin-safe wrapper functions for LED control.
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

#ifndef __PLUGIN_LIGHT_H__
#define __PLUGIN_LIGHT_H__

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Set RGB LED color (plugin-safe wrapper)
 *
 * This function checks if a plugin is active before
 * allowing LED control. Only the active plugin can control LEDs.
 * Plugins should check their active status before calling this function.
 *
 * This function controls Neopixel/WS2812 LEDs only. Requires NEOPIXEL_ENABLE
 * to be defined (enabled by default). If NEOPIXEL_ENABLE is not defined,
 * the underlying function is stubbed and returns ESP_OK (no-op).
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if no plugin is active
 * @note Requires NEOPIXEL_ENABLE to be defined for actual LED control
 */
esp_err_t plugin_light_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set RGB LED color using direct LED control (plugin-safe wrapper)
 *
 * This function checks if a plugin is active before
 * allowing LED control. Only the active plugin can control LEDs.
 * Plugins should check their active status before calling this function.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if no plugin is active
 */
esp_err_t plugin_set_rgb_led(int r, int g, int b);

/**
 * @brief Set RGB LED color on all available LED systems (unified wrapper)
 *
 * This is the recommended function for plugin LED control. It automatically
 * controls all available LED systems:
 * - Neopixel/WS2812 LEDs (if NEOPIXEL_ENABLE is defined, enabled by default, via plugin_light_set_rgb)
 * - Common-cathode/anode RGB LEDs (if RGB_ENABLE is defined, via plugin_set_rgb_led)
 *
 * This function eliminates the need for conditional compilation in plugins.
 * Plugins should use this function instead of calling plugin_light_set_rgb()
 * and plugin_set_rgb_led() separately.
 *
 * The function checks if a plugin is active before allowing LED control.
 * Only the active plugin can control LEDs.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return ESP_OK on success (or if both LED types are disabled)
 * @return ESP_ERR_INVALID_STATE if no plugin is active
 * @return Error code from Neopixel if NEOPIXEL_ENABLE is defined and Neopixel call fails
 *
 * @note This function replaces the need for conditional compilation in plugins.
 *       For advanced use cases requiring fine-grained control, the individual
 *       functions (plugin_light_set_rgb, plugin_set_rgb_led) remain available.
 * @note Both LED systems are optional and can be enabled/disabled independently via
 *       NEOPIXEL_ENABLE and RGB_ENABLE defines in mesh_device_config.h.
 */
esp_err_t plugin_set_rgb(uint8_t r, uint8_t g, uint8_t b);

#endif /* __PLUGIN_LIGHT_H__ */
