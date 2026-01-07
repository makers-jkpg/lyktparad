/* Effects Plugin Header
 *
 * This module provides effects mode functionality as a plugin.
 * Effects mode allows synchronized playback of visual effects across all mesh nodes.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __EFFECTS_PLUGIN_H__
#define __EFFECTS_PLUGIN_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Effect Definitions
 *******************************************************/

#define EFFECT_NONE    0
#define EFFECT_STROBE  1
#define EFFECT_FADE    2

/*******************************************************
 *                Data Structures
 *******************************************************/

struct effect_params_t {
    uint8_t command;           /* command is the effect command identifier (deprecated field, kept for compatibility) */
    uint8_t effect_id;         /* effect_id is the id of the effect to play */
    uint16_t start_delay_ms;   /* start_delay_ms is the delay before starting the effect */
};

struct effect_params_strobe_t {
    struct effect_params_t base;
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
    uint16_t duration_on;
    uint16_t duration_off;
    uint8_t repeat_count;
};

struct effect_params_fade_t {
    struct effect_params_t base;
    uint8_t r_on, g_on, b_on;
    uint8_t r_off, g_off, b_off;
    uint16_t fade_in_ms, fade_out_ms;
    uint16_t duration_ms;
    uint8_t repeat_count;
};

/*******************************************************
 *                Plugin Registration
 *******************************************************/

/**
 * @brief Register the effects plugin with the plugin system
 *
 * This function should be called during system initialization to register
 * the effects plugin. The plugin will be assigned a command ID automatically.
 */
void effects_plugin_register(void);

#endif /* __EFFECTS_PLUGIN_H__ */
