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
#include "esp_mesh.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Plugin Command ID Range
 *******************************************************/

/* Plugin IDs are assigned in the range 0x0B-0xEE (228 plugins maximum)
 * Plugin IDs are assigned automatically during registration, starting from 0x0B
 * and incrementing sequentially for each registered plugin.
 * Registration order is deterministic (fixed in plugins.h), ensuring consistency
 * across all nodes with the same firmware version.
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
     * The plugin ID has already been validated and extracted by the plugin system.
     * This handler receives data starting with the command byte.
     *
     * @param data Pointer to command data (data[0] is command byte, e.g., PLUGIN_CMD_DATA)
     * @param len Length of command data in bytes (includes command byte)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*command_handler)(uint8_t *data, uint16_t len);

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
     * Called when PLUGIN_CMD_START (0x01) is received via plugin protocol.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_start)(void);

    /**
     * @brief Plugin PAUSE command callback (optional, may be NULL)
     *
     * Called when PLUGIN_CMD_PAUSE (0x02) is received via plugin protocol.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_pause)(void);

    /**
     * @brief Plugin RESET command callback (optional, may be NULL)
     *
     * Called when PLUGIN_CMD_RESET (0x03) is received via plugin protocol.
     *
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*on_reset)(void);

    /**
     * @brief Plugin BEAT command callback (optional, may be NULL)
     *
     * Called when PLUGIN_CMD_BEAT (0x05) is received via plugin protocol.
     *
     * @param data Command data (data[0] = pointer, data[1] = counter (0-255))
     * @param len Data length (expected 2: pointer + counter)
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
 * This function registers a plugin and assigns it a unique plugin ID
 * in the range 0x0B-0xEE. Plugin IDs are assigned sequentially starting
 * from 0x0B. Registration order is deterministic (fixed in plugins.h),
 * ensuring consistency across all nodes with the same firmware version.
 *
 * @param info Plugin information structure (must contain valid name and command_handler)
 * @param assigned_cmd_id Output parameter for assigned plugin ID (non-NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if info, info->name, or assigned_cmd_id is NULL, or if name is empty
 * @return ESP_ERR_INVALID_ARG if command_handler callback is NULL
 * @return ESP_ERR_INVALID_STATE if plugin with same name is already registered
 * @return ESP_ERR_NO_MEM if plugin registry is full or plugin ID range is exhausted
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
 * @brief Look up plugin by plugin ID
 *
 * @param plugin_id Plugin ID (must be in plugin range 0x0B-0xEE)
 * @return Pointer to plugin info if found, NULL if not found or plugin_id is outside range
 */
const plugin_info_t *plugin_get_by_id(uint8_t plugin_id);

/**
 * @brief Get plugin ID by name
 *
 * @param name Plugin name (non-NULL, must be registered)
 * @param plugin_id Output parameter for plugin ID (non-NULL)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if name or plugin_id is NULL
 * @return ESP_ERR_NOT_FOUND if plugin with name is not registered
 */
esp_err_t plugin_get_id_by_name(const char *name, uint8_t *plugin_id);

/**
 * @brief Handle command routing to plugin system
 *
 * This function routes plugin protocol commands to the appropriate plugin's
 * command_handler callback. The protocol format is:
 * [PLUGIN_ID:1] [CMD:1] [LENGTH:2?] [DATA:N]
 *
 * The plugin ID is extracted from the first byte of data, and the command
 * byte is extracted from the second byte. Commands are routed based on
 * plugin ID, making the protocol stateless and self-contained.
 *
 * @param data Pointer to command data (must start with plugin ID at data[0], must be non-NULL if len > 0)
 * @param len Length of command data in bytes (must be at least 2 for plugin ID + command byte)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if data is NULL when len > 0, or len < 2, or plugin ID is outside range (0x0B-0xEE)
 * @return ESP_ERR_NOT_FOUND if no plugin is registered for the given plugin ID
 * @return Error code from plugin's command_handler callback
 */
esp_err_t plugin_system_handle_command(uint8_t *data, uint16_t len);

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
 * @brief Send active plugin START command to a specific node
 *
 * This function sends the currently active plugin's START command to a specific
 * mesh node. This is used to synchronize newly joined nodes with the current
 * plugin state. If no plugin is active, the function returns successfully without
 * sending anything.
 *
 * @param node_addr Destination mesh node address (non-NULL)
 * @return ESP_OK on success (including when no plugin is active)
 * @return ESP_ERR_INVALID_ARG if node_addr is NULL
 * @return ESP_ERR_NOT_FOUND if active plugin is not found in registry
 * @return Error code from mesh_send_with_bridge() if send fails
 */
esp_err_t plugin_send_start_to_node(const mesh_addr_t *node_addr);

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
 * @brief Handle plugin control command routing
 *
 * This function routes plugin control commands (START, PAUSE, RESET, BEAT)
 * using the new plugin protocol format: [PLUGIN_ID:1] [CMD:1] [DATA:N]
 *
 * The plugin ID is extracted from data[0], and the command byte is extracted
 * from data[1]. When a START command is received, all other plugins are
 * automatically deactivated to ensure mutual exclusivity.
 *
 * @param data Command data (must start with plugin ID at data[0] and command byte at data[1], must be non-NULL)
 * @param len Data length (must be at least 2 for plugin ID + command byte)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if data is NULL, len < 2, or plugin ID/command byte is invalid
 * @return ESP_ERR_NOT_FOUND if no plugin is registered for the given plugin ID
 * @return ESP_ERR_INVALID_STATE if plugin doesn't have the required callback
 * @return Error code from plugin's callback
 */
esp_err_t plugin_system_handle_plugin_command(uint8_t *data, uint16_t len);

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
