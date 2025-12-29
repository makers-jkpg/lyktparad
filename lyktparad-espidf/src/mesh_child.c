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
#include "esp_log.h"
#include "esp_mesh.h"
#include "mesh_common.h"
#include "mesh_child.h"
#include "light_neopixel.h"
#include "light_common_cathode.h"
#include "mesh_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
            ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat received from "MACSTR", count:%" PRIu32, MAC2STR(from.addr), hb);
            if (!(hb%2)) {
                /* even heartbeat: turn off light */
                mesh_light_set_colour(0);
                set_rgb_led(0, 0, 0);
                ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (even) - LED OFF", (unsigned long)hb);
            } else {
                /* odd heartbeat: turn on light using last RGB color or default to MESH_LIGHT_BLUE */
                if (rgb_has_been_set) {
                    /* Use the color from the latest MESH_CMD_SET_RGB command */
                    mesh_light_set_rgb(last_rgb_r, last_rgb_g, last_rgb_b);
                    set_rgb_led(last_rgb_r, last_rgb_g, last_rgb_b);
                    ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (odd) - LED RGB(%d,%d,%d)",
                             (unsigned long)hb, last_rgb_r, last_rgb_g, last_rgb_b);
                } else {
                    /* Default to MESH_LIGHT_BLUE if no RGB command has been received */
                    mesh_light_set_colour(MESH_LIGHT_BLUE);
                    set_rgb_led(0, 0, 155);  /* Match MESH_LIGHT_BLUE RGB values */
                    ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] Heartbeat #%lu (odd) - LED BLUE (default)", (unsigned long)hb);
                }
            }
        } else if (data.proto == MESH_PROTO_BIN && data.size == 4 && data.data[0] == MESH_CMD_SET_RGB) {
            /* detect RGB command: command prefix (0x03) + 3-byte RGB values */
            uint8_t r = data.data[1];
            uint8_t g = data.data[2];
            uint8_t b = data.data[3];
            ESP_LOGI(mesh_common_get_tag(), "[NODE ACTION] RGB command received from "MACSTR", R:%d G:%d B:%d", MAC2STR(from.addr), r, g, b);
            /* Store RGB values for use in heartbeat handler */
            last_rgb_r = r;
            last_rgb_g = g;
            last_rgb_b = b;
            rgb_has_been_set = true;
            err = mesh_light_set_rgb(r, g, b);
            if (err != ESP_OK) {
                ESP_LOGE(mesh_common_get_tag(), "[RGB] failed to set LED: 0x%x", err);
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
