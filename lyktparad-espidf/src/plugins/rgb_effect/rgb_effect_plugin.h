/* RGB Effect Plugin Header
 *
 * This module provides RGB color cycling effect functionality as a plugin.
 * The effect cycles through 6 colors synchronized across all mesh nodes
 * using heartbeat counter mechanism.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __RGB_EFFECT_PLUGIN_H__
#define __RGB_EFFECT_PLUGIN_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Plugin Registration
 *******************************************************/

/**
 * @brief Register the rgb_effect plugin with the plugin system
 *
 * This function should be called during system initialization to register
 * the rgb_effect plugin. The plugin will be assigned a command ID automatically.
 */
void rgb_effect_plugin_register(void);

/*******************************************************
 *                Heartbeat Handler
 *******************************************************/

/**
 * @brief Handle heartbeat from root node for RGB effect synchronization
 *
 * This function processes heartbeat messages to synchronize the RGB effect
 * counter across all mesh nodes. It corrects drift and updates the color
 * based on the received counter value.
 *
 * @param pointer Heartbeat pointer (unused for this plugin, for sequence plugin compatibility)
 * @param counter Heartbeat counter value (0-255, wraps)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t rgb_effect_plugin_handle_heartbeat(uint8_t pointer, uint8_t counter);

#endif /* __RGB_EFFECT_PLUGIN_H__ */
