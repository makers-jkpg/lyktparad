/* Effect Strobe Plugin Header
 *
 * This module provides strobe effect functionality as a plugin.
 * The strobe effect automatically starts when the plugin is activated.
 *
 * The strobe effect is synchronized to the internal heartbeat counter system.
 * Each complete strobe cycle (4 strobes) takes exactly 1 heartbeat interval (MESH_CONFIG_HEARTBEAT_INTERVAL).
 * Each strobe period = MESH_CONFIG_HEARTBEAT_INTERVAL / 4.
 * Each strobe on/off duration = MESH_CONFIG_HEARTBEAT_INTERVAL / 8.
 * The plugin uses a periodic timer that reads the synchronized local heartbeat counter
 * to determine cycle progress. Effects continue running even when mesh heartbeats stop
 * (node out of range) and automatically resynchronize when the node rejoins the mesh.
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

/*******************************************************
 *                Heartbeat Handler
 *******************************************************/

/**
 * @brief Handle heartbeat from root node for strobe effect synchronization
 *
 * This function processes heartbeat messages to synchronize the strobe effect
 * cycle across all mesh nodes. It corrects drift by resetting the cycle start time
 * when the counter changes, ensuring perfect synchronization.
 *
 * The heartbeat handler only corrects synchronization - the local heartbeat timer
 * continues to drive the main timing. This ensures graceful degradation during
 * mesh disconnection.
 *
 * @param pointer Heartbeat pointer (unused for this plugin, for sequence plugin compatibility)
 * @param counter Heartbeat counter value (0-255, wraps)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t effect_strobe_plugin_handle_heartbeat(uint8_t pointer, uint8_t counter);

#endif /* __EFFECT_STROBE_PLUGIN_H__ */
