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
#include "esp_mesh.h"

/*******************************************************
 *                Constants
 *******************************************************/
#define MESH_OTA_BLOCK_SIZE           1024    /* Block size in bytes (1KB) */
#define MESH_OTA_BLOCK_HEADER_SIZE    10      /* Block header size in bytes */
#define MESH_OTA_MAX_BLOCKS           2048    /* Maximum number of blocks (supports up to 2MB firmware) */
#define MESH_OTA_ACK_TIMEOUT_MS       5000    /* ACK timeout in milliseconds */
#define MESH_OTA_MAX_RETRIES_PER_BLOCK 3     /* Maximum retries per block per node */
#define MESH_OTA_PROGRESS_LOG_INTERVAL 10     /* Log progress every 10% */

/*******************************************************
 *                Data Structures
 *******************************************************/

/**
 * OTA_START message structure
 * Sent from root to all nodes to initiate distribution
 */
typedef struct {
    uint8_t cmd;              /* MESH_CMD_OTA_START (0xF1) */
    uint16_t total_blocks;    /* Total number of blocks (big-endian) */
    uint32_t firmware_size;   /* Total firmware size in bytes (big-endian) */
    char version[16];         /* Firmware version string (null-terminated) */
} __attribute__((packed)) mesh_ota_start_t;

/**
 * Block header structure
 * Prefix for each OTA_BLOCK message
 */
typedef struct {
    uint8_t cmd;              /* MESH_CMD_OTA_BLOCK (0xF2) */
    uint16_t block_number;    /* Block number (0-based, big-endian) */
    uint16_t total_blocks;    /* Total number of blocks (big-endian) */
    uint16_t block_size;      /* Size of this block in bytes (big-endian) */
    uint32_t checksum;        /* Block checksum (CRC32, big-endian) */
} __attribute__((packed)) mesh_ota_block_header_t;

/**
 * OTA_ACK message structure
 * Sent from leaf nodes to root to acknowledge block receipt
 */
typedef struct {
    uint8_t cmd;              /* MESH_CMD_OTA_ACK (0xF3) */
    uint16_t block_number;    /* Block number being acknowledged (big-endian) */
    uint8_t status;           /* Status: 0=OK, 1=ERROR */
} __attribute__((packed)) mesh_ota_ack_t;

/**
 * OTA_STATUS message structure
 * Query distribution status
 */
typedef struct {
    uint8_t cmd;              /* MESH_CMD_OTA_STATUS (0xF4) */
    uint8_t request_type;     /* 0=query progress, 1=query failed blocks */
} __attribute__((packed)) mesh_ota_status_t;

/**
 * OTA_PREPARE_REBOOT message structure
 * Sent from root to all nodes to prepare for coordinated reboot
 */
typedef struct {
    uint8_t cmd;              /* MESH_CMD_OTA_PREPARE_REBOOT (0xF5) */
    uint16_t timeout_seconds; /* Timeout for preparation in seconds (big-endian) */
    char version[16];         /* Firmware version to verify (null-terminated) */
} __attribute__((packed)) mesh_ota_prepare_reboot_t;

/**
 * OTA_REBOOT message structure
 * Sent from root to all nodes to trigger coordinated reboot
 */
typedef struct {
    uint8_t cmd;              /* MESH_CMD_OTA_REBOOT (0xF6) */
    uint16_t delay_ms;        /* Delay before reboot in milliseconds (big-endian) */
} __attribute__((packed)) mesh_ota_reboot_t;

/**
 * Progress callback function type
 */
typedef void (*mesh_ota_progress_callback_t)(float overall_progress, int nodes_complete, int nodes_total, int blocks_sent, int blocks_total);

/**
 * Distribution status structure
 */
typedef struct {
    bool distributing;        /* Whether distribution is active */
    uint16_t total_blocks;    /* Total number of blocks */
    uint16_t current_block;   /* Current block being sent */
    float overall_progress;   /* Overall progress (0.0-1.0) */
    int nodes_total;          /* Total number of target nodes */
    int nodes_complete;       /* Number of nodes that completed */
    int nodes_failed;         /* Number of nodes that failed */
} mesh_ota_distribution_status_t;

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

/**
 * Start firmware distribution to all mesh nodes
 *
 * Distributes the firmware from the inactive OTA partition to all leaf nodes in the mesh network.
 * This function can only be called on the root node.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_distribute_firmware(void);

/**
 * Get distribution status
 *
 * Returns the current distribution status including progress information.
 *
 * @param status Pointer to status structure to fill
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_get_distribution_status(mesh_ota_distribution_status_t *status);

/**
 * Get distribution progress
 *
 * Returns the overall distribution progress as a float between 0.0 and 1.0.
 *
 * @return Progress value (0.0-1.0), or 0.0 if not distributing
 */
float mesh_ota_get_distribution_progress(void);

/**
 * Cancel ongoing distribution
 *
 * Cancels the current distribution operation if one is in progress.
 * This function is idempotent and safe to call even if no distribution is active.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_cancel_distribution(void);

/**
 * Register progress callback
 *
 * Registers a callback function that will be called during distribution to report progress.
 * The callback will be invoked from the distribution task context.
 *
 * @param callback Callback function pointer, or NULL to unregister
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_register_progress_callback(mesh_ota_progress_callback_t callback);

/**
 * Handle OTA message from mesh (root node only)
 *
 * Processes incoming OTA messages from leaf nodes (OTA_REQUEST, OTA_ACK, OTA_STATUS).
 * This function should be called from the mesh receive handler.
 *
 * @param from Source node address
 * @param data Message data
 * @param len Message length
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_handle_mesh_message(mesh_addr_t *from, uint8_t *data, uint16_t len);

/**
 * Handle OTA message from mesh (leaf node only)
 *
 * Processes incoming OTA messages from root node (OTA_START, OTA_BLOCK, PREPARE_REBOOT, REBOOT).
 * This function should be called from the mesh receive handler.
 *
 * @param from Source node address
 * @param data Message data
 * @param len Message length
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_handle_leaf_message(mesh_addr_t *from, uint8_t *data, uint16_t len);

/**
 * Request firmware update from root node
 *
 * Sends an OTA_REQUEST message to the root node to initiate firmware distribution.
 * This function can only be called on leaf nodes.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_request_update(void);

/**
 * Initiate coordinated reboot of all mesh nodes
 *
 * Coordinates a reboot of all nodes in the mesh network. First sends PREPARE_REBOOT
 * to all nodes and waits for acknowledgments. If all nodes are ready, sends REBOOT
 * command to trigger simultaneous reboot.
 *
 * This function can only be called on the root node after distribution is complete.
 *
 * @param timeout_seconds Timeout for waiting for PREPARE_REBOOT ACKs
 * @param reboot_delay_ms Delay before reboot in milliseconds
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mesh_ota_initiate_coordinated_reboot(uint16_t timeout_seconds, uint16_t reboot_delay_ms);

/**
 * Cleanup OTA reception on mesh disconnection
 *
 * This function should be called when the mesh connection is lost to clean up
 * any ongoing OTA reception state. It's safe to call even if no OTA is in progress.
 *
 * @return ESP_OK on success
 */
esp_err_t mesh_ota_cleanup_on_disconnect(void);

#endif /* __MESH_OTA_H__ */
