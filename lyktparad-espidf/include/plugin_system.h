/* Plugin System Header
 *
 * This module provides a plugin system infrastructure for the mesh network.
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

#ifndef __PLUGIN_SYSTEM_H__
#define __PLUGIN_SYSTEM_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Plugin Command ID Range
 *******************************************************/

/* Plugin commands are assigned IDs in the range 0x10-0xEF (224 plugins maximum)
 * Command IDs are assigned automatically during registration, starting from 0x10
 * and incrementing sequentially for each registered plugin.
 */

/*******************************************************
 *                Plugin Callback Interface
 *******************************************************/

/**
 * @brief Plugin callback function structure
 *
 * This structure defines the callback functions that a plugin must provide.
 * Only command_handler is required; all other callbacks are optional (may be NULL).
 */
typedef struct {
    /**
     * @brief Command handler callback (required)
     *
     * Handle mesh commands for this plugin.
     *
     * @param cmd Command ID (should match plugin's assigned command ID)
     * @param data Pointer to command data (includes command byte at data[0])
     * @param len Length of command data in bytes (includes command byte)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*command_handler)(uint8_t cmd, uint8_t *data, uint16_t len);

    /**
     * @brief Timer callback (optional, may be NULL)
     *
     * Periodic callback for timer-based plugins (e.g., effects, sequences).
     * Called when plugin's timer fires.
     *
     * @param arg Optional argument passed when timer was created
     */
    void (*timer_callback)(void *arg);

    /**
     * @brief Plugin initialization callback (optional, may be NULL)
     *
     * Called during plugin registration to initialize plugin state.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*init)(void);

    /**
     * @brief Plugin deinitialization callback (optional, may be NULL)
     *
     * Called when plugin is unregistered to clean up plugin state.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*deinit)(void);

    /**
     * @brief Plugin active state query callback (optional, may be NULL)
     *
     * Query whether plugin is currently active (e.g., effect/sequence playing).
     *
     * @return true if plugin is active, false otherwise
     */
    bool (*is_active)(void);
} plugin_callback_t;

/*******************************************************
 *                Plugin Info Structure
 *******************************************************/

/**
 * @brief Plugin information structure
 *
 * This structure contains information about a registered plugin,
 * including its name, assigned command ID, and callback functions.
 */
typedef struct {
    /**
     * @brief Plugin name (unique identifier, required, non-NULL)
     *
     * Plugin names must be unique. Registration will fail if a plugin
     * with the same name is already registered.
     */
    const char *name;

    /**
     * @brief Assigned command ID (set by registration system)
     *
     * This field is set by plugin_register() to the automatically
     * assigned command ID (0x10-0xEF). Initially should be 0.
     */
    uint8_t command_id;

    /**
     * @brief Plugin callback functions
     *
     * command_handler is required; all other callbacks are optional.
     */
    plugin_callback_t callbacks;

    /**
     * @brief Optional user data pointer (may be NULL)
     *
     * This field is not used by the plugin system but may be used
     * by plugins to store additional data.
     */
    void *user_data;
} plugin_info_t;

/*******************************************************
 *                Plugin Registration Functions
 *******************************************************/

/**
 * @brief Register a plugin with the plugin system
 *
 * This function registers a plugin and assigns it a unique command ID
 * in the range 0x10-0xEF. Command IDs are assigned sequentially starting
 * from 0x10.
 *
 * @param info Plugin information structure (must contain valid name and command_handler)
 * @param assigned_cmd_id Output parameter for assigned command ID (non-NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if info, info->name, or assigned_cmd_id is NULL, or if name is empty
 * @return ESP_ERR_INVALID_ARG if command_handler callback is NULL
 * @return ESP_ERR_INVALID_STATE if plugin with same name is already registered
 * @return ESP_ERR_NO_MEM if plugin registry is full or command ID range is exhausted
 */
esp_err_t plugin_register(const plugin_info_t *info, uint8_t *assigned_cmd_id);

/**
 * @brief Look up plugin by name
 *
 * @param name Plugin name (non-NULL)
 * @return Pointer to plugin info if found, NULL if not found or name is NULL
 */
const plugin_info_t *plugin_get_by_name(const char *name);

/**
 * @brief Look up plugin by command ID
 *
 * @param cmd_id Command ID (must be in plugin range 0x10-0xEF)
 * @return Pointer to plugin info if found, NULL if not found or cmd_id is outside range
 */
const plugin_info_t *plugin_get_by_cmd_id(uint8_t cmd_id);

/**
 * @brief Handle command routing to plugin system
 *
 * This function routes commands in the plugin range (0x10-0xEF) to the
 * appropriate plugin's command_handler callback.
 *
 * @param cmd Command ID (must be in plugin range 0x10-0xEF)
 * @param data Pointer to command data (includes command byte at data[0], must be non-NULL if len > 0)
 * @param len Length of command data in bytes (includes command byte)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if cmd is outside plugin range or data is NULL when len > 0
 * @return ESP_ERR_NOT_FOUND if no plugin is registered for the given command ID
 * @return Error code from plugin's command_handler callback
 */
esp_err_t plugin_system_handle_command(uint8_t cmd, uint8_t *data, uint16_t len);

#endif /* __PLUGIN_SYSTEM_H__ */
