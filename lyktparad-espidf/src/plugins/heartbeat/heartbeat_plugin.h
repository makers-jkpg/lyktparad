/* Heartbeat Plugin Header
 *
 * This module provides heartbeat functionality as a plugin.
 * The heartbeat plugin maintains a heartbeat timer (root node only) that broadcasts
 * MESH_CMD_HEARTBEAT with a 4-byte counter, cycles RGB LEDs through 6 colors based
 * on the heartbeat counter, and handles MESH_CMD_HEARTBEAT and MESH_CMD_LIGHT_ON_OFF commands.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __HEARTBEAT_PLUGIN_H__
#define __HEARTBEAT_PLUGIN_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Plugin Registration
 *******************************************************/

/**
 * @brief Register the heartbeat plugin with the plugin system
 *
 * This function should be called during system initialization to register
 * the heartbeat plugin. The plugin will be assigned a command ID automatically.
 */
void heartbeat_plugin_register(void);

#endif /* __HEARTBEAT_PLUGIN_H__ */
