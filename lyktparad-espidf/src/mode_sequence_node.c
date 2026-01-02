/* Sequence Mode Node Module Implementation
 *
 * This module contains child node-specific functionality for sequence mode.
 * Child nodes receive sequence commands from the mesh network and store the data.
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
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mesh_commands.h"
#include "light_neopixel.h"
#include "node_sequence.h"

static const char *SEQ_NODE_TAG = "mode_seq_node";

/* Static storage for sequence data */
static uint8_t sequence_rhythm = 0;  /* 0 = not set */
static uint8_t sequence_colors[SEQUENCE_COLOR_DATA_SIZE];  /* Packed color data (384 bytes) */
static uint8_t sequence_length = 16;  /* Sequence length in rows (1-16), default 16 for backward compatibility */
static uint16_t sequence_pointer = 0;  /* Current position in sequence (0-255) */
static esp_timer_handle_t sequence_timer = NULL;  /* Timer handle for sequence playback */
static bool sequence_active = false;  /* Playback state */

/*******************************************************
 *                Color Extraction
 *******************************************************/

/**
 * Extract RGB values for a square from packed color data
 *
 * Packed format: 2 squares = 3 bytes
 * - Square N (even): R in upper nibble of byte[0], G in lower nibble of byte[0], B in upper nibble of byte[1]
 * - Square N+1 (odd): R in lower nibble of byte[1], G in upper nibble of byte[2], B in lower nibble of byte[2]
 *
 * @param packed_data Pointer to packed color array (384 bytes)
 * @param square_index Square index (0-255)
 * @param r Output pointer for red value (4-bit, 0-15)
 * @param g Output pointer for green value (4-bit, 0-15)
 * @param b Output pointer for blue value (4-bit, 0-15)
 */
static void extract_square_rgb(uint8_t *packed_data, uint16_t square_index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (packed_data == NULL || r == NULL || g == NULL || b == NULL) {
        return;
    }

    /* Validate square index against current sequence length */
    if (square_index >= (sequence_length * 16)) {
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

/*******************************************************
 *                Timer Management
 *******************************************************/

/* Forward declarations */
static void sequence_timer_cb(void *arg);
static esp_err_t sequence_timer_reset(void);
esp_err_t mode_sequence_node_start(void);
esp_err_t mode_sequence_node_reset(void);
esp_err_t mode_sequence_node_handle_beat(uint16_t received_pointer);

/**
 * Stop and delete sequence timer
 */
static void sequence_timer_stop(void)
{
    if (sequence_timer != NULL) {
        esp_err_t err = esp_timer_stop(sequence_timer);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(SEQ_NODE_TAG, "Failed to stop sequence timer: 0x%x", err);
        }

        err = esp_timer_delete(sequence_timer);
        if (err != ESP_OK) {
            ESP_LOGE(SEQ_NODE_TAG, "Failed to delete sequence timer: 0x%x", err);
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
        ESP_LOGE(SEQ_NODE_TAG, "Invalid rhythm value: %d (must be 1-255)", rhythm);
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
        ESP_LOGE(SEQ_NODE_TAG, "Failed to create sequence timer: 0x%x", err);
        sequence_timer = NULL;
        return err;
    }

    err = esp_timer_start_periodic(sequence_timer, interval_us);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to start sequence timer: 0x%x", err);
        esp_timer_delete(sequence_timer);
        sequence_timer = NULL;
        return err;
    }

    sequence_active = true;
    return ESP_OK;
}

/*******************************************************
 *                Timer Callback
 *******************************************************/

/**
 * Timer callback for sequence playback
 * Extracts RGB for current square, scales to 8-bit, and updates LED
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

    /* Update LED */
    esp_err_t err = mesh_light_set_rgb(r_scaled, g_scaled, b_scaled);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to set LED in timer callback: 0x%x", err);
    }

    /* Calculate maximum squares for current sequence length */
    uint16_t max_squares = sequence_length * 16;

    /* Increment pointer and wrap at sequence length */
    sequence_pointer = (sequence_pointer + 1) % max_squares;
}

/*******************************************************
 *                Node Functions
 *******************************************************/

esp_err_t mode_sequence_node_handle_command(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* Validate inputs */
    if (data == NULL) {
        ESP_LOGE(SEQ_NODE_TAG, "Command data pointer is NULL");
        return ESP_FAIL;
    }

    if (len < 1) {
        ESP_LOGE(SEQ_NODE_TAG, "Command data length too small: %d", len);
        return ESP_FAIL;
    }

    /* Check if this is a SEQUENCE command */
    if (cmd != MESH_CMD_SEQUENCE) {
        ESP_LOGD(SEQ_NODE_TAG, "Not a sequence command: 0x%02x", cmd);
        return ESP_FAIL;
    }

    /* Verify minimum command size (cmd + rhythm + length = 3 bytes) */
    if (len < 3) {
        ESP_LOGE(SEQ_NODE_TAG, "Invalid sequence command size: %d (need at least 3 bytes)", len);
        return ESP_FAIL;
    }

    /* Verify command byte */
    if (data[0] != MESH_CMD_SEQUENCE) {
        ESP_LOGE(SEQ_NODE_TAG, "Command byte mismatch: 0x%02x (expected 0x%02x)", data[0], MESH_CMD_SEQUENCE);
        return ESP_FAIL;
    }

    /* Extract rhythm value */
    uint8_t rhythm = data[1];

    /* Validate rhythm value (1-255) - reject if invalid */
    if (rhythm == 0) {
        ESP_LOGE(SEQ_NODE_TAG, "Invalid rhythm value received: %d (must be 1-255)", rhythm);
        return ESP_FAIL;
    }

    /* Extract sequence length (number of rows, 1-16) */
    uint8_t num_rows = data[2];

    /* Validate sequence length (1-16) - reject if invalid */
    if (num_rows < 1 || num_rows > 16) {
        ESP_LOGE(SEQ_NODE_TAG, "Invalid sequence length received: %d (must be 1-16 rows)", num_rows);
        return ESP_FAIL;
    }

    /* Calculate expected command size */
    uint16_t expected_size = sequence_mesh_cmd_size(num_rows);

    /* Verify command format matches expected size */
    if (len != expected_size) {
        ESP_LOGE(SEQ_NODE_TAG, "Invalid sequence command size: %d (expected %d for %d rows)", len, expected_size, num_rows);
        return ESP_FAIL;
    }

    /* Calculate color data length */
    uint16_t color_data_len = len - 3;

    /* Stop and delete existing timer if any */
    sequence_timer_stop();

    /* Store rhythm, length, and color data with padding */
    sequence_rhythm = rhythm;
    sequence_length = num_rows;
    memset(sequence_colors, 0, SEQUENCE_COLOR_DATA_SIZE);  /* Clear array (padding with zeros) */
    memcpy(sequence_colors, &data[3], color_data_len);      /* Copy received data */

    /* Reset pointer to start of sequence */
    sequence_pointer = 0;

    /* Create and start new timer */
    esp_err_t err = sequence_timer_start(rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to start sequence timer: 0x%x", err);
        return err;
    }

    ESP_LOGI(SEQ_NODE_TAG, "Sequence command received and timer started - rhythm: %d (%.1f ms), length: %d rows",
             rhythm, (float)rhythm * 10.0f, num_rows);

    return ESP_OK;
}

/**
 * Stop sequence playback
 * Called externally (e.g., when RGB command is received)
 */
void mode_sequence_node_stop(void)
{
    sequence_timer_stop();
}

/**
 * Reset sequence timer while preserving rhythm interval
 * Internal helper function for BEAT synchronization
 */
static esp_err_t sequence_timer_reset(void)
{
    if (!sequence_active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sequence_rhythm == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop current timer */
    esp_err_t err = esp_timer_stop(sequence_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to stop timer for reset: 0x%x", err);
    }

    /* Delete old timer before creating new one (prevents memory leak) */
    err = esp_timer_delete(sequence_timer);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to delete timer for reset: 0x%x", err);
    }
    sequence_timer = NULL;

    /* Restart timer with same rhythm */
    err = sequence_timer_start(sequence_rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to restart timer: 0x%x", err);
        sequence_active = false;  /* Ensure state is consistent on failure */
        return err;
    }

    return ESP_OK;
}

esp_err_t mode_sequence_node_start(void)
{
    if (sequence_rhythm == 0) {
        ESP_LOGE(SEQ_NODE_TAG, "No sequence data available");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop existing timer if running */
    sequence_timer_stop();

    /* Reset pointer to start of sequence */
    sequence_pointer = 0;

    /* Start timer */
    esp_err_t err = sequence_timer_start(sequence_rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to start sequence timer: 0x%x", err);
        return err;
    }

    ESP_LOGI(SEQ_NODE_TAG, "Sequence playback started");
    return ESP_OK;
}

esp_err_t mode_sequence_node_reset(void)
{
    /* Reset pointer to start of sequence */
    sequence_pointer = 0;

    /* If sequence is active, restart timer */
    if (sequence_active && sequence_rhythm != 0) {
        sequence_timer_stop();
        esp_err_t err = sequence_timer_start(sequence_rhythm);
        if (err != ESP_OK) {
            ESP_LOGE(SEQ_NODE_TAG, "Failed to restart sequence timer: 0x%x", err);
            return err;
        }
    }

    ESP_LOGI(SEQ_NODE_TAG, "Sequence pointer reset to 0");
    return ESP_OK;
}

esp_err_t mode_sequence_node_handle_beat(uint16_t received_pointer)
{
    /* If sequence is not active, ignore BEAT (not an error) */
    if (!sequence_active) {
        ESP_LOGD(SEQ_NODE_TAG, "BEAT received but sequence not active, ignoring");
        return ESP_OK;
    }

    /* Calculate maximum squares for current sequence length */
    uint16_t max_squares = sequence_length * 16;

    /* Validate received pointer against sequence length */
    if (received_pointer >= max_squares) {
        ESP_LOGE(SEQ_NODE_TAG, "Invalid pointer value in BEAT: %d (must be 0-%d for %d-row sequence)", received_pointer, max_squares - 1, sequence_length);
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t old_pointer = sequence_pointer;

    /* Set local pointer to received value (no calculation) */
    sequence_pointer = received_pointer;

    /* Reset timer to resynchronize */
    esp_err_t err = sequence_timer_reset();
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to reset timer on BEAT: 0x%x", err);
        return err;
    }

    ESP_LOGD(SEQ_NODE_TAG, "BEAT synchronization - pointer: %d -> %d", old_pointer, received_pointer);
    return ESP_OK;
}

esp_err_t mode_sequence_node_handle_control(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* Validate command */
    if (cmd != MESH_CMD_SEQUENCE_START && cmd != MESH_CMD_SEQUENCE_STOP &&
        cmd != MESH_CMD_SEQUENCE_RESET && cmd != MESH_CMD_SEQUENCE_BEAT) {
        ESP_LOGE(SEQ_NODE_TAG, "Invalid control command: 0x%02x", cmd);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate data pointer */
    if (data == NULL) {
        ESP_LOGE(SEQ_NODE_TAG, "Control command data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    /* Handle each command */
    if (cmd == MESH_CMD_SEQUENCE_START) {
        if (len != 1) {
            ESP_LOGE(SEQ_NODE_TAG, "Invalid START command size: %d (expected 1)", len);
            return ESP_ERR_INVALID_ARG;
        }
        err = mode_sequence_node_start();
    } else if (cmd == MESH_CMD_SEQUENCE_STOP) {
        if (len != 1) {
            ESP_LOGE(SEQ_NODE_TAG, "Invalid STOP command size: %d (expected 1)", len);
            return ESP_ERR_INVALID_ARG;
        }
        mode_sequence_node_stop();
        err = ESP_OK;
    } else if (cmd == MESH_CMD_SEQUENCE_RESET) {
        if (len != 1) {
            ESP_LOGE(SEQ_NODE_TAG, "Invalid RESET command size: %d (expected 1)", len);
            return ESP_ERR_INVALID_ARG;
        }
        err = mode_sequence_node_reset();
    } else if (cmd == MESH_CMD_SEQUENCE_BEAT) {
        if (len != 2) {
            ESP_LOGE(SEQ_NODE_TAG, "Invalid BEAT command size: %d (expected 2)", len);
            return ESP_ERR_INVALID_ARG;
        }
        /* Extract pointer from BEAT message (single byte) */
        uint16_t received_pointer = data[1];
        err = mode_sequence_node_handle_beat(received_pointer);
    }

    if (err == ESP_OK) {
        const char *cmd_name = (cmd == MESH_CMD_SEQUENCE_START) ? "START" :
                              (cmd == MESH_CMD_SEQUENCE_STOP) ? "STOP" :
                              (cmd == MESH_CMD_SEQUENCE_RESET) ? "RESET" : "BEAT";
        ESP_LOGD(SEQ_NODE_TAG, "Control command received: %s", cmd_name);
    }

    return err;
}

bool mode_sequence_node_is_active(void)
{
    return sequence_active;
}
