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
