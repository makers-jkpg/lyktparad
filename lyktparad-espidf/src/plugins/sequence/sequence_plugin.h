/* Sequence Plugin Header
 *
 * This module provides sequence mode functionality as a plugin.
 * Sequence mode allows synchronized playback of color sequences across all mesh nodes.
 * This plugin handles both root and child node logic.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __SEQUENCE_PLUGIN_H__
#define __SEQUENCE_PLUGIN_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Constants
 *******************************************************/

#define SEQUENCE_COLOR_DATA_SIZE  (384)  /* Maximum packed color array size (256 squares × 1.5 bytes) */

/* Helper functions moved to plugin implementation - use plugin_get_helper() instead */

/*******************************************************
 *                Plugin Registration
 *******************************************************/

/**
 * @brief Register the sequence plugin with the plugin system
 *
 * This function should be called during system initialization to register
 * the sequence plugin. The plugin will be assigned a command ID automatically.
 */
void sequence_plugin_register(void);

/*******************************************************
 *                Root Node Functions (for API handlers)
 *******************************************************/

/**
 * @brief Store sequence data and broadcast to all child nodes (root node only)
 */
esp_err_t sequence_plugin_root_store_and_broadcast(uint8_t rhythm, uint8_t num_rows, uint8_t *color_data, uint16_t color_data_len);

/**
 * @brief Start sequence playback on root node and broadcast START command
 */
esp_err_t sequence_plugin_root_start(void);

/**
 * @brief Pause sequence playback on root node and broadcast PAUSE command
 */
esp_err_t sequence_plugin_root_pause(void);

/**
 * @brief Reset sequence pointer to 0 on root node and broadcast RESET command
 */
esp_err_t sequence_plugin_root_reset(void);

/**
 * @brief Get current sequence pointer position (root node only)
 */
uint16_t sequence_plugin_root_get_pointer(void);

/**
 * @brief Get sequence pointer for heartbeat (returns 0 if sequence inactive)
 *
 * This function returns the current sequence pointer as a 1-byte value (0-255)
 * if the sequence plugin is active, or 0 if the plugin is inactive.
 * Used by heartbeat timer to include sequence pointer in heartbeat payload.
 *
 * @return Sequence pointer (0-255) if active, 0 if inactive
 */
uint8_t sequence_plugin_get_pointer_for_heartbeat(void);

/**
 * @brief Check if sequence playback is currently active on root node
 */
bool sequence_plugin_root_is_active(void);

/**
 * @brief Handle heartbeat command from mesh (child nodes only)
 *
 * This function is called when a heartbeat is received via mesh.
 * It extracts the pointer and counter from the heartbeat payload
 * and updates the sequence pointer if the sequence plugin is active.
 *
 * @param pointer Sequence pointer from heartbeat (0-255)
 * @param counter Synchronization counter from heartbeat (0-255)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t sequence_plugin_handle_heartbeat(uint8_t pointer, uint8_t counter);

/*******************************************************
 *                Child Node Functions (for mesh_child.c)
 *******************************************************/

/**
 * @brief Pause sequence playback on child node
 *
 * Note: This function is available for manual pausing but is not automatically
 * called when RGB commands are received. RGB commands are now handled through
 * the plugin system, and plugins control LEDs exclusively when active.
 */
void sequence_plugin_node_pause(void);

#endif /* __SEQUENCE_PLUGIN_H__ */
