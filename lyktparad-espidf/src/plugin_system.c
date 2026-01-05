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
#include "esp_log.h"
#include <string.h>

static const char *TAG = "plugin_system";

#define MAX_PLUGINS 16
#define PLUGIN_CMD_ID_MIN 0x10
#define PLUGIN_CMD_ID_MAX 0xEF

/* Plugin registry */
static plugin_info_t plugin_registry[MAX_PLUGINS];
static uint8_t plugin_count = 0;
static uint8_t next_command_id = PLUGIN_CMD_ID_MIN;

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

    /* Check if command ID range is exhausted */
    if (next_command_id > PLUGIN_CMD_ID_MAX) {
        ESP_LOGE(TAG, "Plugin registration failed: command ID range exhausted (0x%02X-0x%02X)", PLUGIN_CMD_ID_MIN, PLUGIN_CMD_ID_MAX);
        return ESP_ERR_NO_MEM;
    }

    /* Assign command ID and prepare plugin copy */
    plugin_info_t plugin_copy = *info;
    plugin_copy.command_id = next_command_id;

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
    *assigned_cmd_id = next_command_id;

    /* Increment counters */
    plugin_count++;
    next_command_id++;

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

const plugin_info_t *plugin_get_by_cmd_id(uint8_t cmd_id)
{
    /* Validate command ID is in plugin range */
    if (cmd_id < PLUGIN_CMD_ID_MIN || cmd_id > PLUGIN_CMD_ID_MAX) {
        return NULL;
    }

    for (uint8_t i = 0; i < plugin_count; i++) {
        if (plugin_registry[i].command_id == cmd_id) {
            return &plugin_registry[i];
        }
    }

    return NULL;
}

esp_err_t plugin_system_handle_command(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* Validate command ID is in plugin range */
    if (cmd < PLUGIN_CMD_ID_MIN || cmd > PLUGIN_CMD_ID_MAX) {
        ESP_LOGE(TAG, "Command routing failed: command ID 0x%02X outside plugin range (0x%02X-0x%02X)", cmd, PLUGIN_CMD_ID_MIN, PLUGIN_CMD_ID_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate data pointer if len > 0 */
    if (len > 0 && data == NULL) {
        ESP_LOGE(TAG, "Command routing failed: data pointer is NULL but len > 0");
        return ESP_ERR_INVALID_ARG;
    }

    /* Look up plugin by command ID */
    const plugin_info_t *plugin = plugin_get_by_cmd_id(cmd);
    if (plugin == NULL) {
        ESP_LOGD(TAG, "Command routing: no plugin registered for command ID 0x%02X", cmd);
        return ESP_ERR_NOT_FOUND;
    }

    /* Check if plugin is active */
    if (!plugin_is_active(plugin->name)) {
        ESP_LOGD(TAG, "Command routing: plugin '%s' is not active, rejecting command 0x%02X", plugin->name, cmd);
        return ESP_ERR_INVALID_STATE;
    }

    /* Call plugin's command handler */
    if (plugin->callbacks.command_handler == NULL) {
        ESP_LOGE(TAG, "Command routing failed: plugin '%s' has NULL command_handler", plugin->name);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = plugin->callbacks.command_handler(cmd, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Plugin '%s' command handler returned error: %s", plugin->name, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Command routed to plugin '%s' (command ID 0x%02X)", plugin->name, cmd);
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

    /* Call plugin's on_activate callback if provided */
    if (plugin->callbacks.on_activate != NULL) {
        esp_err_t activate_err = plugin->callbacks.on_activate();
        if (activate_err != ESP_OK) {
            ESP_LOGE(TAG, "Plugin '%s' on_activate callback failed: %s",
                     name, esp_err_to_name(activate_err));
            return activate_err;
        }
    }

    /* Set as active plugin */
    active_plugin_name = plugin->name;
    ESP_LOGI(TAG, "Plugin '%s' activated", name);

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

esp_err_t plugin_system_handle_plugin_command(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* Validate command ID */
    if (cmd != 0x05 && cmd != 0x06 && cmd != 0x07 && cmd != 0x08) {
        ESP_LOGE(TAG, "Plugin command routing failed: invalid command ID 0x%02X (expected 0x05-0x08)", cmd);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate data pointer if len > 0 */
    if (len > 0 && data == NULL) {
        ESP_LOGE(TAG, "Plugin command routing failed: data pointer is NULL but len > 0");
        return ESP_ERR_INVALID_ARG;
    }

    /* Get active plugin */
    if (active_plugin_name == NULL) {
        ESP_LOGD(TAG, "Plugin command routing: no active plugin");
        return ESP_ERR_NOT_FOUND;
    }

    /* Look up active plugin */
    const plugin_info_t *plugin = plugin_get_by_name(active_plugin_name);
    if (plugin == NULL) {
        ESP_LOGE(TAG, "Plugin command routing failed: active plugin '%s' not found in registry", active_plugin_name);
        return ESP_ERR_NOT_FOUND;
    }

    /* Route command to appropriate callback */
    esp_err_t err = ESP_OK;
    switch (cmd) {
        case 0x05: /* MESH_CMD_PLUGIN_START */
            if (plugin->callbacks.on_start == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_start callback", active_plugin_name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 1) {
                ESP_LOGE(TAG, "Plugin command routing failed: START command requires len=1, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_start();
            break;

        case 0x06: /* MESH_CMD_PLUGIN_PAUSE */
            if (plugin->callbacks.on_pause == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_pause callback", active_plugin_name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 1) {
                ESP_LOGE(TAG, "Plugin command routing failed: PAUSE command requires len=1, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_pause();
            break;

        case 0x07: /* MESH_CMD_PLUGIN_RESET */
            if (plugin->callbacks.on_reset == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_reset callback", active_plugin_name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 1) {
                ESP_LOGE(TAG, "Plugin command routing failed: RESET command requires len=1, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_reset();
            break;

        case 0x08: /* MESH_CMD_PLUGIN_BEAT */
            if (plugin->callbacks.on_beat == NULL) {
                ESP_LOGD(TAG, "Plugin command routing: plugin '%s' has no on_beat callback", active_plugin_name);
                return ESP_ERR_INVALID_STATE;
            }
            if (len != 2) {
                ESP_LOGE(TAG, "Plugin command routing failed: BEAT command requires len=2, got %d", len);
                return ESP_ERR_INVALID_ARG;
            }
            err = plugin->callbacks.on_beat(data, len);
            break;

        default:
            ESP_LOGE(TAG, "Plugin command routing failed: unhandled command ID 0x%02X", cmd);
            return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Plugin '%s' command callback (0x%02X) returned error: %s", active_plugin_name, cmd, esp_err_to_name(err));
    } else {
        ESP_LOGD(TAG, "Plugin command routed to plugin '%s' (command ID 0x%02X)", active_plugin_name, cmd);
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
