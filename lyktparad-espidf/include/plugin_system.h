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

    /**
     * @brief Plugin activation callback (optional, may be NULL)
     *
     * Called when plugin is activated. Plugin should initialize
     * its state if needed.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_activate)(void);

    /**
     * @brief Plugin deactivation callback (optional, may be NULL)
     *
     * Called when plugin is deactivated. Plugin should stop
     * timers, clear state, and clean up resources.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_deactivate)(void);

    /**
     * @brief Plugin START command callback (optional, may be NULL)
     *
     * Called when MESH_CMD_PLUGIN_START (0x05) is received.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_start)(void);

    /**
     * @brief Plugin PAUSE command callback (optional, may be NULL)
     *
     * Called when MESH_CMD_PLUGIN_PAUSE (0x06) is received.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_pause)(void);

    /**
     * @brief Plugin RESET command callback (optional, may be NULL)
     *
     * Called when MESH_CMD_PLUGIN_RESET (0x07) is received.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_reset)(void);

    /**
     * @brief Plugin BEAT command callback (optional, may be NULL)
     *
     * Called when MESH_CMD_PLUGIN_BEAT (0x08) is received.
     *
     * @param data Command data (data[0] = MESH_CMD_PLUGIN_BEAT, data[1] = pointer)
     * @param len Data length (must be 2)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_beat)(uint8_t *data, uint16_t len);

    /**
     * @brief Plugin state query callback (optional, may be NULL)
     *
     * Query plugin-specific state (pointer, active status, rhythm, length, etc.).
     *
     * @param query_type Query type identifier (plugin-specific enum or integer)
     * @param result Output parameter for query result (type depends on query_type)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*get_state)(uint32_t query_type, void *result);

    /**
     * @brief Plugin operation execution callback (optional, may be NULL)
     *
     * Execute plugin operations (store, start, pause, reset, broadcast_beat, etc.).
     *
     * @param operation_type Operation type identifier (plugin-specific enum or integer)
     * @param params Operation parameters (type depends on operation_type)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*execute_operation)(uint32_t operation_type, void *params);

    /**
     * @brief Plugin helper function callback (optional, may be NULL)
     *
     * Get helper function results (size calculations, etc.).
     *
     * @param helper_type Helper type identifier (plugin-specific enum or integer)
     * @param params Helper parameters (type depends on helper_type)
     * @param result Output parameter for helper result (type depends on helper_type)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*get_helper)(uint32_t helper_type, void *params, void *result);
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

/**
 * @brief Activate a plugin by name
 *
 * Activates the specified plugin and automatically deactivates
 * any currently active plugin. Only one plugin can be active at a time.
 *
 * @param name Plugin name (non-NULL, must be registered)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if name is NULL
 * @return ESP_ERR_NOT_FOUND if plugin with name is not registered
 * @return Error code from plugin's on_activate callback if it fails
 */
esp_err_t plugin_activate(const char *name);

/**
 * @brief Deactivate a plugin by name
 *
 * Deactivates the specified plugin if it is currently active.
 *
 * @param name Plugin name (non-NULL, must be registered)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if name is NULL
 * @return ESP_ERR_NOT_FOUND if plugin with name is not registered
 * @return ESP_ERR_INVALID_STATE if plugin is not currently active
 * @return Error code from plugin's on_deactivate callback if it fails
 */
esp_err_t plugin_deactivate(const char *name);

/**
 * @brief Deactivate all plugins
 *
 * Deactivates the currently active plugin (if any).
 *
 * @return ESP_OK on success
 */
esp_err_t plugin_deactivate_all(void);

/**
 * @brief Get the name of the currently active plugin
 *
 * @return Pointer to active plugin name, or NULL if no plugin is active
 */
const char *plugin_get_active(void);

/**
 * @brief Check if a plugin is currently active
 *
 * @param name Plugin name (non-NULL)
 * @return true if plugin is active, false otherwise
 */
bool plugin_is_active(const char *name);

/**
 * @brief Get all registered plugin names
 *
 * @param names Output array of plugin names (must be large enough for max_count)
 * @param max_count Maximum number of plugin names that can be stored in names array
 * @param count Output parameter for number of plugins (non-NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if names or count is NULL
 * @return ESP_ERR_INVALID_SIZE if max_count is less than the number of registered plugins
 */
esp_err_t plugin_get_all_names(const char *names[], uint8_t max_count, uint8_t *count);

/**
 * @brief Handle plugin command routing for MESH_CMD_PLUGIN_* commands
 *
 * This function routes core plugin commands (MESH_CMD_PLUGIN_START, MESH_CMD_PLUGIN_PAUSE,
 * MESH_CMD_PLUGIN_RESET, MESH_CMD_PLUGIN_BEAT) to the active plugin's callbacks.
 *
 * @param cmd Command ID (MESH_CMD_PLUGIN_START, MESH_CMD_PLUGIN_PAUSE, MESH_CMD_PLUGIN_RESET, or MESH_CMD_PLUGIN_BEAT)
 * @param data Command data (for BEAT command, data[1] contains pointer)
 * @param len Data length (1 for START/PAUSE/RESET, 2 for BEAT)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if cmd is not a valid plugin command or data is NULL when len > 0
 * @return ESP_ERR_NOT_FOUND if no plugin is active
 * @return ESP_ERR_INVALID_STATE if active plugin doesn't have the required callback
 * @return Error code from plugin's callback
 */
esp_err_t plugin_system_handle_plugin_command(uint8_t cmd, uint8_t *data, uint16_t len);

/**
 * @brief Query plugin state
 *
 * Queries the specified plugin's state using its get_state callback.
 *
 * @param plugin_name Plugin name (non-NULL, must be registered)
 * @param query_type Query type identifier (plugin-specific)
 * @param result Output parameter for query result (non-NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if plugin_name or result is NULL
 * @return ESP_ERR_NOT_FOUND if plugin is not registered
 * @return ESP_ERR_INVALID_STATE if plugin doesn't have get_state callback
 * @return Error code from plugin's get_state callback
 */
esp_err_t plugin_query_state(const char *plugin_name, uint32_t query_type, void *result);

/**
 * @brief Execute plugin operation
 *
 * Executes an operation on the specified plugin using its execute_operation callback.
 *
 * @param plugin_name Plugin name (non-NULL, must be registered)
 * @param operation_type Operation type identifier (plugin-specific)
 * @param params Operation parameters (may be NULL if operation requires no parameters)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if plugin_name is NULL
 * @return ESP_ERR_NOT_FOUND if plugin is not registered
 * @return ESP_ERR_INVALID_STATE if plugin doesn't have execute_operation callback
 * @return Error code from plugin's execute_operation callback
 */
esp_err_t plugin_execute_operation(const char *plugin_name, uint32_t operation_type, void *params);

/**
 * @brief Get plugin helper result
 *
 * Gets a helper function result from the specified plugin using its get_helper callback.
 *
 * @param plugin_name Plugin name (non-NULL, must be registered)
 * @param helper_type Helper type identifier (plugin-specific)
 * @param params Helper parameters (may be NULL if helper requires no parameters)
 * @param result Output parameter for helper result (non-NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if plugin_name or result is NULL
 * @return ESP_ERR_NOT_FOUND if plugin is not registered
 * @return ESP_ERR_INVALID_STATE if plugin doesn't have get_helper callback
 * @return Error code from plugin's get_helper callback
 */
esp_err_t plugin_get_helper(const char *plugin_name, uint32_t helper_type, void *params, void *result);

#endif /* __PLUGIN_SYSTEM_H__ */
