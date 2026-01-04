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
#include "mesh_commands.h"
#include "mesh_common.h"
#include "light_neopixel.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "sequence_plugin";

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
static esp_err_t sequence_timer_reset(void);
static void extract_square_rgb(uint8_t *packed_data, uint16_t square_index, uint8_t *r, uint8_t *g, uint8_t *b);
static esp_err_t sequence_handle_command_internal(uint8_t cmd, uint8_t *data, uint16_t len);
static esp_err_t sequence_broadcast_control(uint8_t cmd);

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
        return err;
    }

    sequence_active = true;
    return ESP_OK;
}

static esp_err_t sequence_timer_reset(void)
{
    if (!sequence_active) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sequence_rhythm == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_timer_stop(sequence_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to stop timer for reset: 0x%x", err);
    }

    err = esp_timer_delete(sequence_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete timer for reset: 0x%x", err);
    }
    sequence_timer = NULL;

    err = sequence_timer_start(sequence_rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restart timer: 0x%x", err);
        sequence_active = false;
        return err;
    }

    return ESP_OK;
}

/*******************************************************
 *                Timer Callback
 *******************************************************/

static void sequence_timer_cb(void *arg)
{
    uint8_t r_4bit, g_4bit, b_4bit;
    uint8_t r_scaled, g_scaled, b_scaled;

    extract_square_rgb(sequence_colors, sequence_pointer, &r_4bit, &g_4bit, &b_4bit);

    r_scaled = r_4bit * 16;
    g_scaled = g_4bit * 16;
    b_scaled = b_4bit * 16;

    esp_err_t err = mesh_light_set_rgb(r_scaled, g_scaled, b_scaled);
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

static esp_err_t sequence_command_handler(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* Validate command */
    if (cmd != MESH_CMD_SEQUENCE) {
        ESP_LOGE(TAG, "Invalid command ID: 0x%02X (expected MESH_CMD_SEQUENCE)", cmd);
        return ESP_ERR_INVALID_ARG;
    }

    return sequence_handle_command_internal(cmd, data, len);
}

static esp_err_t sequence_handle_command_internal(uint8_t cmd, uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 3) {
        ESP_LOGE(TAG, "Invalid command data: data=%p, len=%d", data, len);
        return ESP_ERR_INVALID_ARG;
    }

    if (data[0] != MESH_CMD_SEQUENCE) {
        ESP_LOGE(TAG, "Command byte mismatch: 0x%02X (expected MESH_CMD_SEQUENCE)", data[0]);
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

    uint16_t expected_size = sequence_mesh_cmd_size(num_rows);
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
    tx_buf[0] = MESH_CMD_SEQUENCE;
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
    if (cmd != MESH_CMD_SEQUENCE_START && cmd != MESH_CMD_SEQUENCE_STOP && cmd != MESH_CMD_SEQUENCE_RESET) {
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

    const char *cmd_name = (cmd == MESH_CMD_SEQUENCE_START) ? "START" :
                          (cmd == MESH_CMD_SEQUENCE_STOP) ? "STOP" : "RESET";
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
    tx_buf[0] = MESH_CMD_SEQUENCE_BEAT;
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

    if (sequence_rhythm == 0) {
        ESP_LOGE(TAG, "No sequence data available");
        return ESP_ERR_INVALID_STATE;
    }

    sequence_timer_stop();
    sequence_pointer = 0;

    esp_err_t err = sequence_timer_start(sequence_rhythm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sequence timer: 0x%x", err);
    }

    esp_err_t broadcast_err = sequence_broadcast_control(MESH_CMD_SEQUENCE_START);
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to broadcast START command: 0x%x", broadcast_err);
    }

    ESP_LOGI(TAG, "Sequence playback started");
    return err;
}

esp_err_t sequence_plugin_root_stop(void)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Not root node, cannot stop sequence");
        return ESP_ERR_INVALID_STATE;
    }

    sequence_timer_stop();

    esp_err_t broadcast_err = sequence_broadcast_control(MESH_CMD_SEQUENCE_STOP);
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to broadcast STOP command: 0x%x", broadcast_err);
    }

    ESP_LOGI(TAG, "Sequence playback stopped");
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

    esp_err_t broadcast_err = sequence_broadcast_control(MESH_CMD_SEQUENCE_RESET);
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

void sequence_plugin_node_stop(void)
{
    sequence_timer_stop();
}
