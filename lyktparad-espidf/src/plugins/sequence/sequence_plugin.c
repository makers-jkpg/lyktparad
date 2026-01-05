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
#define SEQUENCE_OP_BROADCAST_BEAT 0x05

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
static esp_err_t sequence_broadcast_control(uint8_t cmd);
static esp_err_t sequence_handle_start(uint8_t *data, uint16_t len);
static esp_err_t sequence_handle_pause(uint8_t *data, uint16_t len);
static esp_err_t sequence_handle_reset(uint8_t *data, uint16_t len);
static esp_err_t sequence_handle_beat(uint8_t *data, uint16_t len);

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
        sequence_active = false;
    }
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

    uint8_t r_4bit, g_4bit, b_4bit;
    uint8_t r_scaled, g_scaled, b_scaled;

    extract_square_rgb(sequence_colors, sequence_pointer, &r_4bit, &g_4bit, &b_4bit);

    r_scaled = r_4bit * 16;
    g_scaled = g_4bit * 16;
    b_scaled = b_4bit * 16;

    esp_err_t err = plugin_light_set_rgb(r_scaled, g_scaled, b_scaled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LED in timer callback: 0x%x", err);
    }

    uint16_t max_squares = sequence_length * 16;
    sequence_pointer = (sequence_pointer + 1) % max_squares;

    /* Root node: broadcast BEAT at row boundaries */
    if (esp_mesh_is_root() && (sequence_pointer % 16 == 0)) {
        sequence_plugin_root_broadcast_beat();
    }
}

/*******************************************************
 *                Plugin Command Handler
 *******************************************************/

/**
 * @brief Sequence plugin command handler
 *
 * Handles plugin data commands:
 * - MESH_CMD_PLUGIN_DATA (0x04): Store and start sequence data
 *
 * Note: MESH_CMD_PLUGIN_START, MESH_CMD_PLUGIN_PAUSE, MESH_CMD_PLUGIN_RESET, and
 * MESH_CMD_PLUGIN_BEAT are handled via plugin callbacks (on_start, on_pause, on_reset, on_beat).
 *
 * @param cmd Command ID (should be MESH_CMD_PLUGIN_DATA = 0x04)
 * @param data Command data (includes command byte at data[0])
 * @param len Data length
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_command_handler(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* Validate data */
    if (data == NULL || len < 1) {
        ESP_LOGE(TAG, "Invalid command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if sequence plugin is active */
    if (!plugin_is_active("sequence")) {
        ESP_LOGD(TAG, "Command received but sequence plugin is not active");
        return ESP_ERR_INVALID_STATE;
    }

    /* Only handle MESH_CMD_PLUGIN_DATA (0x04) */
    if (cmd == 0x04) {  /* MESH_CMD_PLUGIN_DATA */
        return sequence_handle_command_internal(cmd, data, len);
    }

    ESP_LOGE(TAG, "Invalid command ID: 0x%02X (expected MESH_CMD_PLUGIN_DATA = 0x04)", cmd);
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t sequence_handle_command_internal(uint8_t cmd, uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 3) {
        ESP_LOGE(TAG, "Invalid command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    if (data[0] != 0x04) {  /* MESH_CMD_PLUGIN_DATA */
        ESP_LOGE(TAG, "Command byte mismatch: 0x%02X (expected MESH_CMD_PLUGIN_DATA = 0x04)", data[0]);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t rhythm = data[1];
    if (rhythm == 0) {
        ESP_LOGE(TAG, "Invalid rhythm value: %d (must be 1-255)", rhythm);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t num_rows = data[2];
    if (num_rows < 1 || num_rows > 16) {
        ESP_LOGE(TAG, "Invalid sequence length: %d (must be 1-16 rows)", num_rows);
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate expected mesh command size: cmd(1) + rhythm(1) + length(1) + color_data(variable) */
    uint16_t expected_size = 3 + ((num_rows * 16 / 2) * 3);
    if (len != expected_size) {
        ESP_LOGE(TAG, "Invalid sequence command size: %d (expected %d for %d rows)", len, expected_size, num_rows);
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t color_data_len = len - 3;

    sequence_timer_stop();

    sequence_rhythm = rhythm;
    sequence_length = num_rows;
    memset(sequence_colors, 0, SEQUENCE_COLOR_DATA_SIZE);
    memcpy(sequence_colors, &data[3], color_data_len);
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

/*******************************************************
 *                Control Command Handlers
 *******************************************************/

/**
 * @brief Handle START command
 *
 * Starts sequence playback. On root node, calls sequence_plugin_root_start().
 * On child node, starts timer if sequence data exists.
 *
 * @param data Command data (data[0] = MESH_CMD_PLUGIN_START = 0x05)
 * @param len Data length (must be 1)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_handle_start(uint8_t *data, uint16_t len)
{
    if (data == NULL || len != 1 || data[0] != 0x05) {  /* MESH_CMD_PLUGIN_START */
        ESP_LOGE(TAG, "Invalid START command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_mesh_is_root()) {
        /* Root node: use root function */
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
 * @brief Handle PAUSE command
 *
 * Pauses sequence playback (preserves state). On root node, calls sequence_plugin_root_pause().
 * On child node, stops timer.
 *
 * @param data Command data (data[0] = MESH_CMD_PLUGIN_PAUSE = 0x06)
 * @param len Data length (must be 1)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_handle_pause(uint8_t *data, uint16_t len)
{
    if (data == NULL || len != 1 || data[0] != 0x06) {  /* MESH_CMD_PLUGIN_PAUSE */
        ESP_LOGE(TAG, "Invalid PAUSE command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_mesh_is_root()) {
        /* Root node: use root function */
        return sequence_plugin_root_pause();
    } else {
        /* Child node: stop timer */
        sequence_timer_stop();
        ESP_LOGI(TAG, "Sequence playback paused (child node)");
        return ESP_OK;
    }
}

/**
 * @brief Handle RESET command
 *
 * Resets sequence pointer to 0. On root node, calls sequence_plugin_root_reset().
 * On child node, resets pointer and restarts timer if active.
 *
 * @param data Command data (data[0] = MESH_CMD_PLUGIN_RESET = 0x07)
 * @param len Data length (must be 1)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_handle_reset(uint8_t *data, uint16_t len)
{
    if (data == NULL || len != 1 || data[0] != 0x07) {  /* MESH_CMD_PLUGIN_RESET */
        ESP_LOGE(TAG, "Invalid RESET command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_mesh_is_root()) {
        /* Root node: use root function */
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

/**
 * @brief Handle BEAT command
 *
 * Updates sequence pointer from BEAT data. Only valid for child nodes.
 * Root nodes don't receive BEAT commands (they broadcast them).
 *
 * @param data Command data (data[0] = MESH_CMD_PLUGIN_BEAT = 0x08, data[1] = pointer)
 * @param len Data length (must be 2)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t sequence_handle_beat(uint8_t *data, uint16_t len)
{
    if (data == NULL || len != 2 || data[0] != 0x08) {  /* MESH_CMD_PLUGIN_BEAT */
        ESP_LOGE(TAG, "Invalid BEAT command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_mesh_is_root()) {
        ESP_LOGW(TAG, "Root node received BEAT command (should not happen)");
        return ESP_ERR_INVALID_STATE;
    }

    /* Child node: update pointer from BEAT data */
    if (sequence_length == 0) {
        ESP_LOGE(TAG, "No sequence data available for BEAT (length=0)");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t new_pointer = data[1];
    uint16_t max_squares = sequence_length * 16;

    if (new_pointer >= max_squares) {
        ESP_LOGE(TAG, "Invalid BEAT pointer: %d (max: %d)", new_pointer, max_squares - 1);
        return ESP_ERR_INVALID_ARG;
    }

    sequence_pointer = new_pointer;
    ESP_LOGD(TAG, "BEAT received - pointer updated to %d", sequence_pointer);

    return ESP_OK;
}

static bool sequence_is_active(void)
{
    return sequence_active;
}

static esp_err_t sequence_init(void)
{
    /* Initialize sequence state */
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
    /* Stop sequence timer */
    sequence_timer_stop();

    /* Clear playback state (but preserve sequence data) */
    sequence_active = false;
    /* Note: sequence_pointer, sequence_rhythm, sequence_colors, sequence_length are preserved */

    ESP_LOGD(TAG, "Sequence plugin deactivated");
    return ESP_OK;
}

static esp_err_t sequence_on_start(void)
{
    if (esp_mesh_is_root()) {
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

static esp_err_t sequence_on_beat(uint8_t *data, uint16_t len)
{
    if (data == NULL || len != 2 || data[0] != 0x08) {  /* MESH_CMD_PLUGIN_BEAT */
        ESP_LOGE(TAG, "Invalid BEAT command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    /* BEAT is only for child nodes, root nodes broadcast it */
    if (esp_mesh_is_root()) {
        ESP_LOGW(TAG, "Root node received BEAT callback (should not happen)");
        return ESP_ERR_INVALID_STATE;
    }

    /* Child node: update pointer from BEAT data */
    uint8_t pointer = data[1];
    if (sequence_length == 0) {
        ESP_LOGE(TAG, "No sequence data available for BEAT (length=0)");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t max_squares = sequence_length * 16;

    if (pointer >= max_squares) {
        ESP_LOGE(TAG, "Invalid BEAT pointer: %d (max: %d)", pointer, max_squares - 1);
        return ESP_ERR_INVALID_ARG;
    }

    sequence_pointer = pointer;
    ESP_LOGD(TAG, "BEAT received - pointer updated to %d", sequence_pointer);

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

        case SEQUENCE_OP_BROADCAST_BEAT:
            return sequence_plugin_root_broadcast_beat();

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
        .command_id = 0, /* Will be assigned by plugin system */
        .callbacks = {
            .command_handler = sequence_command_handler,
            .timer_callback = sequence_timer_cb,
            .init = sequence_init,
            .deinit = sequence_deinit,
            .is_active = sequence_is_active,
            .on_activate = sequence_on_activate,
            .on_deactivate = sequence_on_deactivate,
            .on_start = sequence_on_start,
            .on_pause = sequence_on_pause,
            .on_reset = sequence_on_reset,
            .on_beat = sequence_on_beat,
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
        ESP_LOGI(TAG, "Sequence plugin registered with command ID 0x%02X", assigned_cmd_id);
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
    tx_buf[0] = 0x04;  /* MESH_CMD_PLUGIN_DATA */
    tx_buf[1] = rhythm;
    tx_buf[2] = num_rows;
    memcpy(&tx_buf[3], color_data, color_data_len);

    mesh_data_t data;
    data.data = tx_buf;
    data.size = 3 + color_data_len;
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

static esp_err_t sequence_broadcast_control(uint8_t cmd)
{
    if (cmd != 0x05 && cmd != 0x06 && cmd != 0x07) {  /* MESH_CMD_PLUGIN_START, MESH_CMD_PLUGIN_PAUSE, MESH_CMD_PLUGIN_RESET */
        ESP_LOGE(TAG, "Invalid control command: 0x%02x", cmd);
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot broadcast control command");
        return ESP_ERR_INVALID_STATE;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;
    if (child_node_count == 0) {
        ESP_LOGD(TAG, "Control command broadcast - no child nodes");
        return ESP_OK;
    }

    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = cmd;

    mesh_data_t data;
    data.data = tx_buf;
    data.size = 1;
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
            ESP_LOGD(TAG, "Control send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    const char *cmd_name = (cmd == 0x05) ? "START" :  /* MESH_CMD_PLUGIN_START */
                          (cmd == 0x06) ? "PAUSE" : "RESET";  /* MESH_CMD_PLUGIN_PAUSE, MESH_CMD_PLUGIN_RESET */
    ESP_LOGI(TAG, "%s command broadcast - sent to %d/%d child nodes (success:%d, failed:%d)",
             cmd_name, success_count, child_node_count, success_count, fail_count);

    return ESP_OK;
}

esp_err_t sequence_plugin_root_broadcast_beat(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot broadcast BEAT");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t max_squares = sequence_length * 16;
    if (sequence_pointer >= max_squares) {
        sequence_pointer = 0;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;
    if (child_node_count == 0) {
        ESP_LOGD(TAG, "BEAT broadcast - no child nodes");
        return ESP_OK;
    }

    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = 0x08;  /* MESH_CMD_PLUGIN_BEAT */
    tx_buf[1] = sequence_pointer & 0xFF;

    mesh_data_t data;
    data.data = tx_buf;
    data.size = 2;
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
            ESP_LOGD(TAG, "BEAT send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    ESP_LOGI(TAG, "BEAT command broadcast - pointer:%d, sent to %d/%d child nodes (success:%d, failed:%d)",
             sequence_pointer, success_count, child_node_count, success_count, fail_count);

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

    esp_err_t broadcast_err = sequence_broadcast_control(0x05);  /* MESH_CMD_PLUGIN_START */
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to broadcast START command: 0x%x", broadcast_err);
    }

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

    esp_err_t broadcast_err = sequence_broadcast_control(0x06);  /* MESH_CMD_PLUGIN_PAUSE */
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to broadcast PAUSE command: 0x%x", broadcast_err);
    }

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

    esp_err_t broadcast_err = sequence_broadcast_control(0x07);  /* MESH_CMD_PLUGIN_RESET */
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to broadcast RESET command: 0x%x", broadcast_err);
    }

    ESP_LOGI(TAG, "Sequence pointer reset to 0");
    return ESP_OK;
}

uint16_t sequence_plugin_root_get_pointer(void)
{
    return sequence_pointer;
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
