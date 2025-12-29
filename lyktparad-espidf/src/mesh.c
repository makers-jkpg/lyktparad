/* Mesh Main Entry Point
 *
 * This file orchestrates the mesh network initialization by calling
 * the common, root, and child modules as appropriate.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "esp_log.h"
#include "mesh_common.h"
#include "mesh_root.h"
#include "mesh_child.h"
#include "light_neopixel.h"

void app_main(void)
{
    /* Initialize LED strip first */
    ESP_ERROR_CHECK(mesh_light_init());
    ESP_LOGI(mesh_common_get_tag(), "[STARTUP] LED strip initialized");

    /* Initialize common mesh functionality */
    ESP_ERROR_CHECK(mesh_common_init());

    /* Initialize root-specific functionality (safe to call on all nodes, checks internally) */
    ESP_ERROR_CHECK(mesh_root_init());

    /* Initialize child-specific functionality */
    ESP_ERROR_CHECK(mesh_child_init());
}
