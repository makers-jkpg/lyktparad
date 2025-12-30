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
#define SEQUENCE_COLOR_DATA_SIZE  (384)  /* Packed color array size (256 squares × 1.5 bytes) */
#define SEQUENCE_PAYLOAD_SIZE     (385)  /* HTTP payload size (1 byte rhythm + 384 bytes color data) */
#define SEQUENCE_MESH_CMD_SIZE    (386)  /* Mesh command size (1 byte command + 1 byte rhythm + 384 bytes color data) */

/*******************************************************
 *                Function Definitions
 *******************************************************/

/* Root node functions (implemented in mode_sequence_root.c) */

/**
 * Store sequence data and broadcast to all child nodes
 *
 * @param rhythm Rhythm value in 10ms units (1-255, where 25 = 250ms)
 * @param color_data Pointer to packed color array (384 bytes)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mode_sequence_root_store_and_broadcast(uint8_t rhythm, uint8_t *color_data);

/* Child node functions (implemented in mode_sequence_node.c) */

/**
 * Handle sequence command received from mesh network
 *
 * @param cmd Command byte (should be MESH_CMD_SEQUENCE)
 * @param data Pointer to command data (should be 386 bytes total)
 * @param len Length of data (should be 386)
 * @return ESP_OK on success, ESP_FAIL on validation failure
 */
esp_err_t mode_sequence_node_handle_command(uint8_t cmd, uint8_t *data, uint16_t len);

/**
 * Stop sequence playback
 * Stops and deletes the sequence timer, clearing the active state
 */
void mode_sequence_node_stop(void);

#endif /* __NODE_SEQUENCE_H__ */
