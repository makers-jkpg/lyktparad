/* Version Management Module Header
 *
 * This module provides firmware version management functionality for the mesh network.
 * It handles version storage, retrieval, and comparison for OTA update support.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#ifndef __MESH_VERSION_H__
#define __MESH_VERSION_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*******************************************************
 *                Version Definitions
 *******************************************************/

/* Firmware version components (compile-time defines) */
#define FIRMWARE_VERSION_MAJOR  1
#define FIRMWARE_VERSION_MINOR  0
#define FIRMWARE_VERSION_PATCH  0

/*******************************************************
 *                Function Definitions
 *******************************************************/

/**
 * Initialize version management system
 * 
 * This function should be called once during system initialization, after NVS is initialized.
 * It checks if a version is stored in NVS, and if not, stores the current version.
 * If a version exists, it compares it with the current version and logs the result.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_version_init(void);

/**
 * Get current firmware version string from NVS
 * 
 * Retrieves the stored version string from NVS. If no version is stored,
 * returns an error.
 * 
 * @param version_str Buffer to store version string (must be at least 16 bytes)
 * @param len Size of version_str buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if version not in NVS, other error codes on failure
 */
esp_err_t mesh_version_get(char *version_str, size_t len);

/**
 * Store firmware version string in NVS
 * 
 * Stores a version string in NVS. The version string must be in format "MAJOR.MINOR.PATCH"
 * (e.g., "1.0.0"). The function validates the format before storing.
 * 
 * @param version_str Version string to store (format: "MAJOR.MINOR.PATCH")
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if version format is invalid, other error codes on failure
 */
esp_err_t mesh_version_store(const char *version_str);

/**
 * Compare two version strings
 * 
 * Compares two version strings numerically (not lexicographically).
 * For example, "1.0.9" < "1.1.0" (not "1.0.9" > "1.1.0").
 * 
 * @param v1 First version string (format: "MAJOR.MINOR.PATCH")
 * @param v2 Second version string (format: "MAJOR.MINOR.PATCH")
 * @param result Pointer to store comparison result:
 *               - negative if v1 < v2
 *               - zero if v1 == v2
 *               - positive if v1 > v2
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if version format is invalid
 */
esp_err_t mesh_version_compare(const char *v1, const char *v2, int *result);

/**
 * Check if a version is newer than the current version
 * 
 * Compares new_version with current_version and returns true if new_version is newer.
 * Returns false if versions are equal, new_version is older, or on error.
 * 
 * @param new_version Version string to check (format: "MAJOR.MINOR.PATCH")
 * @param current_version Current version string (format: "MAJOR.MINOR.PATCH")
 * @return true if new_version > current_version, false otherwise
 */
bool mesh_version_is_newer(const char *new_version, const char *current_version);

/**
 * Get current firmware version as string from compile-time defines
 * 
 * Returns the version string built from compile-time defines.
 * This is the "source of truth" for the current firmware version.
 * The returned string is static and should not be freed.
 * 
 * @return Version string in format "MAJOR.MINOR.PATCH" (e.g., "1.0.0")
 */
const char* mesh_version_get_string(void);

#endif /* __MESH_VERSION_H__ */
