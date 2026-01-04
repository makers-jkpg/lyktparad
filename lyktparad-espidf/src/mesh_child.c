/* Mesh Child Node Module Implementation
 *
 * This module contains code specific to child/non-root nodes in the mesh network.
 * Child nodes receive heartbeats, process RGB commands, and update their LED state.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include <string.h>
#include <inttypes.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "mesh_common.h"
#include "mesh_child.h"
#include "mesh_commands.h"
#include "light_neopixel.h"
#include "node_sequence.h"
#include "node_effects.h"
#include "light_common_cathode.h"
#include "config/mesh_config.h"
#include "mesh_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Tag for logging - compile-time constant for MACSTR concatenation */
#define MESH_TAG "mesh_main"

/* Ensure MACSTR is defined - it should be in esp_mac.h */
#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

/* Child-specific static variables */
static uint8_t last_rgb_r = 0;
static uint8_t last_rgb_g = 0;
static uint8_t last_rgb_b = 0;
static bool rgb_has_been_set = false;

/*******************************************************
 *                Child P2P RX Task
 *******************************************************/

void esp_mesh_p2p_rx_main(void *arg)
{
    int recv_count = 0;
    esp_err_t err;
    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;
    uint8_t *rx_buf = mesh_common_get_rx_buf();
    data.data = rx_buf;
    data.size = RX_SIZE;
    mesh_common_set_running(true);

    while (mesh_common_is_running()) {
        data.size = RX_SIZE;
        err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err != ESP_OK || !data.size) {
            ESP_LOGE(mesh_common_get_tag(), "err:0x%x, size:%d", err, data.size);
            continue;
        }
        if (esp_mesh_is_root()) {
            /* Root node handles OTA messages */
            if (data.proto == MESH_PROTO_BIN && data.size >= 1) {
                uint8_t cmd = data.data[0];
                if (cmd == MESH_CMD_OTA_REQUEST || cmd == MESH_CMD_OTA_ACK || cmd == MESH_CMD_OTA_STATUS) {
                    mesh_ota_handle_mesh_message(&from, data.data, data.size);
                }
            }
            continue;
        }

        ESP_LOGI(mesh_common_get_tag(), "[RCVD NOT ROOT]");

        recv_count++;
        /* detect heartbeat: command prefix (0x01) + 4-byte big-endian counter */
        if (data.proto == MESH_PROTO_BIN && data.size == 5 && data.data[0] == MESH_CMD_HEARTBEAT) {
            uint32_t hb = ((uint32_t)(uint8_t)data.data[1] << 24) |
                          ((uint32_t)(uint8_t)data.data[2] << 16) |
                          ((uint32_t)(uint8_t)data.data[3] << 8) |
                          ((uint32_t)(uint8_t)data.data[4] << 0);
            ESP_LOGI(MESH_TAG, "[NODE ACTION] Heartbeat received from "MACSTR", count:%" PRIu32, MAC2STR(from.addr), hb);
            /* Skip heartbeat LED changes if sequence mode is active (sequence controls LED) */
            if (mode_sequence_node_is_active()) {
                ESP_LOGD(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu - skipping LED change (sequence active)", (unsigned long)hb);
            } else if (!(hb%2)) {
                /* even heartbeat: turn off light */
                mesh_light_set_colour(0);
                ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (even) - LED OFF", (unsigned long)hb);
            } else {
                /* odd heartbeat: turn on light using last RGB color or default to MESH_LIGHT_BLUE */
                if (rgb_has_been_set) {
                    /* Use the color from the latest MESH_CMD_SET_RGB command */
                    mesh_light_set_rgb(last_rgb_r, last_rgb_g, last_rgb_b);
                    ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (odd) - LED RGB(%d,%d,%d)",
                             (unsigned long)hb, last_rgb_r, last_rgb_g, last_rgb_b);
                } else {
                    /* Default to MESH_LIGHT_BLUE if no RGB command has been received */
                    mesh_light_set_colour(MESH_LIGHT_BLUE);
                    ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (odd) - LED BLUE (default)", (unsigned long)hb);
                }
            }
            
        } else if (data.proto == MESH_PROTO_BIN && data.data[0] == MESH_CMD_EFFECT) {

            /* Prepare effect parameters structure */
            struct effect_params_t *effect_params = (struct effect_params_t *)data.data;

            uint8_t effect_id = data.data[1];
            ESP_LOGI(MESH_TAG, "[NODE ACTION] EFFECT command received from "MACSTR", effect_id:%d param1:%d param2:%d",
                     MAC2STR(from.addr), effect_params->effect_id);
            /* For simplicity, we only handle strobe effect here */
            if (effect_id == EFFECT_STROBE || effect_id == EFFECT_FADE) {
                play_effect(effect_params);
            } else {
                ESP_LOGE(MESH_TAG, "[NODE ACTION] Unsupported effect_id:%d", effect_id);
            }

        } else if (data.proto == MESH_PROTO_BIN && data.size == 4 && data.data[0] == MESH_CMD_SET_RGB) {
            /* detect RGB command: command prefix (0x03) + 3-byte RGB values */
            uint8_t r = data.data[1];
            uint8_t g = data.data[2];
            uint8_t b = data.data[3];
            ESP_LOGI(MESH_TAG, "[NODE ACTION] RGB command received from "MACSTR", R:%d G:%d B:%d", MAC2STR(from.addr), r, g, b);
            /* Stop sequence playback if active */
            mode_sequence_node_stop();
            /* Store RGB values for use in heartbeat handler */
            last_rgb_r = r;
            last_rgb_g = g;
            last_rgb_b = b;
            rgb_has_been_set = true;
            err = mesh_light_set_rgb(r, g, b);
            if (err != ESP_OK) {
                ESP_LOGE(mesh_common_get_tag(), "[RGB] failed to set LED: 0x%x", err);
            }
        } else if (data.proto == MESH_PROTO_BIN && data.size >= 3 && data.data[0] == MESH_CMD_SEQUENCE) {
            /* detect SEQUENCE command: variable length (cmd + rhythm + length + color data) */
            /* Extract length to validate size */

            uint8_t num_rows = data.data[2];
            if (num_rows >= 1 && num_rows <= 16) {
                uint16_t expected_size = sequence_mesh_cmd_size(num_rows);
                if (data.size == expected_size) {
                    err = mode_sequence_node_handle_command(MESH_CMD_SEQUENCE, data.data, data.size);
                } else {
                    ESP_LOGE(mesh_common_get_tag(), "[SEQUENCE] size mismatch: got %d, expected %d for %d rows", data.size, expected_size, num_rows);
                    err = ESP_ERR_INVALID_SIZE;
                }
            } else {
                ESP_LOGE(mesh_common_get_tag(), "[SEQUENCE] invalid length: %d (must be 1-16)", num_rows);
                err = ESP_ERR_INVALID_ARG;
            }
            if (err != ESP_OK) {
                ESP_LOGE(mesh_common_get_tag(), "[SEQUENCE] failed to handle command: 0x%x", err);
            }
        } else if (data.proto == MESH_PROTO_BIN &&
                   ((data.size == 1 && (data.data[0] == MESH_CMD_SEQUENCE_START ||
                                        data.data[0] == MESH_CMD_SEQUENCE_STOP ||
                                        data.data[0] == MESH_CMD_SEQUENCE_RESET)) ||
                    (data.size == 2 && data.data[0] == MESH_CMD_SEQUENCE_BEAT))) {
            /* Control command: START/STOP/RESET are single-byte, BEAT is 2-byte (command + 1-byte pointer) */
            err = mode_sequence_node_handle_control(data.data[0], data.data, data.size);
            if (err != ESP_OK) {
                ESP_LOGE(mesh_common_get_tag(), "[SEQUENCE CONTROL] failed to handle command 0x%02x: 0x%x", data.data[0], err);
            }
        } else if (data.proto == MESH_PROTO_BIN && data.size >= 1) {
            /* Check for OTA messages (leaf nodes only) */
            uint8_t cmd = data.data[0];
            if (cmd == MESH_CMD_OTA_START || cmd == MESH_CMD_OTA_BLOCK ||
                cmd == MESH_CMD_OTA_PREPARE_REBOOT || cmd == MESH_CMD_OTA_REBOOT) {
                mesh_ota_handle_leaf_message(&from, data.data, data.size);
            }
        } else {
            /* process other light control messages */
            //mesh_light_process(&from, data.data, data.size);
        }
    }
    vTaskDelete(NULL);
}

/*******************************************************
 *                Child Initialization
 *******************************************************/

esp_err_t mesh_child_init(void)
{
    /* Child nodes don't need special initialization beyond common init */
    /* Event handler callbacks are optional for child nodes */
    return ESP_OK;
}
