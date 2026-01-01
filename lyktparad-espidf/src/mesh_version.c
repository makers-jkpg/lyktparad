/* Version Management Module Implementation
 *
 * This module provides firmware version management functionality for the mesh network.
 * It handles version storage in NVS, retrieval, and comparison for OTA update support.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "mesh_version.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

/*******************************************************
 *                Constants
 *******************************************************/

#define MESH_VERSION_NAMESPACE  "mesh"
#define MESH_VERSION_KEY        "fw_version"
#define MESH_VERSION_MAX_LEN   16

static const char *TAG = "mesh_version";

/*******************************************************
 *                Helper Functions
 *******************************************************/

/**
 * Parse version string into components
 * 
 * @param version_str Version string in format "MAJOR.MINOR.PATCH"
 * @param major Pointer to store major version
 * @param minor Pointer to store minor version
 * @param patch Pointer to store patch version
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on parse error
 */
static esp_err_t parse_version(const char *version_str, int *major, int *minor, int *patch)
{
    if (!version_str || !major || !minor || !patch) {
        return ESP_ERR_INVALID_ARG;
    }

    char *endptr;
    char *version_copy = strdup(version_str);
    if (!version_copy) {
        return ESP_ERR_NO_MEM;
    }

    /* Parse major version */
    *major = (int)strtol(version_copy, &endptr, 10);
    if (*endptr != '.' || *major < 0) {
        free(version_copy);
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse minor version */
    char *minor_start = endptr + 1;
    *minor = (int)strtol(minor_start, &endptr, 10);
    if (*endptr != '.' || *minor < 0) {
        free(version_copy);
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse patch version */
    char *patch_start = endptr + 1;
    *patch = (int)strtol(patch_start, &endptr, 10);
    if (*endptr != '\0' || *patch < 0) {
        free(version_copy);
        return ESP_ERR_INVALID_ARG;
    }

    free(version_copy);
    return ESP_OK;
}

/*******************************************************
 *                Function Definitions
 *******************************************************/

const char* mesh_version_get_string(void)
{
    static char version_str[MESH_VERSION_MAX_LEN];
    snprintf(version_str, sizeof(version_str), "%d.%d.%d",
             FIRMWARE_VERSION_MAJOR,
             FIRMWARE_VERSION_MINOR,
             FIRMWARE_VERSION_PATCH);
    return version_str;
}

esp_err_t mesh_version_init(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    /* Open NVS namespace */
    err = nvs_open(MESH_VERSION_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    /* Check if version exists in NVS */
    char stored_version[MESH_VERSION_MAX_LEN];
    size_t required_size = sizeof(stored_version);
    err = nvs_get_str(nvs_handle, MESH_VERSION_KEY, stored_version, &required_size);

    const char *current_version = mesh_version_get_string();

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Version not found, store current version */
        ESP_LOGI(TAG, "No version found in NVS, storing current version: %s", current_version);
        err = nvs_set_str(nvs_handle, MESH_VERSION_KEY, current_version);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store version: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit version: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
        ESP_LOGI(TAG, "Version initialized: %s", current_version);
    } else if (err == ESP_OK) {
        /* Version exists, compare with current */
        int compare_result;
        esp_err_t compare_err = mesh_version_compare(current_version, stored_version, &compare_result);
        if (compare_err == ESP_OK) {
            if (compare_result > 0) {
                /* Current version is newer, update stored version */
                ESP_LOGI(TAG, "Version updated: %s -> %s", stored_version, current_version);
                err = nvs_set_str(nvs_handle, MESH_VERSION_KEY, current_version);
                if (err == ESP_OK) {
                    err = nvs_commit(nvs_handle);
                }
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to update stored version: %s", esp_err_to_name(err));
                }
            } else if (compare_result == 0) {
                ESP_LOGI(TAG, "Version unchanged: %s", current_version);
            } else {
                ESP_LOGW(TAG, "Stored version (%s) is newer than current (%s), keeping stored version",
                         stored_version, current_version);
            }
        } else {
            ESP_LOGW(TAG, "Failed to compare versions, keeping stored version: %s", stored_version);
        }
    } else {
        /* Error reading from NVS */
        ESP_LOGE(TAG, "Failed to read version from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t mesh_version_get(char *version_str, size_t len)
{
    if (!version_str || len < MESH_VERSION_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    /* Open NVS namespace */
    err = nvs_open(MESH_VERSION_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    /* Read version from NVS */
    size_t required_size = len;
    err = nvs_get_str(nvs_handle, MESH_VERSION_KEY, version_str, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Version not found in NVS");
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read version from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t mesh_version_store(const char *version_str)
{
    if (!version_str) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate version format by attempting to parse it */
    int major, minor, patch;
    esp_err_t err = parse_version(version_str, &major, &minor, &patch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invalid version format: %s (expected MAJOR.MINOR.PATCH)", version_str);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;

    /* Open NVS namespace */
    err = nvs_open(MESH_VERSION_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    /* Store version in NVS */
    err = nvs_set_str(nvs_handle, MESH_VERSION_KEY, version_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store version: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    /* Commit changes */
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit version: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    ESP_LOGI(TAG, "Version stored: %s", version_str);
    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t mesh_version_compare(const char *v1, const char *v2, int *result)
{
    if (!v1 || !v2 || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    int major1, minor1, patch1;
    int major2, minor2, patch2;

    /* Parse both version strings */
    esp_err_t err = parse_version(v1, &major1, &minor1, &patch1);
    if (err != ESP_OK) {
        return err;
    }

    err = parse_version(v2, &major2, &minor2, &patch2);
    if (err != ESP_OK) {
        return err;
    }

    /* Compare versions numerically */
    if (major1 != major2) {
        *result = major1 - major2;
    } else if (minor1 != minor2) {
        *result = minor1 - minor2;
    } else {
        *result = patch1 - patch2;
    }

    return ESP_OK;
}

bool mesh_version_is_newer(const char *new_version, const char *current_version)
{
    if (!new_version || !current_version) {
        return false;
    }

    int result;
    esp_err_t err = mesh_version_compare(new_version, current_version, &result);
    if (err != ESP_OK) {
        /* On error, return false to be safe (don't allow update with invalid version) */
        return false;
    }

    return result > 0;
}
