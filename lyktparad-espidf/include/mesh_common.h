/* Mesh Common Module
 *
 * This module contains code shared by both root and child nodes in the mesh network.
 * It provides common initialization, event handling framework, and shared state management.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_COMMON_H__
#define __MESH_COMMON_H__

#include "esp_err.h"
#include "esp_mesh.h"
#include <stdint.h>
#include <stdbool.h>

/*******************************************************
 *                Constants
 *******************************************************/
#define RX_SIZE          (1500)
#define TX_SIZE          (1460)

/*******************************************************
 *                Common State Accessors
 *******************************************************/

/* Get current mesh layer */
int mesh_common_get_layer(void);

/* Set mesh layer */
void mesh_common_set_layer(int layer);

/* Get mesh connection status */
bool mesh_common_is_connected(void);
bool mesh_common_is_router_connected(void);

/* Set mesh connection status */
void mesh_common_set_connected(bool connected);

/* Get parent address */
void mesh_common_get_parent_addr(mesh_addr_t *parent_addr);

/* Set parent address */
void mesh_common_set_parent_addr(const mesh_addr_t *parent_addr);

/* Get network interface handle */
esp_netif_t* mesh_common_get_netif_sta(void);

/* Get transmit buffer pointer */
uint8_t* mesh_common_get_tx_buf(void);

/* Get receive buffer pointer */
uint8_t* mesh_common_get_rx_buf(void);

/* Get running status */
bool mesh_common_is_running(void);

/* Set running status */
void mesh_common_set_running(bool running);

/* Get mesh tag for logging */
const char* mesh_common_get_tag(void);

/* Get mesh ID */
const uint8_t* mesh_common_get_mesh_id(void);

/*******************************************************
 *                Event Handler Callbacks
 *******************************************************/

/* Event handler callback function type */
typedef void (*mesh_event_callback_t)(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data);

/* IP event handler callback function type */
typedef void (*ip_event_callback_t)(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data);

/* Register root-specific event handler callback */
void mesh_common_register_root_event_callback(mesh_event_callback_t callback);

/* Register child-specific event handler callback */
void mesh_common_register_child_event_callback(mesh_event_callback_t callback);

/* Register root-specific IP event handler callback */
void mesh_common_register_root_ip_callback(ip_event_callback_t callback);

/*******************************************************
 *                Initialization Functions
 *******************************************************/

/* Initialize common mesh module */
esp_err_t mesh_common_init(void);

/* Start P2P communication tasks */
esp_err_t mesh_common_comm_p2p_start(void);

/* Main mesh event handler (registered with ESP-IDF) */
void mesh_common_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);

/* IP event handler (registered with ESP-IDF) */
void mesh_common_ip_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data);

/*******************************************************
 *                Mesh Send Bridge Wrapper
 *******************************************************/

/* Wrapper function to send mesh data and optionally forward to UDP bridge */
esp_err_t mesh_send_with_bridge(const mesh_addr_t *to, const mesh_data_t *data,
                                 int flag, const mesh_opt_t opt[], int opt_count);

#endif /* __MESH_COMMON_H__ */
