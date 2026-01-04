/* Root Status LED Module Header
 *
 * This module provides a separate single-color status LED to indicate root node status.
 * The LED blinks in different patterns based on router connection status and mesh node count.
 * The LED is optional and disabled by default (no code compiled if ROOT_STATUS_LED_GPIO is undefined).
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __ROOT_STATUS_LED_H__
#define __ROOT_STATUS_LED_H__

#ifdef ROOT_STATUS_LED_GPIO

#include "esp_err.h"
#include <stdbool.h>

/*******************************************************
 *                Pattern Definitions
 *******************************************************/

/**
 * @brief Blinking pattern types for root status LED
 */
typedef enum {
    ROOT_LED_PATTERN_STARTUP,           /* 250ms ON - 750ms OFF (router not connected, no nodes) */
    ROOT_LED_PATTERN_ROUTER_ONLY,       /* 125ms ON - 125ms OFF - 125ms ON - 625ms OFF (router connected, no nodes) */
    ROOT_LED_PATTERN_NODES_ONLY,        /* 125ms ON - 375ms OFF - 125ms ON - 375ms OFF (no router, 1+ nodes) */
    ROOT_LED_PATTERN_ROUTER_AND_NODES,  /* 125ms ON - 125ms OFF × 4 (router connected, 1+ nodes) */
    ROOT_LED_PATTERN_OFF                /* LED OFF (child node) */
} root_led_pattern_t;

/*******************************************************
 *                Function Definitions
 *******************************************************/

/**
 * @brief Initialize the root status LED GPIO
 *
 * This function initializes the GPIO pin for the root status LED and starts
 * blinking with the STARTUP pattern if the node is root.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t root_status_led_init(void);

/**
 * @brief Set the root status LED state
 *
 * This function sets the LED pattern based on root status.
 * When setting to root, it calls root_status_led_update_status() to determine the pattern.
 * When setting to non-root, it stops blinking and turns the LED OFF.
 *
 * @param is_root True if node is root, false otherwise
 */
void root_status_led_set_root(bool is_root);

/**
 * @brief Update the root status LED based on current mesh role
 *
 * This function checks the current mesh role and updates the LED state accordingly.
 * It should be called when the mesh role might have changed.
 */
void root_status_led_update(void);

/**
 * @brief Update the root status LED pattern based on router connection and node count
 *
 * This function determines the appropriate blinking pattern based on:
 * - Whether the node is root
 * - Router connection status (mesh_common_is_router_connected())
 * - Node count (esp_mesh_get_routing_table_size() - 1)
 *
 * Pattern determination:
 * - Not root → OFF (stop blinking, LED OFF)
 * - Root, router connected, node_count == 0 → ROUTER_ONLY
 * - Root, router not connected, node_count > 0 → NODES_ONLY
 * - Root, router connected, node_count > 0 → ROUTER_AND_NODES
 * - Root, router not connected, node_count == 0 → STARTUP
 *
 * Should be called when:
 * - Router connection changes (IP_EVENT_STA_GOT_IP, IP_EVENT_STA_LOST_IP)
 * - Child nodes connect/disconnect (MESH_EVENT_CHILD_CONNECTED, MESH_EVENT_CHILD_DISCONNECTED)
 * - Routing table changes (MESH_EVENT_ROUTING_TABLE_ADD, MESH_EVENT_ROUTING_TABLE_REMOVE)
 */
void root_status_led_update_status(void);

#else /* ROOT_STATUS_LED_GPIO */

/* Root status LED is disabled - provide empty stub functions */

#include "esp_err.h"
#include <stdbool.h>

static inline esp_err_t root_status_led_init(void) { return ESP_OK; }
static inline void root_status_led_set_root(bool is_root) { (void)is_root; }
static inline void root_status_led_update(void) { }
static inline void root_status_led_update_status(void) { }

#endif /* ROOT_STATUS_LED_GPIO */

#endif /* __ROOT_STATUS_LED_H__ */
