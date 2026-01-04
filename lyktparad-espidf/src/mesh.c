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
#include "mesh_version.h"
#include "mesh_ota.h"
#include "plugins.h"

void app_main(void)
{
    /* Initialize LED strip first */
    ESP_ERROR_CHECK(mesh_light_init());
    ESP_LOGI(mesh_common_get_tag(), "[STARTUP] LED strip initialized");

    /* Initialize plugins (before mesh starts) */
    plugins_init();

    /* Initialize common mesh functionality (includes NVS initialization) */
    ESP_ERROR_CHECK(mesh_common_init());

    /* Initialize version management (after NVS is initialized) */
    esp_err_t version_err = mesh_version_init();
    if (version_err != ESP_OK) {
        ESP_LOGW("mesh_main", "[STARTUP] Version management initialization failed: %s",
                 esp_err_to_name(version_err));
    } else {
        const char *version = mesh_version_get_string();
        ESP_LOGI("mesh_main", "[STARTUP] Firmware version: %s", version);
    }

    /* Check for rollback before starting mesh (after NVS and version init) */
    esp_err_t rollback_err = mesh_ota_check_rollback();
    if (rollback_err != ESP_OK && rollback_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW("mesh_main", "[STARTUP] Rollback check failed: %s", esp_err_to_name(rollback_err));
        /* Continue boot even if rollback check fails (shouldn't happen, but be safe) */
    }
    /* Note: If rollback is needed, mesh_ota_check_rollback() will reboot and never return */

    /* Initialize root-specific functionality (safe to call on all nodes, checks internally) */
    ESP_ERROR_CHECK(mesh_root_init());

    /* Initialize child-specific functionality */
    ESP_ERROR_CHECK(mesh_child_init());
}
