/* Mesh GPIO Module
 *
 * This module provides GPIO-based root node forcing functionality.
 * Two GPIO pins can be used to manually select root or mesh node behavior at startup.
 *
 * GPIO Pin Configuration (configurable in mesh_device_config.h):
 * - Default: GPIO 5 (Pin A) - Force Root Node - When connected to GND, forces root node behavior
 * - Default: GPIO 4 (Pin B) - Force Mesh Node - When connected to GND, forces mesh node behavior
 * - Pins can be changed via MESH_GPIO_FORCE_ROOT and MESH_GPIO_FORCE_MESH in mesh_device_config.h
 *
 * Simplified Logic (only one pin should be tied to GND at a time for forcing behavior):
 * - GPIO 5 (Pin A) LOW (and Pin B HIGH): Force root node (uses esp_mesh_fix_root(true))
 * - GPIO 4 (Pin B) LOW (and Pin A HIGH): Force mesh node (uses esp_mesh_set_self_organized(true, false))
 * - Both HIGH (both floating): Normal root election enabled (uses esp_mesh_set_self_organized(true, true))
 * - Both LOW (conflict): Normal root election enabled (uses esp_mesh_set_self_organized(true, true))
 *
 * Both pins have internal pull-up resistors enabled, so they read HIGH when not connected to GND.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_GPIO_H__
#define __MESH_GPIO_H__

#include "esp_err.h"
#include <stdbool.h>
#include "mesh_device_config.h"

/*******************************************************
 *                GPIO Pin Definitions
 *******************************************************/

/* GPIO pins are configured in mesh_device_config.h
 * Default values: MESH_GPIO_FORCE_ROOT = 5, MESH_GPIO_FORCE_MESH = 4
 * These can be changed in mesh_device_config.h to use different pins
 */

/*******************************************************
 *                Function Definitions
 *******************************************************/

/* Initialize GPIO pins for root node forcing
 * Configures the GPIO pins defined in mesh_device_config.h (default: GPIO 5 and 4) as inputs with internal pull-up resistors enabled.
 * After configuration, waits 50ms for pull-up resistors to stabilize before allowing pin reads.
 *
 * @return ESP_OK on success, error code on failure
 * @note This function is idempotent and safe to call multiple times
 * @note GPIO initialization failures are logged but do not prevent mesh initialization
 * @note GPIO pins are configurable via MESH_GPIO_FORCE_ROOT and MESH_GPIO_FORCE_MESH in mesh_device_config.h
 */
esp_err_t mesh_gpio_init(void);

/* Check if GPIO module is initialized
 *
 * @return true if GPIO module is initialized, false otherwise
 */
bool mesh_gpio_is_initialized(void);

/* Read GPIO pins to determine if root node should be forced
 * Implements simplified logic: only one pin tied to GND at a time, other floating with pull-up
 *
 * @return true if root node should be forced, false otherwise
 * @note Returns false (mesh node) if GPIO not initialized (safe default)
 * @note Both pins have pull-up resistors, so they read HIGH when floating (not grounded)
 * @note GPIO must be initialized (mesh_gpio_init) before calling this function
 */
bool mesh_gpio_read_root_force(void);

#endif /* __MESH_GPIO_H__ */
