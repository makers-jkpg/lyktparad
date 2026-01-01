/* OTA Module Header
 *
 * This module provides Over-The-Air (OTA) firmware download functionality for the mesh network.
 * It handles HTTP/HTTPS firmware downloads from remote servers and stores them in the inactive OTA partition.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_OTA_H__
#define __MESH_OTA_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

/*******************************************************
 *                Function Definitions
 *******************************************************/

/**
 * Initialize OTA system
 *
 * This function should be called once during system initialization, after NVS is initialized.
 * It verifies that OTA partitions (app0 and app1) exist and are valid.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_init(void);

/**
 * Download firmware from URL
 *
 * Downloads firmware from the specified URL (HTTP or HTTPS) and stores it in the inactive OTA partition.
 * The download is performed asynchronously and progress can be monitored via mesh_ota_get_download_progress().
 *
 * @param url URL of the firmware binary (must start with http:// or https://)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_download_firmware(const char *url);

/**
 * Get inactive partition for updates
 *
 * Returns a pointer to the inactive OTA partition (app1 if running from app0, or vice versa).
 * This partition is where new firmware will be written during downloads.
 *
 * @return Pointer to update partition, or NULL if not initialized or error
 */
const esp_partition_t* mesh_ota_get_update_partition(void);

/**
 * Check if download is in progress
 *
 * @return true if download is currently in progress, false otherwise
 */
bool mesh_ota_is_downloading(void);

/**
 * Get download progress
 *
 * Returns the current download progress as a float between 0.0 and 1.0.
 * 0.0 means download hasn't started, 1.0 means download is complete.
 *
 * @return Progress value (0.0-1.0), or 0.0 if not downloading
 */
float mesh_ota_get_download_progress(void);

/**
 * Cancel ongoing download
 *
 * Cancels the current download operation if one is in progress.
 * This function is idempotent and safe to call even if no download is active.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_cancel_download(void);

#endif /* __MESH_OTA_H__ */
