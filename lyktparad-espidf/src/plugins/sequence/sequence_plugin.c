/* Sequence Plugin Implementation
 *
 * This module implements sequence mode functionality as a plugin.
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

#include "sequence_plugin.h"
#include "plugin_system.h"
#include "plugin_light.h"
#include "mesh_commands.h"
#include "mesh_common.h"
#include "light_neopixel.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "sequence_plugin";

/* Plugin ID storage (assigned during registration) */
static uint8_t sequence_plugin_id = 0;

/* Query type constants for get_state callback */
#define SEQUENCE_QUERY_IS_ACTIVE  0x01
#define SEQUENCE_QUERY_GET_POINTER 0x02
#define SEQUENCE_QUERY_GET_RHYTHM  0x03
#define SEQUENCE_QUERY_GET_LENGTH  0x04

/* Operation type constants for execute_operation callback */
#define SEQUENCE_OP_STORE          0x01
#define SEQUENCE_OP_START          0x02
#define SEQUENCE_OP_PAUSE          0x03
#define SEQUENCE_OP_RESET          0x04

/* Helper type constants for get_helper callback */
#define SEQUENCE_HELPER_PAYLOAD_SIZE    0x01
#define SEQUENCE_HELPER_MESH_CMD_SIZE   0x02
#define SEQUENCE_HELPER_COLOR_DATA_SIZE 0x03

/* Default CONFIG_MESH_ROUTE_TABLE_SIZE if not defined */
#ifndef CONFIG_MESH_ROUTE_TABLE_SIZE
#define CONFIG_MESH_ROUTE_TABLE_SIZE 50
#endif

/* Ensure MACSTR is defined */
#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

/* Hardcoded RGB-rainbow default sequence data
 * Source: RGB-rainbow.csv
 * Format: Packed format, 2 squares per 3 bytes
 * Packing: byte0=(r0<<4)|g0, byte1=(b0<<4)|r1, byte2=(g1<<4)|b1
 * Size: 384 bytes (256 squares × 1.5 bytes per square)
 * Usage: Default sequence data loaded if no user data exists
 */
static const uint8_t sequence_default_rgb_rainbow[384] = {
    0x8E, 0x18, 0xE1, 0x8E, 0x18, 0xE1, 0x8D, 0x19, 0xD1, 0x9D, 0x09, 0xD0,
    0x9D, 0x09, 0xD0, 0xAD, 0x0A, 0xD0, 0xAC, 0x0A, 0xC0, 0xAC, 0x0A, 0xC0,
    0xBC, 0x0B, 0xC0, 0xBC, 0x0B, 0xB0, 0xBB, 0x0B, 0xB0, 0xCB, 0x0C, 0xB0,
    0xCB, 0x0C, 0xA0, 0xCA, 0x0C, 0xA0, 0xCA, 0x0D, 0xA0, 0xDA, 0x0D, 0x90,
    0xD9, 0x0D, 0x90, 0xD9, 0x0D, 0x90, 0xD9, 0x1E, 0x81, 0xE8, 0x1E, 0x81,
    0xE8, 0x1E, 0x81, 0xE7, 0x1E, 0x71, 0xE7, 0x1E, 0x71, 0xE7, 0x1E, 0x72,
    0xE6, 0x2F, 0x62, 0xF6, 0x2F, 0x62, 0xF6, 0x2F, 0x52, 0xF5, 0x2F, 0x53,
    0xF5, 0x3F, 0x53, 0xF5, 0x3F, 0x43, 0xF4, 0x3F, 0x43, 0xF4, 0x4F, 0x44,
    0xF4, 0x4F, 0x34, 0xF3, 0x4F, 0x34, 0xF3, 0x5F, 0x35, 0xF3, 0x5F, 0x35,
    0xF2, 0x5F, 0x25, 0xF2, 0x6F, 0x26, 0xF2, 0x6F, 0x26, 0xE2, 0x6E, 0x27,
    0xE1, 0x7E, 0x17, 0xE1, 0x7E, 0x17, 0xE1, 0x7E, 0x18, 0xE1, 0x8E, 0x18,
    0xE1, 0x8E, 0x18, 0xD1, 0x9D, 0x09, 0xD0, 0x9D, 0x09, 0xD0, 0x9D, 0x09,
    0xD0, 0xAD, 0x0A, 0xC0, 0xAC, 0x0A, 0xC0, 0xAC, 0x0A, 0xC0, 0xBC, 0x0B,
    0xC0, 0xBB, 0x0B, 0xB0, 0xBB, 0x0B, 0xB0, 0xCB, 0x0C, 0xB0, 0xCA, 0x0C,
    0xA0, 0xCA, 0x0C, 0xA0, 0xCA, 0x0D, 0xA0, 0xD9, 0x0D, 0x90, 0xD9, 0x0D,
    0x90, 0xD9, 0x1D, 0x81, 0xD8, 0x1E, 0x81, 0xE8, 0x1E, 0x81, 0xE8, 0x1E,
    0x71, 0xE7, 0x1E, 0x71, 0xE7, 0x1E, 0x72, 0xE6, 0x2E, 0x62, 0xF6, 0x2F,
    0x62, 0xF6, 0x2F, 0x52, 0xF5, 0x2F, 0x53, 0xF5, 0x3F, 0x53, 0xF5, 0x3F,
    0x43, 0xF4, 0x3F, 0x43, 0xF4, 0x4F, 0x44, 0xF4, 0x4F, 0x34, 0xF3, 0x4F,
    0x34, 0xF3, 0x5F, 0x35, 0xF3, 0x5F, 0x35, 0xF2, 0x5F, 0x25, 0xF2, 0x6F,
    0x26, 0xF2, 0x6F, 0x26, 0xF2, 0x6F, 0x26, 0xE1, 0x7E, 0x17, 0xE1, 0x7E,
    0x17, 0xE1, 0x7E, 0x18, 0xE1, 0x8E, 0x18, 0xE1, 0x8E, 0x18, 0xE1, 0x8D,
    0x19, 0xD0, 0x9D, 0x09, 0xD0, 0x9D, 0x09, 0xD0, 0xAD, 0x0A, 0xD0, 0xAC,
    0x0A, 0xC0, 0xAC, 0x0A, 0xC0, 0xBC, 0x0B, 0xC0, 0xBC, 0x0B, 0xB0, 0xBB,
    0x0B, 0xB0, 0xCB, 0x0C, 0xB0, 0xCB, 0x0C, 0xA0, 0xCA, 0x0C, 0xA0, 0xCA,
    0x0D, 0xA0, 0xDA, 0x0D, 0x90, 0xD9, 0x0D, 0x90, 0xD9, 0x1D, 0x91, 0xD8,
    0x1E, 0x81, 0xE8, 0x1E, 0x81, 0xE8, 0x1E, 0x81, 0xE7, 0x1E, 0x71, 0xE7,
    0x1E, 0x71, 0xE7, 0x2E, 0x62, 0xF6, 0x2F, 0x62, 0xF6, 0x2F, 0x62, 0xF6,
    0x2F, 0x52, 0xF5, 0x3F, 0x53, 0xF5, 0x3F, 0x53, 0xF5, 0x3F, 0x43, 0xF4,
    0x3F, 0x44, 0xF4, 0x4F, 0x44, 0xF4, 0x4F, 0x34, 0xF3, 0x4F, 0x35, 0xF3,
    0x5F, 0x35, 0xF3, 0x5F, 0x35, 0xF2, 0x5F, 0x26, 0xF2, 0x6F, 0x26, 0xF2,
    0x6F, 0x26, 0xE2, 0x7E, 0x27, 0xE1, 0x7E, 0x17, 0xE1, 0x7E, 0x18, 0xE1
};

/* Static storage for sequence data */
static uint8_t sequence_rhythm = 25;  /* Default: 25 (250ms) */
static uint8_t sequence_colors[SEQUENCE_COLOR_DATA_SIZE];  /* Packed color data (384 bytes) */
static uint8_t sequence_length = 16;  /* Sequence length in rows (1-16), default 16 for backward compatibility */
static uint16_t sequence_pointer = 0;  /* Current position in sequence (0-255) */
static esp_timer_handle_t sequence_timer = NULL;  /* Timer handle for sequence playback */
static bool sequence_active = false;  /* Playback state */

/* Forward declarations */
static void sequence_timer_cb(void *arg);
static void sequence_timer_stop(void);
static esp_err_t sequence_timer_start(uint8_t rhythm);
static void extract_square_rgb(uint8_t *packed_data, uint16_t square_index, uint8_t *r, uint8_t *g, uint8_t *b);
static esp_err_t sequence_handle_command_internal(uint8_t cmd, uint8_t *data, uint16_t len);
static esp_err_t sequence_load_default_data(void);

/*******************************************************
 *                Helper Functions
 *******************************************************/

static void extract_square_rgb(uint8_t *packed_data, uint16_t square_index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (packed_data == NULL || r == NULL || g == NULL || b == NULL) {
        return;
    }

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

static void sequence_timer_stop(void)
{
    if (sequence_timer != NULL) {
        esp_timer_stop(sequence_timer);
        esp_timer_delete(sequence_timer);
        sequence_timer = NULL;
    }
    /* Always set sequence_active to false to ensure state consistency,
     * even if timer is already NULL (handles inconsistent state) */
    sequence_active = false;
}

static esp_err_t sequence_timer_start(uint8_t rhythm)
{
    if (rhythm == 0) {
        ESP_LOGE(TAG, "Invalid rhythm value: %d (must be 1-255)", rhythm);
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t interval_us = (uint64_t)rhythm * 10000;
    const esp_timer_create_args_t timer_args = {
        .callback = &sequence_timer_cb,
        .arg = NULL,
        .name = "sequence"
    };

    esp_err_t err = esp_timer_create(&timer_args, &sequence_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sequence timer: 0x%x", err);
        sequence_timer = NULL;
        return err;
    }

    err = esp_timer_start_periodic(sequence_timer, interval_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sequence timer: 0x%x", err);
        esp_timer_delete(sequence_timer);
        sequence_timer = NULL;
        sequence_active = false;  /* Ensure state is consistent on failure */
        return err;
    }

    sequence_active = true;
    return ESP_OK;
}

/*******************************************************
 *                Timer Callback
 *******************************************************/

static void sequence_timer_cb(void *arg)
{
    /* Check if sequence plugin is active */
    if (!plugin_is_active("sequence")) {
        ESP_LOGW(TAG, "Sequence timer callback called but plugin is not active, stopping timer");
        sequence_timer_stop();
        return;
    }

    /* Defensive check: timer should never run with invalid sequence data */
    if (sequence_length == 0 || sequence_rhythm == 0) {
        ESP_LOGE(TAG, "Timer callback called with invalid sequence data (length=%d, rhythm=%d), stopping timer", sequence_length, sequence_rhythm);
        sequence_timer_stop();
        return;
    }

    /* Read pointer once to ensure consistency between extraction and increment */
    /* This prevents race conditions if heartbeat updates pointer between read and increment */
    uint16_t current_pointer = sequence_pointer;
    uint16_t max_squares = sequence_length * 16;

    uint8_t r_4bit, g_4bit, b_4bit;
    uint8_t r_scaled, g_scaled, b_scaled;

    extract_square_rgb(sequence_colors, current_pointer, &r_4bit, &g_4bit, &b_4bit);

    r_scaled = r_4bit * 16;
    g_scaled = g_4bit * 16;
    b_scaled = b_4bit * 16;

    esp_err_t err = plugin_set_rgb(r_scaled, g_scaled, b_scaled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LED in timer callback: 0x%x", err);
    }

    /* Increment pointer using the value we read at the start (ensures consistency) */
    sequence_pointer = (current_pointer + 1) % max_squares;

    /* Root node: heartbeat timer will include sequence pointer in heartbeat payload */
}

/*******************************************************
 *                Plugin Command Handler
 *******************************************************/

/**
 * @brief Sequence plugin command handler
 *
 * Handles plugin data commands:
 * - PLUGIN_CMD_DATA (0x04): Store and start sequence data
 *
 * Note: PLUGIN_CMD_START, PLUGIN_CMD_PAUSE, and PLUGIN_CMD_RESET are handled via plugin callbacks (on_start, on_pause, on_reset).
 * Sequence synchronization is now handled via MESH_CMD_HEARTBEAT.
 *
 * The plugin ID has already been validated by the plugin system before this handler is called.
 *
 * @param data Command data (data[0] is command byte, e.g., PLUGIN_CMD_DATA)
 * @param len Data length (includes command byte)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_command_handler(uint8_t *data, uint16_t len)
{
    /* Validate data */
    if (data == NULL || len < 1) {
        ESP_LOGE(TAG, "Invalid command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract command byte from data[0] */
    uint8_t cmd = data[0];

    /* Only handle PLUGIN_CMD_DATA (0x04) */
    if (cmd == PLUGIN_CMD_DATA) {
        return sequence_handle_command_internal(cmd, data, len);
    }

    ESP_LOGE(TAG, "Invalid command byte: 0x%02X (expected PLUGIN_CMD_DATA = 0x04)", cmd);
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t sequence_handle_command_internal(uint8_t cmd, uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 5) {
        ESP_LOGE(TAG, "Invalid command data: data=%p, len=%d (need at least 5 bytes: CMD + LENGTH + rhythm + num_rows)", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    if (data[0] != PLUGIN_CMD_DATA) {
        ESP_LOGE(TAG, "Command byte mismatch: 0x%02X (expected PLUGIN_CMD_DATA = 0x04)", data[0]);
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract length prefix (2 bytes, network byte order) */
    uint16_t data_len = (data[1] << 8) | data[2];

    /* Verify length matches */
    if (len != 3 + data_len) {
        ESP_LOGE(TAG, "Length mismatch: len=%d, expected %d (3 header bytes + %d data bytes)", len, 3 + data_len, data_len);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Extract rhythm and num_rows from data payload */
    uint8_t rhythm = data[3];
    if (rhythm == 0) {
        ESP_LOGE(TAG, "Invalid rhythm value: %d (must be 1-255)", rhythm);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t num_rows = data[4];
    if (num_rows < 1 || num_rows > 16) {
        ESP_LOGE(TAG, "Invalid sequence length: %d (must be 1-16 rows)", num_rows);
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate expected data length: rhythm(1) + num_rows(1) + color_data(variable) */
    uint16_t expected_data_len = 2 + ((num_rows * 16 / 2) * 3);
    if (data_len != expected_data_len) {
        ESP_LOGE(TAG, "Invalid sequence data length: %d (expected %d for %d rows)", data_len, expected_data_len, num_rows);
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t color_data_len = data_len - 2;

    /* Validate color_data_len doesn't exceed buffer size */
    if (color_data_len > SEQUENCE_COLOR_DATA_SIZE) {
        ESP_LOGE(TAG, "Color data length %d exceeds maximum %d", color_data_len, SEQUENCE_COLOR_DATA_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    sequence_timer_stop();

    sequence_rhythm = rhythm;
    sequence_length = num_rows;
    memset(sequence_colors, 0, SEQUENCE_COLOR_DATA_SIZE);
    memcpy(sequence_colors, &data[5], color_data_len);
    sequence_pointer = 0;

    esp_err_t err = sequence_timer_start(rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sequence timer: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "Sequence command received and timer started - rhythm: %d (%.1f ms), length: %d rows",
             rhythm, (float)rhythm * 10.0f, num_rows);

    return ESP_OK;
}

static bool sequence_is_active(void)
{
    return sequence_active;
}

/**
 * @brief Load hardcoded RGB-rainbow default sequence data
 *
 * This function loads the hardcoded RGB-rainbow sequence data into the sequence
 * plugin's RAM buffer. It sets the rhythm to 5 (50ms), length to 16 rows, and
 * copies the packed color data from flash to RAM.
 *
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_load_default_data(void)
{
    memcpy(sequence_colors, sequence_default_rgb_rainbow, SEQUENCE_COLOR_DATA_SIZE);
    sequence_rhythm = 5;  /* Default: 5 (50ms) */
    sequence_length = 16;  /* Default: 16 rows */
    sequence_pointer = 0;  /* Reset pointer to start */
    return ESP_OK;
}

static esp_err_t sequence_init(void)
{
    /* Check if sequence data already exists */
    /* Data does NOT exist if:
     * - sequence_length == 0 OR
     * - sequence_rhythm == 0 OR
     * - sequence_colors is all zeros
     */
    bool data_exists = true;

    if (sequence_length == 0 || sequence_rhythm == 0) {
        data_exists = false;
    } else {
        /* Check if colors buffer is all zeros */
        bool all_zeros = true;
        for (int i = 0; i < SEQUENCE_COLOR_DATA_SIZE; i++) {
            if (sequence_colors[i] != 0) {
                all_zeros = false;
                break;
            }
        }
        if (all_zeros) {
            data_exists = false;
        }
    }

    if (!data_exists) {
        /* No data exists, load default RGB-rainbow sequence */
        esp_err_t err = sequence_load_default_data();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to load default sequence data: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Loading default RGB-rainbow sequence data");
    }

    return ESP_OK;
}

static esp_err_t sequence_deinit(void)
{
    sequence_timer_stop();
    return ESP_OK;
}

static esp_err_t sequence_on_activate(void)
{
    /* Sequence plugin activation: no special initialization needed */
    /* Sequence data and state are preserved from previous activation */
    ESP_LOGD(TAG, "Sequence plugin activated");
    return ESP_OK;
}

static esp_err_t sequence_on_deactivate(void)
{
    /* Stop sequence timer (also sets sequence_active = false) */
    sequence_timer_stop();

    /* Note: sequence_pointer, sequence_rhythm, sequence_colors, sequence_length are preserved */

    ESP_LOGD(TAG, "Sequence plugin deactivated");
    return ESP_OK;
}

/**
 * @brief Sequence plugin START command callback
 *
 * Handles PLUGIN_CMD_START command for the sequence plugin.
 * Starts sequence playback by creating and starting the sequence timer.
 *
 * Root Node Behavior:
 * - If timer is already running (duplicate START), returns ESP_OK gracefully
 * - Calls sequence_plugin_root_start() to start playback
 * - Root nodes typically start via plugin_activate(), not via mesh commands
 *
 * Child Node Behavior:
 * - Validates sequence data exists (rhythm and length must be non-zero)
 * - Stops existing timer before starting (cleanup)
 * - Preserves sequence pointer if resuming from pause (if within bounds)
 * - Resets pointer to 0 if out of bounds
 * - Starts timer with existing rhythm value
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if sequence data is missing (rhythm=0 or length=0)
 * @return Error code from sequence_timer_start() if timer start fails
 */
static esp_err_t sequence_on_start(void)
{
    if (esp_mesh_is_root()) {
        /* Root node: START commands received via mesh should be ignored
         * Root node starts via direct call during plugin activation, not via mesh commands
         * If timer is already running, this is likely a duplicate START from our own broadcast
         */
        if (sequence_active && sequence_timer != NULL) {
            return ESP_OK;
        }
        return sequence_plugin_root_start();
    } else {
        /* Child node: start timer if sequence data exists */
        if (sequence_rhythm == 0 || sequence_length == 0) {
            ESP_LOGE(TAG, "No sequence data available for START (rhythm=%d, length=%d)", sequence_rhythm, sequence_length);
            return ESP_ERR_INVALID_STATE;
        }

        sequence_timer_stop();

        /* Preserve pointer if resuming from pause (valid sequence data exists) */
        /* Validate pointer range and reset if out of bounds */
        uint16_t max_squares = sequence_length * 16;
        if (sequence_pointer >= max_squares) {
            sequence_pointer = 0;  /* Pointer out of range, reset to 0 */
        }
        /* Otherwise, preserve current pointer value (resume from pause) */

        esp_err_t err = sequence_timer_start(sequence_rhythm);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start sequence timer: 0x%x", err);
            return err;
        }

        ESP_LOGI(TAG, "Sequence playback started (child node)");
        return ESP_OK;
    }
}

/**
 * @brief Sequence plugin PAUSE command callback
 *
 * Handles PLUGIN_CMD_PAUSE command for the sequence plugin.
 * Pauses sequence playback by stopping the sequence timer.
 * The sequence pointer is preserved to allow resume functionality.
 *
 * Root Node Behavior:
 * - Calls sequence_plugin_root_pause() to stop playback
 *
 * Child Node Behavior:
 * - Stops the sequence timer
 * - Preserves sequence pointer (not modified) for resume
 * - Safe to call even if timer is not running (idempotent)
 *
 * @return ESP_OK on success (always succeeds)
 */
static esp_err_t sequence_on_pause(void)
{
    if (esp_mesh_is_root()) {
        return sequence_plugin_root_pause();
    } else {
        /* Child node: stop timer */
        sequence_timer_stop();
        ESP_LOGI(TAG, "Sequence playback paused (child node)");
        return ESP_OK;
    }
}

/**
 * @brief Sequence plugin RESET command callback
 *
 * Handles PLUGIN_CMD_RESET command for the sequence plugin.
 * Resets the sequence pointer to 0 and restarts playback if the sequence was active.
 *
 * Root Node Behavior:
 * - Calls sequence_plugin_root_reset() to reset playback
 *
 * Child Node Behavior:
 * - Always resets sequence pointer to 0
 * - If sequence was active, stops and restarts timer from beginning
 * - Validates sequence data before restarting timer
 * - If sequence was not active, only resets pointer (timer not restarted)
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if sequence data is invalid when restarting timer (rhythm=0 or length=0)
 * @return Error code from sequence_timer_start() if timer restart fails
 */
static esp_err_t sequence_on_reset(void)
{
    if (esp_mesh_is_root()) {
        return sequence_plugin_root_reset();
    } else {
        /* Child node: reset pointer and restart timer if active */
        sequence_pointer = 0;

        if (sequence_active) {
            if (sequence_rhythm == 0 || sequence_length == 0) {
                ESP_LOGE(TAG, "Cannot restart timer: invalid sequence data (rhythm=%d, length=%d)", sequence_rhythm, sequence_length);
                sequence_timer_stop();  /* Stop timer but don't restart */
                return ESP_ERR_INVALID_STATE;
            }
            sequence_timer_stop();
            esp_err_t err = sequence_timer_start(sequence_rhythm);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restart sequence timer: 0x%x", err);
                return err;
            }
        }

        ESP_LOGI(TAG, "Sequence pointer reset to 0 (child node)");
        return ESP_OK;
    }
}

static esp_err_t sequence_on_stop(void)
{
    /* Stop sequence timer (also sets sequence_active = false) */
    sequence_timer_stop();

    /* Reset sequence pointer to 0 */
    sequence_pointer = 0;

    ESP_LOGI(TAG, "Sequence plugin stopped - pointer reset to 0");
    return ESP_OK;
}


/*******************************************************
 *                Query Interface Callbacks
 *******************************************************/

static esp_err_t sequence_get_state(uint32_t query_type, void *result)
{
    if (result == NULL) {
        ESP_LOGE(TAG, "sequence_get_state failed: result is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    switch (query_type) {
        case SEQUENCE_QUERY_IS_ACTIVE:
            *((bool *)result) = sequence_active;
            return ESP_OK;

        case SEQUENCE_QUERY_GET_POINTER:
            *((uint16_t *)result) = sequence_pointer;
            return ESP_OK;

        case SEQUENCE_QUERY_GET_RHYTHM:
            *((uint8_t *)result) = sequence_rhythm;
            return ESP_OK;

        case SEQUENCE_QUERY_GET_LENGTH:
            *((uint8_t *)result) = sequence_length;
            return ESP_OK;

        default:
            ESP_LOGE(TAG, "sequence_get_state failed: invalid query_type 0x%08X", query_type);
            return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t sequence_execute_operation(uint32_t operation_type, void *params)
{
    switch (operation_type) {
        case SEQUENCE_OP_STORE: {
            if (params == NULL) {
                ESP_LOGE(TAG, "sequence_execute_operation STORE failed: params is NULL");
                return ESP_ERR_INVALID_ARG;
            }
            /* params points to: struct { uint8_t rhythm; uint8_t num_rows; uint8_t *color_data; uint16_t color_data_len; } */
            typedef struct {
                uint8_t rhythm;
                uint8_t num_rows;
                uint8_t *color_data;
                uint16_t color_data_len;
            } store_params_t;
            store_params_t *store_params = (store_params_t *)params;
            return sequence_plugin_root_store_and_broadcast(store_params->rhythm, store_params->num_rows,
                                                           store_params->color_data, store_params->color_data_len);
        }

        case SEQUENCE_OP_START:
            return sequence_plugin_root_start();

        case SEQUENCE_OP_PAUSE:
            return sequence_plugin_root_pause();

        case SEQUENCE_OP_RESET:
            return sequence_plugin_root_reset();


        default:
            ESP_LOGE(TAG, "sequence_execute_operation failed: invalid operation_type 0x%08X", operation_type);
            return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t sequence_get_helper(uint32_t helper_type, void *params, void *result)
{
    if (result == NULL) {
        ESP_LOGE(TAG, "sequence_get_helper failed: result is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    switch (helper_type) {
        case SEQUENCE_HELPER_PAYLOAD_SIZE: {
            if (params == NULL) {
                ESP_LOGE(TAG, "sequence_get_helper PAYLOAD_SIZE failed: params is NULL");
                return ESP_ERR_INVALID_ARG;
            }
            uint8_t num_rows = *((uint8_t *)params);
            *((uint16_t *)result) = 2 + ((num_rows * 16 / 2) * 3);  /* rhythm(1) + length(1) + color_data */
            return ESP_OK;
        }

        case SEQUENCE_HELPER_MESH_CMD_SIZE: {
            if (params == NULL) {
                ESP_LOGE(TAG, "sequence_get_helper MESH_CMD_SIZE failed: params is NULL");
                return ESP_ERR_INVALID_ARG;
            }
            uint8_t num_rows = *((uint8_t *)params);
            *((uint16_t *)result) = 3 + ((num_rows * 16 / 2) * 3);  /* cmd(1) + rhythm(1) + length(1) + color_data */
            return ESP_OK;
        }

        case SEQUENCE_HELPER_COLOR_DATA_SIZE: {
            if (params == NULL) {
                ESP_LOGE(TAG, "sequence_get_helper COLOR_DATA_SIZE failed: params is NULL");
                return ESP_ERR_INVALID_ARG;
            }
            uint8_t num_rows = *((uint8_t *)params);
            *((uint16_t *)result) = (num_rows * 16 / 2) * 3;  /* packed color data size */
            return ESP_OK;
        }

        default:
            ESP_LOGE(TAG, "sequence_get_helper failed: invalid helper_type 0x%08X", helper_type);
            return ESP_ERR_INVALID_ARG;
    }
}

/*******************************************************
 *                Plugin Registration
 *******************************************************/

void sequence_plugin_register(void)
{
    plugin_info_t info = {
        .name = "sequence",
        .is_default = false,  /* sequence is not the default plugin */
        .command_id = 0, /* Will be assigned by plugin system */
        .callbacks = {
            .command_handler = sequence_command_handler,
            .timer_callback = sequence_timer_cb,
            .heartbeat_handler = sequence_plugin_handle_heartbeat,
            .init = sequence_init,
            .deinit = sequence_deinit,
            .is_active = sequence_is_active,
            .on_activate = sequence_on_activate,
            .on_deactivate = sequence_on_deactivate,
            .on_start = sequence_on_start,
            .on_pause = sequence_on_pause,
            .on_reset = sequence_on_reset,
            .on_stop = sequence_on_stop,
            .get_state = sequence_get_state,
            .execute_operation = sequence_execute_operation,
            .get_helper = sequence_get_helper,
        },
        .user_data = NULL,
    };

    uint8_t assigned_cmd_id;
    esp_err_t err = plugin_register(&info, &assigned_cmd_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register sequence plugin: %s", esp_err_to_name(err));
    } else {
        sequence_plugin_id = assigned_cmd_id;
        ESP_LOGI(TAG, "Sequence plugin registered with plugin ID 0x%02X", sequence_plugin_id);
    }
}

/*******************************************************
 *                Root Node Functions (for API handlers)
 *******************************************************/

esp_err_t sequence_plugin_root_store_and_broadcast(uint8_t rhythm, uint8_t num_rows, uint8_t *color_data, uint16_t color_data_len)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot store and broadcast sequence");
        return ESP_ERR_INVALID_STATE;
    }

    if (rhythm == 0) {
        ESP_LOGE(TAG, "Invalid rhythm value: %d (must be 1-255)", rhythm);
        return ESP_ERR_INVALID_ARG;
    }

    if (num_rows < 1 || num_rows > 16) {
        ESP_LOGE(TAG, "Invalid sequence length: %d (must be 1-16 rows)", num_rows);
        return ESP_ERR_INVALID_ARG;
    }

    if (color_data == NULL) {
        ESP_LOGE(TAG, "Color data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate color_data_len doesn't exceed buffer size */
    if (color_data_len > SEQUENCE_COLOR_DATA_SIZE) {
        ESP_LOGE(TAG, "Color data length %d exceeds maximum %d", color_data_len, SEQUENCE_COLOR_DATA_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    sequence_rhythm = rhythm;
    sequence_length = num_rows;
    memset(sequence_colors, 0, SEQUENCE_COLOR_DATA_SIZE);
    memcpy(sequence_colors, color_data, color_data_len);
    ESP_LOGI(TAG, "Sequence data stored - rhythm: %d (%.1f ms), length: %d rows", rhythm, (float)rhythm * 10.0f, num_rows);

    sequence_timer_stop();
    sequence_pointer = 0;

    esp_err_t timer_err = sequence_timer_start(rhythm);
    if (timer_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start root sequence timer: 0x%x", timer_err);
    } else {
        ESP_LOGI(TAG, "Root sequence playback started");
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;
    if (child_node_count == 0) {
        ESP_LOGD(TAG, "Sequence stored - no child nodes to broadcast");
        return ESP_OK;
    }

    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = sequence_plugin_id;  /* Plugin ID */
    tx_buf[1] = PLUGIN_CMD_DATA;  /* Command byte */

    /* Calculate data length: rhythm(1) + num_rows(1) + color_data(variable) */
    uint16_t data_len = 2 + color_data_len;
    tx_buf[2] = (data_len >> 8) & 0xFF;  /* Length MSB (network byte order) */
    tx_buf[3] = data_len & 0xFF;  /* Length LSB */

    tx_buf[4] = rhythm;
    tx_buf[5] = num_rows;
    memcpy(&tx_buf[6], color_data, color_data_len);

    mesh_data_t data;
    data.data = tx_buf;
    data.size = 6 + color_data_len;  /* Plugin ID(1) + CMD(1) + LENGTH(2) + rhythm(1) + num_rows(1) + color_data */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    int success_count = 0;
    int fail_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        esp_err_t err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err == ESP_OK) {
            success_count++;
        } else {
            fail_count++;
            ESP_LOGD(TAG, "Sequence send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    ESP_LOGI(TAG, "Sequence command broadcast - rhythm:%d, length:%d rows, sent to %d/%d child nodes (success:%d, failed:%d)",
             rhythm, num_rows, success_count, child_node_count, success_count, fail_count);

    return ESP_OK;
}

esp_err_t sequence_plugin_root_start(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot start sequence");
        return ESP_ERR_INVALID_STATE;
    }

    if (sequence_rhythm == 0 || sequence_length == 0) {
        ESP_LOGE(TAG, "No sequence data available (rhythm=%d, length=%d)", sequence_rhythm, sequence_length);
        return ESP_ERR_INVALID_STATE;
    }

    sequence_timer_stop();

    /* Preserve pointer if resuming from pause (valid sequence data exists) */
    /* Validate pointer range and reset if out of bounds */
    uint16_t max_squares = sequence_length * 16;
    if (sequence_pointer >= max_squares) {
        sequence_pointer = 0;  /* Pointer out of range, reset to 0 */
    }
    /* Otherwise, preserve current pointer value (resume from pause) */

    esp_err_t err = sequence_timer_start(sequence_rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sequence timer: 0x%x", err);
    }

    /* Note: START command broadcasting is now handled by plugin_system.c in plugin_activate() */
    /* No need to broadcast here - plugin system handles it automatically */

    ESP_LOGI(TAG, "Sequence playback started");
    return err;
}

esp_err_t sequence_plugin_root_pause(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot pause sequence");
        return ESP_ERR_INVALID_STATE;
    }

    sequence_timer_stop();

    /* Note: PAUSE command broadcasting is now handled by plugin_system.c in plugin_system_handle_plugin_command_from_api() */
    /* No need to broadcast here - plugin system handles it automatically */

    ESP_LOGI(TAG, "Sequence playback paused");
    return ESP_OK;
}

esp_err_t sequence_plugin_root_reset(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot reset sequence");
        return ESP_ERR_INVALID_STATE;
    }

    sequence_pointer = 0;

    if (sequence_active) {
        sequence_timer_stop();
        esp_err_t err = sequence_timer_start(sequence_rhythm);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart sequence timer: 0x%x", err);
        }
    }

    /* Note: RESET command broadcasting is now handled by plugin_system.c in plugin_system_handle_plugin_command_from_api() */
    /* No need to broadcast here - plugin system handles it automatically */

    ESP_LOGI(TAG, "Sequence pointer reset to 0");
    return ESP_OK;
}

uint16_t sequence_plugin_root_get_pointer(void)
{
    return sequence_pointer;
}

uint8_t sequence_plugin_get_pointer_for_heartbeat(void)
{
    /* Return current sequence pointer if plugin is active, 0 otherwise */
    if (plugin_is_active("sequence") && sequence_active) {
        return (uint8_t)(sequence_pointer & 0xFF);  /* Convert to 1-byte (0-255) */
    }
    return 0;
}

esp_err_t sequence_plugin_handle_heartbeat(uint8_t pointer, uint8_t counter)
{
    /* Heartbeat is only for child nodes, root nodes send it */
    if (esp_mesh_is_root()) {
        ESP_LOGW(TAG, "Root node received heartbeat handler call (should not happen)");
        return ESP_ERR_INVALID_STATE;
    }

    /* Only update pointer if sequence plugin is active */
    if (!plugin_is_active("sequence")) {
        ESP_LOGD(TAG, "Heartbeat received but sequence plugin not active, ignoring");
        return ESP_OK;
    }

    if (sequence_length == 0) {
        ESP_LOGE(TAG, "No sequence data available for heartbeat (length=0)");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t max_squares = sequence_length * 16;

    if (pointer >= max_squares) {
        ESP_LOGE(TAG, "Invalid heartbeat pointer: %d (max: %d)", pointer, max_squares - 1);
        return ESP_ERR_INVALID_ARG;
    }

    sequence_pointer = pointer;
    ESP_LOGD(TAG, "Heartbeat received - pointer updated to %d, counter: %d", sequence_pointer, counter);

    return ESP_OK;
}

bool sequence_plugin_root_is_active(void)
{
    return sequence_active;
}

/*******************************************************
 *                Child Node Functions (for mesh_child.c)
 *******************************************************/

void sequence_plugin_node_pause(void)
{
    sequence_timer_stop();
}
