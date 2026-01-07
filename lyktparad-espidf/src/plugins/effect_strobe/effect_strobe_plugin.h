/* Effect Strobe Plugin Header
 *
 * This module provides strobe effect functionality as a plugin.
 * The strobe effect automatically starts when the plugin is activated.
 *
 * The strobe effect is synchronized to the internal heartbeat counter system.
 * Each complete strobe cycle (4 strobes) takes exactly 1 heartbeat interval (1000ms).
 * Each strobe = 250ms (125ms on, 125ms off). The plugin uses a periodic timer that
 * reads the synchronized local heartbeat counter to determine cycle progress.
 * Effects continue running even when mesh heartbeats stop (node out of range)
 * and automatically resynchronize when the node rejoins the mesh.
 *
 * Copyright (c) 2025 Arvind
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __EFFECT_STROBE_PLUGIN_H__
#define __EFFECT_STROBE_PLUGIN_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Plugin Registration
 *******************************************************/

/**
 * @brief Register the effect_strobe plugin with the plugin system
 *
 * This function should be called during system initialization to register
 * the effect_strobe plugin. The plugin will be assigned a command ID automatically.
 */
void effect_strobe_plugin_register(void);

#endif /* __EFFECT_STROBE_PLUGIN_H__ */
