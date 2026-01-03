/* Root Status LED Module Header
 *
 * This module provides a separate single-color status LED to indicate root node status.
 * The LED is ON when the node is the root node, and OFF when it is a non-root node.
 * The LED updates immediately on role changes.
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

#include "esp_err.h"
#include <stdbool.h>

/*******************************************************
 *                Function Definitions
 *******************************************************/

/**
 * @brief Initialize the root status LED GPIO
 *
 * This function initializes the GPIO pin for the root status LED and sets
 * the initial state based on the current mesh role.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t root_status_led_init(void);

/**
 * @brief Set the root status LED state
 *
 * This function sets the LED ON if the node is root, OFF if not root.
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

#endif /* __ROOT_STATUS_LED_H__ */
