/* Plugin System Implementation
 *
 * This module implements the plugin system infrastructure for the mesh network.
 * Plugins can register themselves and receive automatic command ID assignment.
 * The system routes commands to registered plugins based on command ID.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "plugin_system.h"
#include "mesh_commands.h"
#include "mesh_common.h"
#include "mesh_root.h"
#include "esp_mesh.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <string.h>

#ifndef CONFIG_MESH_ROUTE_TABLE_SIZE
#define CONFIG_MESH_ROUTE_TABLE_SIZE 50
#endif

/* Ensure MACSTR and MAC2STR are defined */
#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

static const char *TAG = "plugin_system";

#define MAX_PLUGINS 16
#define PLUGIN_ID_MIN 0x0B
#define PLUGIN_ID_MAX 0xEE

/* Plugin registry */
static plugin_info_t plugin_registry[MAX_PLUGINS];
static uint8_t plugin_count = 0;
static uint8_t next_plugin_id = PLUGIN_ID_MIN;

/* Active plugin tracking - only one plugin can be active at a time (mutual exclusivity) */
static const char *active_plugin_name = NULL;

esp_err_t plugin_register(const plugin_info_t *info, uint8_t *assigned_cmd_id)
{
    /* Validate input parameters */
    if (info == NULL) {
        ESP_LOGE(TAG, "Plugin registration failed: info is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (info->name == NULL) {
        ESP_LOGE(TAG, "Plugin registration failed: name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(info->name) == 0) {
        ESP_LOGE(TAG, "Plugin registration failed: name is empty");
        return ESP_ERR_INVALID_ARG;
    }

    if (info->callbacks.command_handler == NULL) {
        ESP_LOGE(TAG, "Plugin registration failed: command_handler is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (assigned_cmd_id == NULL) {
        ESP_LOGE(TAG, "Plugin registration failed: assigned_cmd_id output parameter is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Check for duplicate name */
    for (uint8_t i = 0; i < plugin_count; i++) {
        if (strcmp(plugin_registry[i].name, info->name) == 0) {
            ESP_LOGE(TAG, "Plugin registration failed: plugin '%s' already registered", info->name);
            return ESP_ERR_INVALID_STATE;
        }
    }

    /* Check if registry is full */
    if (plugin_count >= MAX_PLUGINS) {
        ESP_LOGE(TAG, "Plugin registration failed: registry full (max %d plugins)", MAX_PLUGINS);
        return ESP_ERR_NO_MEM;
    }

    /* Check if plugin ID range is exhausted */
    if (next_plugin_id > PLUGIN_ID_MAX) {
        ESP_LOGE(TAG, "Plugin registration failed: plugin ID range exhausted (0x%02X-0x%02X)", PLUGIN_ID_MIN, PLUGIN_ID_MAX);
        return ESP_ERR_NO_MEM;
    }

    /* Assign plugin ID and prepare plugin copy */
    plugin_info_t plugin_copy = *info;
    plugin_copy.command_id = next_plugin_id;

    /* Call optional init callback before adding to registry */
    if (plugin_copy.callbacks.init != NULL) {
        esp_err_t init_err = plugin_copy.callbacks.init();
        if (init_err != ESP_OK) {
            ESP_LOGE(TAG, "Plugin '%s' init callback failed: %s", info->name, esp_err_to_name(init_err));
            return init_err;
        }
    }

    /* Add plugin to registry (after successful init) */
    plugin_registry[plugin_count] = plugin_copy;

    /* Set output parameter */
    *assigned_cmd_id = next_plugin_id;

    /* Increment counters */
    plugin_count++;
    next_plugin_id++;

    ESP_LOGI(TAG, "Plugin '%s' registered with command ID 0x%02X", info->name, plugin_copy.command_id);

    return ESP_OK;
}

const plugin_info_t *plugin_get_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    for (uint8_t i = 0; i < plugin_count; i++) {
        if (strcmp(plugin_registry[i].name, name) == 0) {
            return &plugin_registry[i];
        }
    }

    return NULL;
}

const plugin_info_t *plugin_get_by_id(uint8_t plugin_id)
{
    /* Validate plugin ID is in plugin range */
    if (plugin_id < PLUGIN_ID_MIN || plugin_id > PLUGIN_ID_MAX) {
        return NULL;
    }

    for (uint8_t i = 0; i < plugin_count; i++) {
        if (plugin_registry[i].command_id == plugin_id) {
            return &plugin_registry[i];
        }
    }

    return NULL;
}

esp_err_t plugin_get_id_by_name(const char *name, uint8_t *plugin_id)
{
    if (name == NULL || plugin_id == NULL) {
        ESP_LOGE(TAG, "plugin_get_id_by_name failed: name or plugin_id is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    const plugin_info_t *plugin = plugin_get_by_name(name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "plugin_get_id_by_name failed: plugin '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }

    *plugin_id = plugin->command_id;
    return ESP_OK;
}

esp_err_t plugin_system_handle_command(uint8_t *data, uint16_t len)
{
    /* Validate data pointer and minimum length */
    if (data == NULL) {
        ESP_LOGE(TAG, "Command routing failed: data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (len < 2) {
        ESP_LOGE(TAG, "Command routing failed: len < 2 (need plugin ID + command byte)");
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract plugin ID from first byte */
    uint8_t plugin_id = data[0];

    /* Validate plugin ID is in plugin range */
    if (plugin_id < PLUGIN_ID_MIN || plugin_id > PLUGIN_ID_MAX) {
        ESP_LOGE(TAG, "Command routing failed: plugin ID 0x%02X outside plugin range (0x%02X-0x%02X)", plugin_id, PLUGIN_ID_MIN, PLUGIN_ID_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract command byte from second byte */
    uint8_t cmd = data[1];

    /* Root nodes should ignore DATA commands received via mesh
     * Root nodes process DATA commands via direct API calls (e.g., sequence_plugin_root_store_and_broadcast),
     * not via mesh commands. This prevents root nodes from processing their own broadcasts.
     */
    if (esp_mesh_is_root() && cmd == PLUGIN_CMD_DATA) {
        return ESP_OK; /* Silently ignore - root node doesn't process DATA via mesh */
    }

    /* Look up plugin by plugin ID */
    const plugin_info_t *plugin = plugin_get_by_id(plugin_id);
    if (plugin == NULL) {
        ESP_LOGD(TAG, "Command routing: no plugin registered for plugin ID 0x%02X", plugin_id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Call plugin's command handler with remaining data (skip plugin ID byte) */
    if (plugin->callbacks.command_handler == NULL) {
        ESP_LOGE(TAG, "Command routing failed: plugin '%s' has NULL command_handler", plugin->name);
        return ESP_ERR_INVALID_STATE;
    }

    /* Pass remaining data (skip plugin ID byte) to command handler */
    /* Command handler receives: [CMD:1] [LENGTH:2?] [DATA:N] */
    esp_err_t err = plugin->callbacks.command_handler(&data[1], len - 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Plugin '%s' command handler returned error: %s", plugin->name, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Command routed to plugin '%s' (plugin ID 0x%02X)", plugin->name, plugin_id);
    }

    return err;
}

/**
 * @brief Broadcast plugin command to all child nodes
 *
 * This function broadcasts a plugin command to all child nodes in the mesh network.
 * Only root nodes can broadcast commands. Child nodes will ignore this function.
 *
 * @param plugin_id Plugin ID (0x0B-0xEE)
 * @param cmd Command byte (PLUGIN_CMD_START, PLUGIN_CMD_PAUSE, PLUGIN_CMD_RESET, PLUGIN_CMD_STOP)
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t plugin_system_broadcast_command(uint8_t plugin_id, uint8_t cmd)
{
    /* Only root nodes can broadcast */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "Not root node, cannot broadcast command");
        return ESP_ERR_INVALID_STATE;
    }

    /* Block commands during root setup (except state query, which is handled elsewhere) */
    if (mesh_root_is_setup_in_progress()) {
        ESP_LOGW(TAG, "Plugin command blocked during root setup: plugin ID 0x%02X, command 0x%02X", plugin_id, cmd);
        return ESP_ERR_INVALID_STATE;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;
    if (child_node_count == 0) {
        ESP_LOGD(TAG, "No child nodes to broadcast command");
        return ESP_OK;
    }

    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = plugin_id;  /* Plugin ID */
    tx_buf[1] = cmd;         /* Command byte */

    mesh_data_t data;
    data.data = tx_buf;
    data.size = 2;  /* Plugin ID(1) + CMD(1) */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    int success_count = 0;
    int fail_count = 0;
    /* Skip index 0 (root node itself), only send to child nodes */
    for (int i = 1; i < route_table_size; i++) {
        esp_err_t err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err == ESP_OK) {
            success_count++;
        } else {
            fail_count++;
            ESP_LOGD(TAG, "Plugin command broadcast err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }

    const char *cmd_name = (cmd == PLUGIN_CMD_START) ? "START" :
                          (cmd == PLUGIN_CMD_PAUSE) ? "PAUSE" :
                          (cmd == PLUGIN_CMD_RESET) ? "RESET" :
                          (cmd == PLUGIN_CMD_STOP) ? "STOP" : "UNKNOWN";
    ESP_LOGI(TAG, "Plugin command %s (plugin ID 0x%02X) broadcast - sent to %d/%d child nodes (success:%d, failed:%d)",
             cmd_name, plugin_id, success_count, child_node_count, success_count, fail_count);

    return ESP_OK;
}

esp_err_t plugin_activate(const char *name)
{
    if (name == NULL) {
        ESP_LOGE(TAG, "Plugin activation failed: name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Look up plugin */
    const plugin_info_t *plugin = plugin_get_by_name(name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "Plugin activation failed: plugin '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }

    /* If another plugin is active, deactivate it first */
    if (active_plugin_name != NULL) {
        if (strcmp(active_plugin_name, name) == 0) {
            /* Already active, no-op */
            ESP_LOGD(TAG, "Plugin '%s' is already active", name);
            return ESP_OK;
        }

        /* Deactivate current plugin */
        esp_err_t deactivate_err = plugin_deactivate(active_plugin_name);
        if (deactivate_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deactivate current plugin '%s': %s",
                     active_plugin_name, esp_err_to_name(deactivate_err));
            return deactivate_err;
        }
    }

    /* Set as active plugin BEFORE calling on_activate to prevent race conditions
     * (e.g., timer callbacks that check plugin_is_active() during on_activate) */
    active_plugin_name = plugin->name;

    /* Call plugin's on_activate callback if provided */
    if (plugin->callbacks.on_activate != NULL) {
        esp_err_t activate_err = plugin->callbacks.on_activate();
        if (activate_err != ESP_OK) {
            ESP_LOGE(TAG, "Plugin '%s' on_activate callback failed: %s",
                     name, esp_err_to_name(activate_err));
            /* Rollback: clear active plugin name on failure */
            active_plugin_name = NULL;
            return activate_err;
        }
    }

    ESP_LOGI(TAG, "Plugin '%s' activated", name);

    /* If root node, call on_start locally and broadcast START command to child nodes */
    if (esp_mesh_is_root()) {
        /* Call on_start callback on root node itself */
        if (plugin->callbacks.on_start != NULL) {
            esp_err_t start_err = plugin->callbacks.on_start();
            if (start_err != ESP_OK) {
                ESP_LOGW(TAG, "Plugin '%s' on_start callback returned error on root node: %s",
                         name, esp_err_to_name(start_err));
                /* Continue with broadcast even if on_start fails */
            }
        }

        mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
        int route_table_size = 0;
        esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

        int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;
        if (child_node_count > 0) {
            /* Block plugin activation broadcast during root setup */
            if (mesh_root_is_setup_in_progress()) {
                ESP_LOGD(TAG, "Plugin activation broadcast blocked during root setup");
                /* Continue with local activation, but skip broadcast */
            } else {
                uint8_t *tx_buf = mesh_common_get_tx_buf();
                tx_buf[0] = plugin->command_id;  /* Plugin ID */
                tx_buf[1] = PLUGIN_CMD_START;    /* START command */

                mesh_data_t data;
                data.data = tx_buf;
                data.size = 2;  /* Plugin ID(1) + CMD(1) */
                data.proto = MESH_PROTO_BIN;
                data.tos = MESH_TOS_P2P;

                int success_count = 0;
                int fail_count = 0;
                /* Skip index 0 (root node itself), only send to child nodes */
                for (int i = 1; i < route_table_size; i++) {
                    esp_err_t err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
                    if (err == ESP_OK) {
                        success_count++;
                    } else {
                        fail_count++;
                        ESP_LOGD(TAG, "Plugin START broadcast err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
                    }
                }

                ESP_LOGI(TAG, "Plugin '%s' START command broadcast - sent to %d/%d child nodes (success:%d, failed:%d)",
                         name, success_count, child_node_count, success_count, fail_count);
            }
        } else {
            ESP_LOGD(TAG, "Plugin '%s' activated on root node - no child nodes to broadcast", name);
        }
    }

    return ESP_OK;
}

esp_err_t plugin_deactivate(const char *name)
{
    if (name == NULL) {
        ESP_LOGE(TAG, "Plugin deactivation failed: name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if plugin is currently active */
    if (active_plugin_name == NULL || strcmp(active_plugin_name, name) != 0) {
        ESP_LOGD(TAG, "Plugin '%s' is not active, nothing to deactivate", name);
        return ESP_ERR_INVALID_STATE;
    }

    /* Look up plugin */
    const plugin_info_t *plugin = plugin_get_by_name(name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "Plugin deactivation failed: plugin '%s' not found", name);
        active_plugin_name = NULL; /* Clear active plugin even if lookup fails */
        return ESP_ERR_NOT_FOUND;
    }

    /* Call plugin's on_deactivate callback if provided */
    if (plugin->callbacks.on_deactivate != NULL) {
        esp_err_t deactivate_err = plugin->callbacks.on_deactivate();
        if (deactivate_err != ESP_OK) {
            ESP_LOGW(TAG, "Plugin '%s' on_deactivate callback returned error: %s",
                     name, esp_err_to_name(deactivate_err));
            /* Continue with deactivation even if callback fails */
        }
    }

    /* Clear active plugin */
    active_plugin_name = NULL;
    ESP_LOGI(TAG, "Plugin '%s' deactivated", name);

    return ESP_OK;
}

esp_err_t plugin_deactivate_all(void)
{
    if (active_plugin_name == NULL) {
        return ESP_OK; /* No plugin active */
    }

    return plugin_deactivate(active_plugin_name);
}

const char *plugin_get_active(void)
{
    return active_plugin_name;
}

esp_err_t plugin_send_start_to_node(const mesh_addr_t *node_addr)
{
    if (node_addr == NULL) {
        ESP_LOGE(TAG, "plugin_send_start_to_node: node_addr is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Only root node should send plugin state to other nodes */
    if (!esp_mesh_is_root()) {
        ESP_LOGD(TAG, "plugin_send_start_to_node: not root node, skipping");
        return ESP_ERR_INVALID_STATE;
    }

    /* Block commands during root setup */
    if (mesh_root_is_setup_in_progress()) {
        ESP_LOGD(TAG, "plugin_send_start_to_node: blocked during root setup");
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if plugin is active */
    const char *active_name = plugin_get_active();
    if (active_name == NULL) {
        /* No active plugin - nothing to send */
        ESP_LOGD(TAG, "plugin_send_start_to_node: no active plugin, skipping");
        return ESP_OK;
    }

    /* Get plugin info */
    const plugin_info_t *plugin = plugin_get_by_name(active_name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "plugin_send_start_to_node: active plugin '%s' not found in registry", active_name);
        return ESP_ERR_NOT_FOUND;
    }

    /* Verify plugin is still active (race condition protection) */
    if (plugin_get_active() != active_name) {
        ESP_LOGD(TAG, "plugin_send_start_to_node: plugin '%s' was deactivated, skipping", active_name);
        return ESP_OK;
    }

    /* Construct START command packet */
    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = plugin->command_id;  /* Plugin ID */
    tx_buf[1] = PLUGIN_CMD_START;    /* START command */

    mesh_data_t data;
    data.data = tx_buf;
    data.size = 2;  /* Plugin ID(1) + CMD(1) */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Send to specific node */
    esp_err_t err = mesh_send_with_bridge(node_addr, &data, MESH_DATA_P2P, NULL, 0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Plugin '%s' START command sent to newly joined node "MACSTR,
                 active_name, MAC2STR(node_addr->addr));
    } else {
        ESP_LOGW(TAG, "Plugin '%s' START command send failed to "MACSTR": %s",
                 active_name, MAC2STR(node_addr->addr), esp_err_to_name(err));
    }

    return err;
}

bool plugin_is_active(const char *name)
{
    if (name == NULL) {
        return false;
    }

    if (active_plugin_name == NULL) {
        return false;
    }

    return (strcmp(active_plugin_name, name) == 0);
}

esp_err_t plugin_get_all_names(const char *names[], uint8_t max_count, uint8_t *count)
{
    if (names == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (max_count < plugin_count) {
        ESP_LOGE(TAG, "plugin_get_all_names failed: max_count (%d) is less than plugin_count (%d)", max_count, plugin_count);
        return ESP_ERR_INVALID_SIZE;
    }

    *count = plugin_count;
    for (uint8_t i = 0; i < plugin_count; i++) {
        names[i] = plugin_registry[i].name;
    }

    return ESP_OK;
}

esp_err_t plugin_system_handle_plugin_command(uint8_t *data, uint16_t len)
{
    /* Validate data pointer and minimum length */
    if (data == NULL) {
        ESP_LOGE(TAG, "Plugin command routing failed: data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (len < 2) {
        ESP_LOGE(TAG, "Plugin command routing failed: len < 2 (need plugin ID + command byte)");
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract plugin ID from first byte */
    uint8_t plugin_id = data[0];

    /* Validate plugin ID is in plugin range */
    if (plugin_id < PLUGIN_ID_MIN || plugin_id > PLUGIN_ID_MAX) {
        ESP_LOGE(TAG, "Plugin command routing failed: plugin ID 0x%02X outside plugin range (0x%02X-0x%02X)", plugin_id, PLUGIN_ID_MIN, PLUGIN_ID_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract command byte from second byte */
    uint8_t cmd = data[1];

    /* Validate command byte */
    if (cmd != PLUGIN_CMD_START && cmd != PLUGIN_CMD_PAUSE && cmd != PLUGIN_CMD_RESET && cmd != PLUGIN_CMD_STOP) {
        ESP_LOGE(TAG, "Plugin command routing failed: invalid command byte 0x%02X (expected 0x%02X-0x%02X or 0x%02X)", cmd, PLUGIN_CMD_START, PLUGIN_CMD_RESET, PLUGIN_CMD_STOP);
        return ESP_ERR_INVALID_ARG;
    }

    /* Look up plugin by plugin ID */
    const plugin_info_t *plugin = plugin_get_by_id(plugin_id);
    if (plugin == NULL) {
        ESP_LOGD(TAG, "Plugin command routing: no plugin registered for plugin ID 0x%02X", plugin_id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Route command to appropriate callback */
    esp_err_t err = ESP_OK;
    switch (cmd) {
        case PLUGIN_CMD_START:
            /* Root nodes should ignore START commands received via mesh
             * Root nodes start plugins via direct activation, not via mesh commands
             * This prevents root nodes from processing their own broadcasts
             */
            if (esp_mesh_is_root()) {
                return ESP_OK; /* Silently ignore - root node doesn't process START via mesh */
            }
            /* Enforce mutual exclusivity: START command must stop other plugins and activate this one */
            esp_err_t deactivate_err = plugin_deactivate_all();
            if (deactivate_err != ESP_OK && deactivate_err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "Failed to deactivate other plugins before START: %s", esp_err_to_name(deactivate_err));
            }
            /* Activate the target plugin */
            esp_err_t activate_err = plugin_activate(plugin->name);
            if (activate_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to activate plugin '%s' for START command: %s", plugin->name, esp_err_to_name(activate_err));
                return activate_err;
            }
            if (plugin->callbacks.on_start == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_start callback", plugin->name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 2) {
                ESP_LOGE(TAG, "Plugin command routing failed: START command requires len=2, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_start();
            break;

        case PLUGIN_CMD_PAUSE:
            /* Root nodes should ignore PAUSE commands received via mesh
             * Root nodes pause plugins via direct API calls, not via mesh commands
             * This prevents root nodes from processing their own broadcasts
             */
            if (esp_mesh_is_root()) {
                return ESP_OK; /* Silently ignore - root node doesn't process PAUSE via mesh */
            }
            if (plugin->callbacks.on_pause == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_pause callback", plugin->name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 2) {
                ESP_LOGE(TAG, "Plugin command routing failed: PAUSE command requires len=2, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_pause();
            break;

        case PLUGIN_CMD_RESET:
            /* Root nodes should ignore RESET commands received via mesh
             * Root nodes reset plugins via direct API calls, not via mesh commands
             * This prevents root nodes from processing their own broadcasts
             */
            if (esp_mesh_is_root()) {
                return ESP_OK; /* Silently ignore - root node doesn't process RESET via mesh */
            }
            if (plugin->callbacks.on_reset == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_reset callback", plugin->name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 2) {
                ESP_LOGE(TAG, "Plugin command routing failed: RESET command requires len=2, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_reset();
            break;

        case PLUGIN_CMD_STOP:
            /* Root nodes should ignore STOP commands received via mesh
             * Root nodes stop plugins via direct API calls, not via mesh commands
             * This prevents root nodes from processing their own broadcasts
             */
            if (esp_mesh_is_root()) {
                return ESP_OK; /* Silently ignore - root node doesn't process STOP via mesh */
            }
            /* Call plugin's on_stop callback if registered (optional) */
            if (plugin->callbacks.on_stop != NULL) {
                err = plugin->callbacks.on_stop();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Plugin '%s' on_stop callback returned error: %s", plugin->name, esp_err_to_name(err));
                    /* Continue with deactivation even if on_stop fails */
                }
            }
            /* Deactivate plugin */
            err = plugin_deactivate(plugin->name);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to deactivate plugin '%s' for STOP command: %s", plugin->name, esp_err_to_name(err));
                return err;
            }
            break;

        default:
            ESP_LOGE(TAG, "Plugin command routing failed: unhandled command byte 0x%02X", cmd);
            return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Plugin '%s' command callback (0x%02X) returned error: %s", plugin->name, cmd, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Plugin command routed to plugin '%s' (plugin ID 0x%02X, command 0x%02X)", plugin->name, plugin_id, cmd);
    }

    return err;
}

/**
 * @brief Handle plugin command from API (root node processes locally and broadcasts)
 *
 * This function is called by API handlers when root node receives a command via HTTP API.
 * Unlike plugin_system_handle_plugin_command(), this function:
 * - Processes the command locally on root node (calls plugin callbacks)
 * - Broadcasts the command to all child nodes
 * - Does NOT ignore commands on root node
 *
 * @param data Command data: [PLUGIN_ID:1] [CMD:1]
 * @param len Command length (must be 2 for fixed-size commands)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t plugin_system_handle_plugin_command_from_api(uint8_t *data, uint16_t len)
{
    /* Validate data pointer and minimum length */
    if (data == NULL) {
        ESP_LOGE(TAG, "Plugin command from API failed: data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (len < 2) {
        ESP_LOGE(TAG, "Plugin command from API failed: len < 2 (need plugin ID + command byte)");
        return ESP_ERR_INVALID_ARG;
    }

    /* Extract plugin ID and command */
    uint8_t plugin_id = data[0];
    uint8_t cmd = data[1];

    /* Validate plugin ID is in plugin range */
    if (plugin_id < PLUGIN_ID_MIN || plugin_id > PLUGIN_ID_MAX) {
        ESP_LOGE(TAG, "Plugin command from API failed: plugin ID 0x%02X outside plugin range (0x%02X-0x%02X)", plugin_id, PLUGIN_ID_MIN, PLUGIN_ID_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate command byte */
    if (cmd != PLUGIN_CMD_START && cmd != PLUGIN_CMD_PAUSE && cmd != PLUGIN_CMD_RESET && cmd != PLUGIN_CMD_STOP) {
        ESP_LOGE(TAG, "Plugin command from API failed: invalid command byte 0x%02X", cmd);
        return ESP_ERR_INVALID_ARG;
    }

    /* Look up plugin by plugin ID */
    const plugin_info_t *plugin = plugin_get_by_id(plugin_id);
    if (plugin == NULL) {
        ESP_LOGD(TAG, "Plugin command from API: no plugin registered for plugin ID 0x%02X", plugin_id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Only root node should call this function */
    if (!esp_mesh_is_root()) {
        ESP_LOGW(TAG, "plugin_system_handle_plugin_command_from_api called on non-root node, falling back to regular handler");
        return plugin_system_handle_plugin_command(data, len);
    }

    esp_err_t err = ESP_OK;

    /* Process command locally on root node */
    switch (cmd) {
        case PLUGIN_CMD_START:
            /* START command handled by plugin_activate(), not here */
            ESP_LOGE(TAG, "START command should not be called via API handler, use plugin_activate() instead");
            return ESP_ERR_INVALID_ARG;

        case PLUGIN_CMD_PAUSE:
            if (plugin->callbacks.on_pause == NULL) {
                ESP_LOGD(TAG, "Plugin command from API: plugin '%s' has no on_pause callback", plugin->name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 2) {
                ESP_LOGE(TAG, "Plugin command from API failed: PAUSE command requires len=2, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_pause();
            break;

        case PLUGIN_CMD_RESET:
            if (plugin->callbacks.on_reset == NULL) {
                ESP_LOGD(TAG, "Plugin command from API: plugin '%s' has no on_reset callback", plugin->name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 2) {
                ESP_LOGE(TAG, "Plugin command from API failed: RESET command requires len=2, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_reset();
            break;

        case PLUGIN_CMD_STOP:
            /* Call plugin's on_stop callback if registered (optional) */
            if (plugin->callbacks.on_stop != NULL) {
                err = plugin->callbacks.on_stop();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Plugin '%s' on_stop callback returned error: %s", plugin->name, esp_err_to_name(err));
                    /* Continue with deactivation even if on_stop fails */
                }
            }
            /* Deactivate plugin */
            err = plugin_deactivate(plugin->name);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to deactivate plugin '%s' for STOP command: %s", plugin->name, esp_err_to_name(err));
                return err;
            }
            break;

        default:
            ESP_LOGE(TAG, "Plugin command from API failed: unhandled command byte 0x%02X", cmd);
            return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Plugin '%s' command callback (0x%02X) from API returned error: %s", plugin->name, cmd, esp_err_to_name(err));
        return err;
    }

    /* Broadcast command to child nodes */
    esp_err_t broadcast_err = plugin_system_broadcast_command(plugin_id, cmd);
    if (broadcast_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to broadcast command 0x%02X for plugin '%s': %s", cmd, plugin->name, esp_err_to_name(broadcast_err));
        /* Don't fail the API call if broadcast fails - local processing succeeded */
    }

    ESP_LOGI(TAG, "Plugin command from API processed: plugin '%s' (plugin ID 0x%02X, command 0x%02X)", plugin->name, plugin_id, cmd);
    return ESP_OK;
}

esp_err_t plugin_query_state(const char *plugin_name, uint32_t query_type, void *result)
{
    if (plugin_name == NULL || result == NULL) {
        ESP_LOGE(TAG, "plugin_query_state failed: plugin_name or result is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "plugin_query_state failed: plugin '%s' not found", plugin_name);
        return ESP_ERR_NOT_FOUND;
    }

    if (plugin->callbacks.get_state == NULL) {
        ESP_LOGE(TAG, "plugin_query_state failed: plugin '%s' has no get_state callback", plugin_name);
        return ESP_ERR_INVALID_STATE;
    }

    return plugin->callbacks.get_state(query_type, result);
}

esp_err_t plugin_execute_operation(const char *plugin_name, uint32_t operation_type, void *params)
{
    if (plugin_name == NULL) {
        ESP_LOGE(TAG, "plugin_execute_operation failed: plugin_name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "plugin_execute_operation failed: plugin '%s' not found", plugin_name);
        return ESP_ERR_NOT_FOUND;
    }

    if (plugin->callbacks.execute_operation == NULL) {
        ESP_LOGE(TAG, "plugin_execute_operation failed: plugin '%s' has no execute_operation callback", plugin_name);
        return ESP_ERR_INVALID_STATE;
    }

    return plugin->callbacks.execute_operation(operation_type, params);
}

esp_err_t plugin_get_helper(const char *plugin_name, uint32_t helper_type, void *params, void *result)
{
    if (plugin_name == NULL || result == NULL) {
        ESP_LOGE(TAG, "plugin_get_helper failed: plugin_name or result is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "plugin_get_helper failed: plugin '%s' not found", plugin_name);
        return ESP_ERR_NOT_FOUND;
    }

    if (plugin->callbacks.get_helper == NULL) {
        ESP_LOGE(TAG, "plugin_get_helper failed: plugin '%s' has no get_helper callback", plugin_name);
        return ESP_ERR_INVALID_STATE;
    }

    return plugin->callbacks.get_helper(helper_type, params, result);
}
