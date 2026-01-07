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
#include "mesh_root.h"
#include "mesh_commands.h"
#include "plugin_system.h"
#include "mesh_udp_bridge.h"
#include "plugins/sequence/sequence_plugin.h"
#include "plugins/rgb_effect/rgb_effect_plugin.h"
#include "light_neopixel.h"
#include "light_common_cathode.h"
#include "config/mesh_config.h"
#include "mesh_ota.h"
#include "nvs_flash.h"
#include "nvs.h"
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
static bool state_query_responded = false;  /* One-time response flag for state queries */

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
            /* Root node handles OTA messages, RGB commands, and plugin commands for unified behavior */
            if (data.proto == MESH_PROTO_BIN && data.size >= 1) {
                uint8_t cmd = data.data[0];
                if (cmd == MESH_CMD_OTA_REQUEST || cmd == MESH_CMD_OTA_ACK || cmd == MESH_CMD_OTA_STATUS) {
                    mesh_ota_handle_mesh_message(&from, data.data, data.size);
                    continue;
                } else if (data.size == 4 && cmd == MESH_CMD_SET_RGB) {
                    /* Root node can receive RGB commands for unified behavior */
                    uint8_t r = data.data[1];
                    uint8_t g = data.data[2];
                    uint8_t b = data.data[3];
                    ESP_LOGI(MESH_TAG, "[ROOT ACTION] RGB command received from "MACSTR", R:%d G:%d B:%d", MAC2STR(from.addr), r, g, b);
                    mesh_root_handle_rgb_command(r, g, b);
                    continue;
                } else if (cmd == MESH_CMD_MESH_STATE_RESPONSE && data.size >= 3) {
                    /* Handle state response from child node (for root state adoption) */
                    uint8_t plugin_name_len = data.data[1];
                    if (plugin_name_len > 31) {
                        plugin_name_len = 31;  /* Limit to prevent overflow */
                    }
                    if (data.size < 2 + plugin_name_len + 1) {
                        ESP_LOGW(MESH_TAG, "[ROOT ACTION] Invalid state response size: %d", data.size);
                        continue;
                    }
                    char plugin_name[32] = {0};
                    if (plugin_name_len > 0) {
                        memcpy(plugin_name, &data.data[2], plugin_name_len);
                        plugin_name[plugin_name_len] = '\0';
                    }
                    uint8_t counter = data.data[2 + plugin_name_len];
                    mesh_root_handle_state_response(plugin_name[0] != '\0' ? plugin_name : NULL, counter);
                    continue;
                } else if (data.size >= 2 && cmd >= 0x0B && cmd <= 0xEE) {
                    /* Route plugin protocol commands: [PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N] */
                    uint8_t plugin_cmd = data.data[1];
                    if (plugin_cmd == PLUGIN_CMD_START || plugin_cmd == PLUGIN_CMD_PAUSE ||
                        plugin_cmd == PLUGIN_CMD_RESET) {
                        /* Plugin control commands (START, PAUSE, RESET) */
                        err = plugin_system_handle_plugin_command(data.data, data.size);
                        if (err == ESP_OK) {
                            ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin ID 0x%02X, command 0x%02X routed", cmd, plugin_cmd);
                            continue;
                        } else if (err == ESP_ERR_NOT_FOUND) {
                            ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin ID 0x%02X not registered", cmd);
                            continue;
                        } else {
                            ESP_LOGE(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin command routing error: 0x%x", err);
                            continue;
                        }
                    } else if (plugin_cmd == PLUGIN_CMD_DATA) {
                        /* Plugin data commands */
                        err = plugin_system_handle_command(data.data, data.size);
                        if (err == ESP_OK) {
                            ESP_LOGD(mesh_common_get_tag(), "[PLUGIN DATA] Plugin ID 0x%02X routed", cmd);
                            continue;
                        } else if (err == ESP_ERR_NOT_FOUND) {
                            ESP_LOGD(mesh_common_get_tag(), "[PLUGIN DATA] Plugin ID 0x%02X not registered", cmd);
                            continue;
                        } else {
                            ESP_LOGE(mesh_common_get_tag(), "[PLUGIN DATA] Plugin command routing error: 0x%x", err);
                            continue;
                        }
                    }
                }
            }
            continue;
        }

        recv_count++;
        /* Route plugin protocol commands: [PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N] */
        if (data.proto == MESH_PROTO_BIN && data.size >= 2 && data.data[0] >= 0x0B && data.data[0] <= 0xEE) {
            uint8_t plugin_id = data.data[0];
            uint8_t plugin_cmd = data.data[1];
            if (plugin_cmd == PLUGIN_CMD_START || plugin_cmd == PLUGIN_CMD_PAUSE ||
                plugin_cmd == PLUGIN_CMD_RESET) {
                /* Plugin control commands (START, PAUSE, RESET, BEAT) */
                err = plugin_system_handle_plugin_command(data.data, data.size);
                if (err == ESP_OK) {
                    ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin ID 0x%02X, command 0x%02X routed", plugin_id, plugin_cmd);
                    continue;
                } else if (err == ESP_ERR_NOT_FOUND) {
                    ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin ID 0x%02X not registered", plugin_id);
                } else {
                    ESP_LOGE(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin command routing error: 0x%x", err);
                    continue;
                }
            } else if (plugin_cmd == PLUGIN_CMD_DATA) {
                /* Plugin data commands */
                err = plugin_system_handle_command(data.data, data.size);
                if (err == ESP_OK) {
                    ESP_LOGD(mesh_common_get_tag(), "[PLUGIN DATA] Plugin ID 0x%02X routed", plugin_id);
                    continue;
                } else if (err == ESP_ERR_NOT_FOUND) {
                    ESP_LOGD(mesh_common_get_tag(), "[PLUGIN DATA] Plugin ID 0x%02X not registered", plugin_id);
                } else {
                    ESP_LOGE(mesh_common_get_tag(), "[PLUGIN DATA] Plugin command routing error: 0x%x", err);
                    continue;
                }
            }
        }
        /* Handle state query from root node */
        if (data.proto == MESH_PROTO_BIN && data.size == 1 && data.data[0] == MESH_CMD_QUERY_MESH_STATE) {
            /* Check if we've already responded to this query */
            if (!state_query_responded) {
                /* Get active plugin name */
                const char *active_plugin = plugin_get_active();
                uint8_t plugin_name_len = 0;
                if (active_plugin != NULL && active_plugin[0] != '\0') {
                    plugin_name_len = strlen(active_plugin);
                    if (plugin_name_len > 31) {
                        plugin_name_len = 31;  /* Limit to prevent overflow */
                    }
                }

                /* Get current local heartbeat counter */
                uint8_t counter = mesh_common_get_local_heartbeat_counter();

                /* Prepare response: [CMD:1] [PLUGIN_NAME_LEN:1] [PLUGIN_NAME:N] [COUNTER:1] */
                uint8_t *tx_buf = mesh_common_get_tx_buf();
                tx_buf[0] = MESH_CMD_MESH_STATE_RESPONSE;
                tx_buf[1] = plugin_name_len;
                if (plugin_name_len > 0) {
                    memcpy(&tx_buf[2], active_plugin, plugin_name_len);
                }
                tx_buf[2 + plugin_name_len] = counter;

                mesh_data_t response_data;
                response_data.data = tx_buf;
                response_data.size = 2 + plugin_name_len + 1;  /* CMD + LEN + NAME + COUNTER */
                response_data.proto = MESH_PROTO_BIN;
                response_data.tos = MESH_TOS_P2P;

                /* Send response to root node */
                esp_err_t send_err = mesh_send_with_bridge(&from, &response_data, MESH_DATA_P2P, NULL, 0);
                if (send_err == ESP_OK) {
                    state_query_responded = true;
                    ESP_LOGI(MESH_TAG, "[CHILD ACTION] State response sent: plugin='%s', counter=%u",
                             active_plugin != NULL ? active_plugin : "none", counter);
                } else {
                    ESP_LOGW(MESH_TAG, "[CHILD ACTION] Failed to send state response: %s", esp_err_to_name(send_err));
                }
            } else {
                ESP_LOGD(MESH_TAG, "[CHILD ACTION] State query ignored (already responded)");
            }
            continue;
        }
        /* detect heartbeat: command prefix (0x01) + pointer (1 byte) + counter (1 byte) */
        if (data.proto == MESH_PROTO_BIN && data.size == 3 && data.data[0] == MESH_CMD_HEARTBEAT) {
            uint8_t pointer = data.data[1];
            uint8_t counter = data.data[2];
            ESP_LOGI(MESH_TAG, "[NODE ACTION] Heartbeat received from "MACSTR", pointer:%u, counter:%u", MAC2STR(from.addr), pointer, counter);

            /* Route heartbeat to sequence plugin if active */
            if (plugin_is_active("sequence")) {
                esp_err_t heartbeat_err = sequence_plugin_handle_heartbeat(pointer, counter);
                if (heartbeat_err != ESP_OK && heartbeat_err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(mesh_common_get_tag(), "[HEARTBEAT] Sequence plugin heartbeat handler error: 0x%x", heartbeat_err);
                }
            }

            /* Route heartbeat to rgb_effect plugin if active */
            if (plugin_is_active("rgb_effect")) {
                esp_err_t heartbeat_err = rgb_effect_plugin_handle_heartbeat(pointer, counter);
                if (heartbeat_err != ESP_OK) {
                    ESP_LOGW(mesh_common_get_tag(), "[HEARTBEAT] RGB effect plugin heartbeat handler error: 0x%x", heartbeat_err);
                }
            }

            /* Synchronize core local heartbeat counter with received counter */
            mesh_common_set_local_heartbeat_counter(counter);

            /* Reset state query response flag (allows node to respond to next state query) */
            state_query_responded = false;

            /* Heartbeat counting and mesh command handling continue, but RGB LED control is removed
             * RGB LEDs are now exclusive to plugins via plugin_light_set_rgb() and plugin_set_rgb_led()
             */
            ESP_LOGD(mesh_common_get_tag(), "[NODE ACTION] Heartbeat - pointer:%u, counter:%u", pointer, counter);

        } else if (data.proto == MESH_PROTO_BIN && data.size == 4 && data.data[0] == MESH_CMD_SET_RGB) {
            /* detect RGB command: command prefix (0x03) + 3-byte RGB values */
            uint8_t r = data.data[1];
            uint8_t g = data.data[2];
            uint8_t b = data.data[3];
            ESP_LOGI(MESH_TAG, "[NODE ACTION] RGB command received from "MACSTR", R:%d G:%d B:%d", MAC2STR(from.addr), r, g, b);
            /* Store RGB values (for potential future use, but RGB LED control is removed)
             * RGB LEDs are now exclusive to plugins via plugin_light_set_rgb() and plugin_set_rgb_led()
             * RGB commands should be routed to plugin system instead
             */
            last_rgb_r = r;
            last_rgb_g = g;
            last_rgb_b = b;
            rgb_has_been_set = true;
        } else if (data.proto == MESH_PROTO_BIN && data.size >= 1) {
            uint8_t cmd = data.data[0];
            /* Check for web server IP broadcast command */
            if (cmd == MESH_CMD_WEBSERVER_IP_BROADCAST) {
                /* Minimum size: command (1 byte) + payload (6 bytes for IP + port) */
                if (data.size >= 7) {
                    const mesh_webserver_ip_broadcast_t *payload = (const mesh_webserver_ip_broadcast_t *)(data.data + 1);

                    /* Extract IP address from payload */
                    char server_ip[16] = {0};
                    snprintf(server_ip, sizeof(server_ip), "%d.%d.%d.%d",
                             payload->ip[0], payload->ip[1], payload->ip[2], payload->ip[3]);

                    /* Extract port from payload (convert from network byte order) */
                    uint16_t server_port = ntohs(payload->port);

                    /* Validate port range (port 0 is invalid) */
                    if (server_port == 0) {
                        ESP_LOGW(MESH_TAG, "[WEBSERVER IP] Invalid port: %d", server_port);
                    } else {
                        /* Extract timestamp if present (optional, 10 bytes total) */
                        uint32_t timestamp = 0;
                        if (data.size >= 11) {
                            timestamp = ntohl(payload->timestamp);
                        }

                        /* Store in NVS */
                        nvs_handle_t nvs_handle;
                        esp_err_t err = nvs_open("udp_bridge", NVS_READWRITE, &nvs_handle);
                        if (err == ESP_OK) {
                            /* Store server IP address */
                            err = nvs_set_str(nvs_handle, "server_ip", server_ip);
                            if (err == ESP_OK) {
                                /* Store server port */
                                err = nvs_set_u16(nvs_handle, "server_port", server_port);
                                if (err == ESP_OK && timestamp > 0) {
                                    /* Store timestamp if present */
                                    nvs_set_u32(nvs_handle, "server_ip_timestamp", timestamp);
                                }
                                /* Commit changes */
                                err = nvs_commit(nvs_handle);
                                if (err == ESP_OK) {
                                    ESP_LOGI(MESH_TAG, "[WEBSERVER IP] Cached external web server: %s:%d", server_ip, server_port);
                                } else {
                                    ESP_LOGW(MESH_TAG, "[WEBSERVER IP] Failed to commit NVS: %s", esp_err_to_name(err));
                                }
                            } else {
                                ESP_LOGW(MESH_TAG, "[WEBSERVER IP] Failed to store IP in NVS: %s", esp_err_to_name(err));
                            }
                            nvs_close(nvs_handle);
                        } else {
                            ESP_LOGW(MESH_TAG, "[WEBSERVER IP] Failed to open NVS: %s", esp_err_to_name(err));
                        }
                    }
                } else {
                    ESP_LOGW(MESH_TAG, "[WEBSERVER IP] Invalid payload size: %d (expected >= 7)", data.size);
                }
            }
            /* Check for web server discovery failure broadcast command */
            else if (cmd == MESH_CMD_WEBSERVER_DISCOVERY_FAILED) {
                /* Minimum size: command (1 byte) + payload (4 bytes for timestamp) */
                if (data.size >= 5) {
                    const mesh_webserver_discovery_failed_t *payload = (const mesh_webserver_discovery_failed_t *)(data.data + 1);

                    /* Extract timestamp from payload (already in network byte order) */
                    uint32_t timestamp = payload->timestamp;

                    /* Store discovery failure state locally */
                    esp_err_t err = mesh_common_set_discovery_failed(timestamp);
                    if (err == ESP_OK) {
                        ESP_LOGI(MESH_TAG, "[DISCOVERY FAILURE] Received discovery failure state from "MACSTR" (timestamp: %lu)",
                                 MAC2STR(from.addr), (unsigned long)ntohl(timestamp));
                    } else {
                        ESP_LOGW(MESH_TAG, "[DISCOVERY FAILURE] Failed to store failure state: %s", esp_err_to_name(err));
                    }
                } else {
                    ESP_LOGW(MESH_TAG, "[DISCOVERY FAILURE] Invalid payload size: %d (expected >= 5)", data.size);
                }
            }
            /* Check for OTA messages (leaf nodes only) */
            else if (cmd == MESH_CMD_OTA_START || cmd == MESH_CMD_OTA_BLOCK ||
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
