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
#include "light_neopixel.h"
#include "node_sequence.h"

static const char *SEQ_NODE_TAG = "mode_seq_node";

/* Static storage for sequence data */
static uint8_t sequence_rhythm = 0;  /* 0 = not set */
static uint8_t sequence_colors[SEQUENCE_COLOR_DATA_SIZE];  /* Packed color data (384 bytes) */
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

/*******************************************************
 *                Timer Management
 *******************************************************/

/* Forward declaration */
static void sequence_timer_cb(void *arg);

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

    /* Increment pointer and wrap at 256 */
    sequence_pointer = (sequence_pointer + 1) % 256;
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

    /* Verify command format: 386 bytes total (1 byte command + 1 byte rhythm + 384 bytes color data) */
    if (len != SEQUENCE_MESH_CMD_SIZE) {
        ESP_LOGE(SEQ_NODE_TAG, "Invalid sequence command size: %d (expected %d)", len, SEQUENCE_MESH_CMD_SIZE);
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

    /* Stop and delete existing timer if any */
    sequence_timer_stop();

    /* Store rhythm and color data */
    sequence_rhythm = rhythm;
    memcpy(sequence_colors, &data[2], SEQUENCE_COLOR_DATA_SIZE);

    /* Reset pointer to start of sequence */
    sequence_pointer = 0;

    /* Create and start new timer */
    esp_err_t err = sequence_timer_start(rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(SEQ_NODE_TAG, "Failed to start sequence timer: 0x%x", err);
        return err;
    }

    ESP_LOGI(SEQ_NODE_TAG, "Sequence command received and timer started - rhythm: %d (%.1f ms)",
             rhythm, (float)rhythm * 10.0f);

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
