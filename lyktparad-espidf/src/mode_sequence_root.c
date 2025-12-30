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

/* Static storage for sequence data */
static uint8_t sequence_rhythm = 25;  /* Default: 25 (250ms) */
static uint8_t sequence_colors[SEQUENCE_COLOR_DATA_SIZE];  /* Packed color data (384 bytes) */

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
