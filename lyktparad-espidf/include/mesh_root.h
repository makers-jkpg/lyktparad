/* Mesh Root Node Module
 *
 * This module contains code specific to root nodes in the mesh network.
 * Root nodes are responsible for sending heartbeats, managing the routing table,
 * hosting the web server, and sending commands to child nodes.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_ROOT_H__
#define __MESH_ROOT_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Root Node Functions
 *******************************************************/

/* Initialize root node module */
esp_err_t mesh_root_init(void);

/* Send RGB color values to all mesh nodes (root node only) */
esp_err_t mesh_send_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Get current heartbeat counter value */
uint32_t mesh_get_heartbeat_count(void);

/* Get current RGB color (for root node web interface) */
void mesh_get_current_rgb(uint8_t *r, uint8_t *g, uint8_t *b, bool *is_set);

/* Get number of nodes in mesh (for root node web interface) */
int mesh_get_node_count(void);

/* Handle RGB command received via mesh network (for unified behavior) */
void mesh_root_handle_rgb_command(uint8_t r, uint8_t g, uint8_t b);

/* Handle mesh state response from child node (for root state adoption) */
void mesh_root_handle_state_response(const char *plugin_name, uint8_t counter);

/* Check if root setup is in progress (for command blocking) */
bool mesh_root_is_setup_in_progress(void);

/* Ensure at least one plugin is active, activating rgb_effect as default if needed */
esp_err_t mesh_root_ensure_active_plugin(void);

#endif /* __MESH_ROOT_H__ */
