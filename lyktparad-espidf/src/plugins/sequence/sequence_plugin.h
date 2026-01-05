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

/* Helper functions for calculating variable-length sizes based on number of rows */
static inline uint16_t sequence_color_data_size(uint8_t num_rows) {
    /* Calculate packed size: (num_rows * 16 / 2) * 3 = num_rows * 24 */
    return (uint16_t)((num_rows * 16 / 2) * 3);
}

static inline uint16_t sequence_payload_size(uint8_t num_rows) {
    /* HTTP payload: rhythm(1) + length(1) + color_data(variable) */
    return 2 + sequence_color_data_size(num_rows);
}

static inline uint16_t sequence_mesh_cmd_size(uint8_t num_rows) {
    /* Mesh command: cmd(1) + rhythm(1) + length(1) + color_data(variable) */
    return 3 + sequence_color_data_size(num_rows);
}

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
 * @brief Stop sequence playback on root node and broadcast STOP command
 */
esp_err_t sequence_plugin_root_stop(void);

/**
 * @brief Reset sequence pointer to 0 on root node and broadcast RESET command
 */
esp_err_t sequence_plugin_root_reset(void);

/**
 * @brief Get current sequence pointer position (root node only)
 */
uint16_t sequence_plugin_root_get_pointer(void);

/**
 * @brief Check if sequence playback is currently active on root node
 */
bool sequence_plugin_root_is_active(void);

/**
 * @brief Broadcast BEAT command to all child nodes (root node only)
 */
esp_err_t sequence_plugin_root_broadcast_beat(void);

/*******************************************************
 *                Child Node Functions (for mesh_child.c)
 *******************************************************/

/**
 * @brief Stop sequence playback (called from mesh_child.c on RGB command)
 */
void sequence_plugin_node_stop(void);

#endif /* __SEQUENCE_PLUGIN_H__ */
