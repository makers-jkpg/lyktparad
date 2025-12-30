/* Sequence Mode Root Node Module Implementation
 *
 * This module contains root node-specific functionality for sequence mode.
 * Root nodes receive sequence data via HTTP, store it, and broadcast it to all child nodes.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mesh_common.h"
#include "light_neopixel.h"
#include "node_sequence.h"

/* Default Kconfig CONFIG_ defines for standalone compilation */
#ifndef CONFIG_MESH_ROUTE_TABLE_SIZE
#define CONFIG_MESH_ROUTE_TABLE_SIZE 50
#endif

/* Ensure MACSTR is defined - it should be in esp_mac.h */
#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

static const char *SEQ_ROOT_TAG = "mode_seq_root";

/* Forward declarations */
static void sequence_timer_cb(void *arg);
static void sequence_timer_stop(void);
static esp_err_t sequence_timer_start(uint8_t rhythm);
esp_err_t mode_sequence_root_broadcast_beat(void);

/* Static storage for sequence data */
static uint8_t sequence_rhythm = 25;  /* Default: 25 (250ms) */
static uint8_t sequence_colors[SEQUENCE_COLOR_DATA_SIZE];  /* Packed color data (384 bytes) */
static uint16_t sequence_pointer = 0;  /* Current position in sequence (0-255) */
static esp_timer_handle_t sequence_timer = NULL;  /* Timer handle for sequence playback */
static bool sequence_active = false;  /* Playback state */

/*******************************************************
 *                Timer Management
 *******************************************************/

/**
 * Extract RGB values for a square from packed color data
 * (Same logic as child node for consistency)
 */
static void extract_square_rgb(uint8_t *packed_data, uint16_t square_index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (packed_data == NULL || r == NULL || g == NULL || b == NULL) {
        return;
    }

    if (square_index >= 256) {
        return;
    }

    uint16_t pair_index = square_index / 2;
    uint16_t byte_offset = pair_index * 3;

    if (square_index % 2 == 0) {
        /* Even square */
        *r = (packed_data[byte_offset] >> 4) & 0x0F;
        *g = packed_data[byte_offset] & 0x0F;
        *b = (packed_data[byte_offset + 1] >> 4) & 0x0F;
    } else {
        /* Odd square */
        *r = packed_data[byte_offset + 1] & 0x0F;
        *g = (packed_data[byte_offset + 2] >> 4) & 0x0F;
        *b = packed_data[byte_offset + 2] & 0x0F;
    }
}

/**
 * Stop and delete sequence timer
 */
static void sequence_timer_stop(void)
{
    if (sequence_timer != NULL) {
        esp_err_t err = esp_timer_stop(sequence_timer);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(SEQ_ROOT_TAG, "Failed to stop sequence timer: 0x%x", err);
        }

        err = esp_timer_delete(sequence_timer);
        if (err != ESP_OK) {
            ESP_LOGE(SEQ_ROOT_TAG, "Failed to delete sequence timer: 0x%x", err);
        }

        sequence_timer = NULL;
        sequence_active = false;
    }
}

/**
 * Start sequence timer with specified rhythm
 *
 * @param rhythm Rhythm value in 10ms units (1-255)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_timer_start(uint8_t rhythm)
{
    if (rhythm == 0) {
        ESP_LOGE(SEQ_ROOT_TAG, "Invalid rhythm value: %d (must be 1-255)", rhythm);
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate interval: rhythm × 10ms in microseconds */
    uint64_t interval_us = (uint64_t)rhythm * 10000;

    /* Create timer configuration */
    const esp_timer_create_args_t timer_args = {
        .callback = &sequence_timer_cb,
        .arg = NULL,
        .name = "sequence"
    };

    esp_err_t err = esp_timer_create(&timer_args, &sequence_timer);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_ROOT_TAG, "Failed to create sequence timer: 0x%x", err);
        sequence_timer = NULL;
        return err;
    }

    err = esp_timer_start_periodic(sequence_timer, interval_us);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_ROOT_TAG, "Failed to start sequence timer: 0x%x", err);
        esp_timer_delete(sequence_timer);
        sequence_timer = NULL;
        return err;
    }

    sequence_active = true;
    return ESP_OK;
}

/**
 * Timer callback for sequence playback
 * Extracts RGB for current square, scales to 8-bit, and updates LED
 * Sends BEAT command at row boundaries
 */
static void sequence_timer_cb(void *arg)
{
    uint8_t r_4bit, g_4bit, b_4bit;
    uint8_t r_scaled, g_scaled, b_scaled;

    /* Extract RGB for current square */
    extract_square_rgb(sequence_colors, sequence_pointer, &r_4bit, &g_4bit, &b_4bit);

    /* Scale from 4-bit to 8-bit (multiply by 16) */
    r_scaled = r_4bit * 16;
    g_scaled = g_4bit * 16;
    b_scaled = b_4bit * 16;

    /* Update root node's LED */
    esp_err_t err = mesh_light_set_rgb(r_scaled, g_scaled, b_scaled);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_ROOT_TAG, "Failed to set LED in timer callback: 0x%x", err);
    }

    /* Increment pointer and wrap at 256 */
    sequence_pointer = (sequence_pointer + 1) % 256;

    /* Check if pointer is at row boundary (every 16 squares) */
    if ((sequence_pointer % 16) == 0) {
        /* Broadcast BEAT command with current pointer value */
        mode_sequence_root_broadcast_beat();
    }
}

/*******************************************************
 *                Root Node Functions
 *******************************************************/

esp_err_t mode_sequence_root_store_and_broadcast(uint8_t rhythm, uint8_t *color_data)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(SEQ_ROOT_TAG, "Not root node, cannot store and broadcast sequence");
        return ESP_ERR_INVALID_STATE;
    }

    /* Validate inputs */
    /* Validate rhythm range (1-255) - uint8_t cannot exceed 255 */
    if (rhythm == 0) {
        ESP_LOGE(SEQ_ROOT_TAG, "Invalid rhythm value: %d (must be 1-255)", rhythm);
        return ESP_ERR_INVALID_ARG;
    }

    if (color_data == NULL) {
        ESP_LOGE(SEQ_ROOT_TAG, "Color data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Store sequence data */
    sequence_rhythm = rhythm;
    memcpy(sequence_colors, color_data, SEQUENCE_COLOR_DATA_SIZE);
    ESP_LOGI(SEQ_ROOT_TAG, "Sequence data stored - rhythm: %d (%.1f ms)", rhythm, (float)rhythm * 10.0f);

    /* Stop existing timer if running */
    sequence_timer_stop();

    /* Reset pointer to start of sequence */
    sequence_pointer = 0;

    /* Start root node playback */
    esp_err_t timer_err = sequence_timer_start(rhythm);
    if (timer_err != ESP_OK) {
        ESP_LOGE(SEQ_ROOT_TAG, "Failed to start root sequence timer: 0x%x", timer_err);
        /* Continue with broadcast even if timer start failed */
    } else {
        ESP_LOGI(SEQ_ROOT_TAG, "Root sequence playback started");
    }

    /* Get routing table */
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate actual child node count (excluding root node) */
    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

    if (child_node_count == 0) {
        ESP_LOGD(SEQ_ROOT_TAG, "Sequence stored - no child nodes to broadcast");
        return ESP_OK;
    }

    /* Get TX buffer and prepare command data */
    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = MESH_CMD_SEQUENCE;  /* Command byte */
    tx_buf[1] = rhythm;             /* Rhythm byte */
    memcpy(&tx_buf[2], color_data, SEQUENCE_COLOR_DATA_SIZE);  /* Color data (384 bytes) */

    /* Create mesh data structure */
    mesh_data_t data;
    data.data = tx_buf;
    data.size = SEQUENCE_MESH_CMD_SIZE;  /* 386 bytes total */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Broadcast to all child nodes */
    int success_count = 0;
    int fail_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        esp_err_t err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err == ESP_OK) {
            success_count++;
        } else {
            fail_count++;
            ESP_LOGD(SEQ_ROOT_TAG, "Sequence send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    ESP_LOGI(SEQ_ROOT_TAG, "Sequence command broadcast - rhythm:%d, sent to %d/%d child nodes (success:%d, failed:%d)",
             rhythm, success_count, child_node_count, success_count, fail_count);

    return ESP_OK;
}

esp_err_t mode_sequence_root_broadcast_beat(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(SEQ_ROOT_TAG, "Not root node, cannot broadcast BEAT");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get routing table */
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate actual child node count (excluding root node) */
    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

    if (child_node_count == 0) {
        ESP_LOGD(SEQ_ROOT_TAG, "BEAT broadcast - no child nodes");
        return ESP_OK;
    }

    /* Get TX buffer and prepare BEAT command data */
    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = MESH_CMD_SEQUENCE_BEAT;  /* Command byte */
    tx_buf[1] = sequence_pointer & 0xFF;  /* Pointer byte (0-255) */

    /* Create mesh data structure */
    mesh_data_t data;
    data.data = tx_buf;
    data.size = 2;  /* 1 byte command + 1 byte pointer */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Broadcast to all child nodes */
    int success_count = 0;
    int fail_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        esp_err_t err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err == ESP_OK) {
            success_count++;
        } else {
            fail_count++;
            ESP_LOGD(SEQ_ROOT_TAG, "BEAT send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    ESP_LOGI(SEQ_ROOT_TAG, "BEAT command broadcast - pointer:%d, sent to %d/%d child nodes (success:%d, failed:%d)",
             sequence_pointer, success_count, child_node_count, success_count, fail_count);

    return ESP_OK;
}

esp_err_t mode_sequence_root_broadcast_control(uint8_t cmd)
{
    if (cmd != MESH_CMD_SEQUENCE_START && cmd != MESH_CMD_SEQUENCE_STOP && cmd != MESH_CMD_SEQUENCE_RESET) {
        ESP_LOGE(SEQ_ROOT_TAG, "Invalid control command: 0x%02x", cmd);
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        ESP_LOGE(SEQ_ROOT_TAG, "Not root node, cannot broadcast control command");
        return ESP_ERR_INVALID_STATE;
    }

    /* Get routing table */
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate actual child node count (excluding root node) */
    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

    if (child_node_count == 0) {
        ESP_LOGD(SEQ_ROOT_TAG, "Control command broadcast - no child nodes");
        return ESP_OK;
    }

    /* Get TX buffer and prepare command data */
    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = cmd;  /* Command byte */

    /* Create mesh data structure */
    mesh_data_t data;
    data.data = tx_buf;
    data.size = 1;  /* Single-byte command */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Broadcast to all child nodes */
    int success_count = 0;
    int fail_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        esp_err_t err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err == ESP_OK) {
            success_count++;
        } else {
            fail_count++;
            ESP_LOGD(SEQ_ROOT_TAG, "Control send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    const char *cmd_name = (cmd == MESH_CMD_SEQUENCE_START) ? "START" :
                          (cmd == MESH_CMD_SEQUENCE_STOP) ? "STOP" : "RESET";
    ESP_LOGI(SEQ_ROOT_TAG, "%s command broadcast - sent to %d/%d child nodes (success:%d, failed:%d)",
             cmd_name, success_count, child_node_count, success_count, fail_count);

    return ESP_OK;
}

esp_err_t mode_sequence_root_start(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(SEQ_ROOT_TAG, "Not root node, cannot start sequence");
        return ESP_ERR_INVALID_STATE;
    }

    if (sequence_rhythm == 0) {
        ESP_LOGE(SEQ_ROOT_TAG, "No sequence data available");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop existing timer if running */
    sequence_timer_stop();

    /* Reset pointer to start of sequence */
    sequence_pointer = 0;

    /* Start timer */
    esp_err_t err = sequence_timer_start(sequence_rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_ROOT_TAG, "Failed to start sequence timer: 0x%x", err);
        /* Continue with broadcast even if timer start failed */
    }

    /* Broadcast START command */
    esp_err_t broadcast_err = mode_sequence_root_broadcast_control(MESH_CMD_SEQUENCE_START);
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(SEQ_ROOT_TAG, "Failed to broadcast START command: 0x%x", broadcast_err);
    }

    ESP_LOGI(SEQ_ROOT_TAG, "Sequence playback started");
    return err;  /* Return timer start error if any, otherwise ESP_OK */
}

esp_err_t mode_sequence_root_stop(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(SEQ_ROOT_TAG, "Not root node, cannot stop sequence");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop timer (handles NULL timer gracefully) */
    sequence_timer_stop();

    /* Broadcast STOP command */
    esp_err_t broadcast_err = mode_sequence_root_broadcast_control(MESH_CMD_SEQUENCE_STOP);
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(SEQ_ROOT_TAG, "Failed to broadcast STOP command: 0x%x", broadcast_err);
    }

    ESP_LOGI(SEQ_ROOT_TAG, "Sequence playback stopped");
    return ESP_OK;
}

esp_err_t mode_sequence_root_reset(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(SEQ_ROOT_TAG, "Not root node, cannot reset sequence");
        return ESP_ERR_INVALID_STATE;
    }

    /* Reset pointer to start of sequence */
    sequence_pointer = 0;

    /* If sequence is active, restart timer */
    if (sequence_active) {
        sequence_timer_stop();
        esp_err_t err = sequence_timer_start(sequence_rhythm);
        if (err != ESP_OK) {
            ESP_LOGE(SEQ_ROOT_TAG, "Failed to restart sequence timer: 0x%x", err);
            /* Continue with broadcast even if timer restart failed */
        }
    }

    /* Broadcast RESET command */
    esp_err_t broadcast_err = mode_sequence_root_broadcast_control(MESH_CMD_SEQUENCE_RESET);
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(SEQ_ROOT_TAG, "Failed to broadcast RESET command: 0x%x", broadcast_err);
    }

    ESP_LOGI(SEQ_ROOT_TAG, "Sequence pointer reset to 0");
    return ESP_OK;
}
