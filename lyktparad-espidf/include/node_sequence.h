/* Sequence Mode Module Header
 *
 * This module provides sequence mode functionality for the mesh network.
 * Sequence mode allows synchronized playback of color sequences across all mesh nodes.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __NODE_SEQUENCE_H__
#define __NODE_SEQUENCE_H__

#include "esp_err.h"
#include <stdint.h>

/*******************************************************
 *                Constants
 *******************************************************/

/* Sequence data sizes */
#define SEQUENCE_COLOR_DATA_SIZE  (384)  /* Maximum packed color array size (256 squares × 1.5 bytes) - used for storage arrays */

/* Helper functions for calculating variable-length sizes based on number of rows */
static inline uint16_t sequence_color_data_size(uint8_t num_rows) {
    /* Calculate packed size: (num_rows * 16 / 2) * 3 = num_rows * 24 */
    /* Since we always have 16 squares per row, num_rows * 16 is always even */
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
 *                Function Definitions
 *******************************************************/

/* Root node functions (implemented in mode_sequence_root.c) */

/**
 * Store sequence data and broadcast to all child nodes
 *
 * @param rhythm Rhythm value in 10ms units (1-255, where 25 = 250ms)
 * @param num_rows Number of rows in sequence (1-16)
 * @param color_data Pointer to packed color array (variable length, use sequence_color_data_size(num_rows))
 * @param color_data_len Actual length of color_data in bytes
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_root_store_and_broadcast(uint8_t rhythm, uint8_t num_rows, uint8_t *color_data, uint16_t color_data_len);

/**
 * Start sequence playback on root node and broadcast START command
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_root_start(void);

/**
 * Stop sequence playback on root node and broadcast STOP command
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_root_stop(void);

/**
 * Reset sequence pointer to 0 on root node and broadcast RESET command
 * If sequence is active, restarts timer from beginning
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_root_reset(void);

/**
 * Broadcast BEAT command to all child nodes for tempo synchronization
 * Includes root's current pointer position in the message
 * Called automatically by root node timer at row boundaries
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_root_broadcast_beat(void);

/**
 * Get current sequence pointer position
 * Returns the current position in the sequence (0-255)
 * @return Current sequence pointer value (0-255)
 */
uint16_t mode_sequence_root_get_pointer(void);

/* Child node functions (implemented in mode_sequence_node.c) */

/**
 * Handle sequence command received from mesh network
 *
 * @param cmd Command byte (should be MESH_CMD_SEQUENCE)
 * @param data Pointer to command data (variable length: cmd + rhythm + length + color data)
 * @param len Length of data (use sequence_mesh_cmd_size(num_rows) to calculate expected size)
 * @return ESP_OK on success, ESP_FAIL on validation failure
 */
esp_err_t mode_sequence_node_handle_command(uint8_t cmd, uint8_t *data, uint16_t len);

/**
 * Stop sequence playback
 * Stops and deletes the sequence timer, clearing the active state
 */
void mode_sequence_node_stop(void);

    /**
     * Handle control command received from mesh network
     * Supports START, STOP, RESET, and BEAT commands
     * @param cmd Command byte (MESH_CMD_SEQUENCE_START, STOP, RESET, or BEAT)
     * @param data Pointer to command data (for BEAT: includes 1-byte pointer)
     * @param len Length of data (1 for START/STOP/RESET, 2 for BEAT)
     * @return ESP_OK on success, error code on failure
     */
esp_err_t mode_sequence_node_handle_control(uint8_t cmd, uint8_t *data, uint16_t len);

/**
 * Start sequence playback on child node
 * Starts timer and resets pointer to 0
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_node_start(void);

/**
 * Reset sequence pointer to 0 on child node
 * If sequence is active, restarts timer from beginning
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_node_reset(void);

/**
 * Handle BEAT command for tempo synchronization
 * Sets pointer to value from BEAT message and resets timer
 * Child nodes continue playing independently even when BEAT messages are lost
 * @param received_pointer Pointer position from BEAT message (0-255)
 * @return ESP_OK on success, error code on failure (ignored if sequence not active)
 */
esp_err_t mode_sequence_node_handle_beat(uint16_t received_pointer);

#endif /* __NODE_SEQUENCE_H__ */
