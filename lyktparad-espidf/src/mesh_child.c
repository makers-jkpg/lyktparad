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
#include "mesh_udp_bridge.h"
#include "light_neopixel.h"
#include "light_common_cathode.h"
#include "plugins/effects/effects_plugin.h"
#include "config/mesh_config.h"
#include "mesh_ota.h"
#include "plugin_system.h"
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
                } else if ((data.size == 1 && (cmd == 0x05 || cmd == 0x06 || cmd == 0x07)) ||  /* MESH_CMD_PLUGIN_START, PAUSE, RESET */
                           (data.size == 2 && cmd == 0x08) ||  /* MESH_CMD_PLUGIN_BEAT */
                           (data.size >= 3 && cmd == 0x04)) {  /* MESH_CMD_PLUGIN_DATA */
                    /* Route plugin control commands through plugin system */
                    if (cmd == 0x04) {
                        /* MESH_CMD_PLUGIN_DATA - route to active plugin's command_handler */
                        const char *active_plugin = plugin_get_active();
                        if (active_plugin != NULL) {
                            const plugin_info_t *plugin = plugin_get_by_name(active_plugin);
                            if (plugin != NULL && plugin->callbacks.command_handler != NULL) {
                                err = plugin->callbacks.command_handler(0x04, data.data, data.size);
                                if (err == ESP_OK) {
                                    ESP_LOGD(mesh_common_get_tag(), "[PLUGIN DATA] Command routed to plugin '%s'", active_plugin);
                                    continue;
                                }
                            }
                        }
                        ESP_LOGD(mesh_common_get_tag(), "[PLUGIN DATA] No active plugin for command 0x04");
                        continue;
                    } else {
                        /* MESH_CMD_PLUGIN_START, PAUSE, RESET, BEAT */
                        err = plugin_system_handle_plugin_command(cmd, data.data, data.size);
                        if (err == ESP_OK) {
                            ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] Command 0x%02X routed to plugin system", cmd);
                            continue;
                        } else if (err == ESP_ERR_NOT_FOUND) {
                            ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] No active plugin for command 0x%02X", cmd);
                            continue;
                        } else {
                            ESP_LOGE(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin command routing error 0x%02X: 0x%x", cmd, err);
                            continue;
                        }
                    }
                } else if (cmd >= 0x10 && cmd <= 0xEF) {
                    /* Route plugin commands (0x10-0xEF) to plugin system */
                    err = plugin_system_handle_command(cmd, data.data, data.size);
                    if (err == ESP_OK) {
                        /* Command handled by plugin, continue */
                        ESP_LOGD(mesh_common_get_tag(), "[PLUGIN] Command 0x%02X routed to plugin", cmd);
                        continue;
                    } else if (err == ESP_ERR_NOT_FOUND) {
                        /* Not a plugin command, fall through to continue (no core handlers for root) */
                        ESP_LOGD(mesh_common_get_tag(), "[PLUGIN] Command 0x%02X not registered, ignoring", cmd);
                        continue;
                    } else {
                        /* Plugin error, log and continue */
                        ESP_LOGE(mesh_common_get_tag(), "[PLUGIN] Command 0x%02X routing error: 0x%x", cmd, err);
                        continue;
                    }
                }
            }
            continue;
        }

        ESP_LOGI(mesh_common_get_tag(), "[RCVD NOT ROOT]");

        recv_count++;
        /* Route plugin commands (0x10-0xEF) to plugin system */
        if (data.proto == MESH_PROTO_BIN && data.size >= 1 && data.data[0] >= 0x10 && data.data[0] <= 0xEF) {
            err = plugin_system_handle_command(data.data[0], data.data, data.size);
            if (err == ESP_OK) {
                /* Command handled by plugin, continue */
                ESP_LOGD(mesh_common_get_tag(), "[PLUGIN] Command 0x%02X routed to plugin", data.data[0]);
                continue;
            } else if (err == ESP_ERR_NOT_FOUND) {
                /* Not a plugin command, fall through to core handlers */
                ESP_LOGD(mesh_common_get_tag(), "[PLUGIN] Command 0x%02X not registered, falling through to core handlers", data.data[0]);
            } else {
                /* Plugin error, log and continue */
                ESP_LOGE(mesh_common_get_tag(), "[PLUGIN] Command 0x%02X routing error: 0x%x", data.data[0], err);
                continue;
            }
        }
        /* detect heartbeat: command prefix (0x01) + 4-byte big-endian counter */
        if (data.proto == MESH_PROTO_BIN && data.size == 5 && data.data[0] == MESH_CMD_HEARTBEAT) {
            uint32_t hb = ((uint32_t)(uint8_t)data.data[1] << 24) |
                          ((uint32_t)(uint8_t)data.data[2] << 16) |
                          ((uint32_t)(uint8_t)data.data[3] << 8) |
                          ((uint32_t)(uint8_t)data.data[4] << 0);
            ESP_LOGI(MESH_TAG, "[NODE ACTION] Heartbeat received from "MACSTR", count:%" PRIu32, MAC2STR(from.addr), hb);
            /* Skip heartbeat LED changes if sequence mode is active (sequence controls LED) */
            bool sequence_active = false;
            esp_err_t query_err = plugin_query_state("sequence", 0x01, &sequence_active);  /* SEQUENCE_QUERY_IS_ACTIVE */
            if (query_err == ESP_OK && sequence_active) {
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
            /* Route MESH_CMD_EFFECT to effects plugin (backward compatibility) */
            const plugin_info_t *effects_plugin = plugin_get_by_name("effects");
            if (effects_plugin != NULL && effects_plugin->callbacks.command_handler != NULL) {
                esp_err_t plugin_err = effects_plugin->callbacks.command_handler(MESH_CMD_EFFECT, data.data, data.size);
                if (plugin_err != ESP_OK) {
                    ESP_LOGE(MESH_TAG, "[NODE ACTION] Effects plugin command handler returned error: %s", esp_err_to_name(plugin_err));
                }
            } else {
                ESP_LOGE(MESH_TAG, "[NODE ACTION] Effects plugin not found or has no command handler");
            }

        } else if (data.proto == MESH_PROTO_BIN && data.size == 4 && data.data[0] == MESH_CMD_SET_RGB) {
            /* detect RGB command: command prefix (0x03) + 3-byte RGB values */
            uint8_t r = data.data[1];
            uint8_t g = data.data[2];
            uint8_t b = data.data[3];
            ESP_LOGI(MESH_TAG, "[NODE ACTION] RGB command received from "MACSTR", R:%d G:%d B:%d", MAC2STR(from.addr), r, g, b);
            /* Pause active plugin if sequence plugin is active */
            if (plugin_is_active("sequence")) {
                plugin_execute_operation("sequence", 0x03, NULL);  /* SEQUENCE_OP_PAUSE */
            }
            /* Store RGB values for use in heartbeat handler */
            last_rgb_r = r;
            last_rgb_g = g;
            last_rgb_b = b;
            rgb_has_been_set = true;
            err = mesh_light_set_rgb(r, g, b);
            if (err != ESP_OK) {
                ESP_LOGE(mesh_common_get_tag(), "[RGB] failed to set LED: 0x%x", err);
            }
        } else if (data.proto == MESH_PROTO_BIN && data.size >= 3 && data.data[0] == 0x04) {  /* MESH_CMD_PLUGIN_DATA */
            /* Route MESH_CMD_PLUGIN_DATA to active plugin's command_handler */
            const char *active_plugin = plugin_get_active();
            if (active_plugin != NULL) {
                const plugin_info_t *plugin = plugin_get_by_name(active_plugin);
                if (plugin != NULL && plugin->callbacks.command_handler != NULL) {
                    err = plugin->callbacks.command_handler(0x04, data.data, data.size);
                    if (err != ESP_OK) {
                        ESP_LOGE(mesh_common_get_tag(), "[PLUGIN DATA] plugin '%s' command handler returned error: 0x%x", active_plugin, err);
                    }
                } else {
                    ESP_LOGE(mesh_common_get_tag(), "[PLUGIN DATA] active plugin '%s' not found or has no command handler", active_plugin);
                    err = ESP_ERR_NOT_FOUND;
                }
            } else {
                ESP_LOGD(mesh_common_get_tag(), "[PLUGIN DATA] No active plugin for command 0x04");
                err = ESP_ERR_NOT_FOUND;
            }
        } else if (data.proto == MESH_PROTO_BIN &&
                   ((data.size == 1 && (data.data[0] == 0x05 ||  /* MESH_CMD_PLUGIN_START */
                                        data.data[0] == 0x06 ||  /* MESH_CMD_PLUGIN_PAUSE */
                                        data.data[0] == 0x07)) ||  /* MESH_CMD_PLUGIN_RESET */
                    (data.size == 2 && data.data[0] == 0x08))) {  /* MESH_CMD_PLUGIN_BEAT */
            /* Route plugin control commands through plugin system */
            err = plugin_system_handle_plugin_command(data.data[0], data.data, data.size);
            if (err == ESP_OK) {
                ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] Command 0x%02X routed to plugin system", data.data[0]);
            } else if (err == ESP_ERR_NOT_FOUND) {
                ESP_LOGD(mesh_common_get_tag(), "[PLUGIN CONTROL] No active plugin for command 0x%02X", data.data[0]);
            } else {
                ESP_LOGE(mesh_common_get_tag(), "[PLUGIN CONTROL] Plugin command routing error 0x%02x: 0x%x", data.data[0], err);
            }
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
