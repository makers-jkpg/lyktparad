/* Effect Fade Plugin Header
 *
 * This module provides fade effect functionality as a plugin.
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

#ifndef __EFFECT_FADE_PLUGIN_H__
#define __EFFECT_FADE_PLUGIN_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Plugin Registration
 *******************************************************/

/**
 * @brief Register the effect_fade plugin with the plugin system
 *
 * This function should be called during system initialization to register
 * the effect_fade plugin. The plugin will be assigned a command ID automatically.
 */
void effect_fade_plugin_register(void);

#endif /* __EFFECT_FADE_PLUGIN_H__ */
