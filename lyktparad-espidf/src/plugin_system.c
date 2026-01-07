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
    if (cmd != PLUGIN_CMD_START && cmd != PLUGIN_CMD_PAUSE && cmd != PLUGIN_CMD_RESET && cmd != PLUGIN_CMD_BEAT) {
        ESP_LOGE(TAG, "Plugin command routing failed: invalid command byte 0x%02X (expected 0x%02X-0x%02X)", cmd, PLUGIN_CMD_START, PLUGIN_CMD_BEAT);
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

        case PLUGIN_CMD_BEAT:
            /* Root nodes should ignore BEAT commands received via mesh
             * Root nodes broadcast BEAT commands but don't process them when received back
             * This prevents root nodes from processing their own broadcasts
             */
            if (esp_mesh_is_root()) {
                return ESP_OK; /* Silently ignore - root node doesn't process BEAT via mesh */
            }
            if (plugin->callbacks.on_beat == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_beat callback", plugin->name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len < 4) {
                ESP_LOGE(TAG, "Plugin command routing failed: BEAT command requires len>=4, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            /* Pass remaining data (skip plugin ID and command bytes) to on_beat: pointer + counter */
            err = plugin->callbacks.on_beat(&data[2], len - 2);
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
