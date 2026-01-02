/* OTA Module Implementation
 *
 * This module implements Over-The-Air (OTA) firmware download functionality.
 * It supports both HTTP and HTTPS downloads and stores firmware in the inactive OTA partition.
 *
 * Copyright (c) 2025 the_louie
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "mesh_ota.h"
#include "mesh_version.h"
#include "mesh_common.h"
#include "mesh_commands.h"
#include "light_neopixel.h"
#include "config/mesh_config.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_mesh.h"
#include "esp_mac.h"
#include "nvs.h"
#include <string.h>
#include <strings.h>  /* for strncasecmp */

#ifndef CONFIG_MESH_ROUTE_TABLE_SIZE
#define CONFIG_MESH_ROUTE_TABLE_SIZE 50
#endif
#include <stdlib.h>   /* for malloc/free */
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include <sys/param.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* Ensure MACSTR and MAC2STR are defined */
#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

static const char *TAG = "mesh_ota";

/* Constants */
#define MESH_OTA_MAX_RETRIES 3
#define MESH_OTA_PROGRESS_LOG_INTERVAL 10  /* Log progress every 10% */
#define MESH_OTA_CHUNK_SIZE 1024           /* Download chunk size in bytes */
#define MESH_OTA_RETRY_DELAY_MS 1000       /* Delay between retries in milliseconds */

/* State management */
static bool s_ota_inited = false;
static bool s_ota_downloading = false;
static float s_ota_progress = 0.0f;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static const esp_partition_t *s_running_partition = NULL;
static esp_http_client_handle_t s_http_client = NULL;
static esp_https_ota_handle_t s_https_ota_handle = NULL;

/* Distribution state management */
static bool s_distributing = false;
static TaskHandle_t s_distribution_task = NULL;
static uint16_t s_total_blocks = 0;
static uint32_t s_firmware_size = 0;
static mesh_addr_t *s_node_list = NULL;
static int s_node_count = 0;
static uint8_t *s_node_block_bitmap = NULL;  /* Per-node bitmap of received blocks (byte array) */
static EventGroupHandle_t s_ack_event_group = NULL;
static uint32_t s_last_ack_block = UINT32_MAX;
static mesh_addr_t s_last_ack_node;
static mesh_ota_progress_callback_t s_progress_callback = NULL;
static int s_nodes_complete = 0;
static int s_nodes_failed = 0;

/* Leaf node OTA reception state management */
static bool s_leaf_receiving = false;
static esp_ota_handle_t s_leaf_ota_handle = 0;
static const esp_partition_t *s_leaf_update_partition = NULL;
static uint16_t s_leaf_total_blocks = 0;
static uint32_t s_leaf_firmware_size = 0;
static char s_leaf_version[16] = {0};
static uint8_t *s_leaf_block_bitmap = NULL;  /* Bitmap to track received blocks */
static size_t s_leaf_bytes_written = 0;
static bool s_leaf_firmware_complete = false;
static TickType_t s_leaf_last_block_time = 0;  /* Time of last block received */
#define MESH_OTA_LEAF_BLOCK_TIMEOUT_MS 30000  /* 30 seconds timeout for block reception */

/* Reboot coordination state (root node) */
static bool s_reboot_coordinating = false;
static EventGroupHandle_t s_reboot_prepare_event_group = NULL;
static int s_reboot_nodes_ready = 0;
static int s_reboot_nodes_total = 0;
static uint8_t *s_reboot_nodes_ready_bitmap = NULL;  /* Bitmap to track which nodes have sent ready ACK */

/*******************************************************
 *                Helper Functions
 *******************************************************/

/**
 * Check if URL is HTTPS
 */
static bool is_https_url(const char *url)
{
    if (url == NULL) {
        return false;
    }
    /* Case-insensitive check for https:// */
    return (strncasecmp(url, "https://", 8) == 0);
}

/**
 * Cleanup OTA download state
 */
static void cleanup_ota_download(void)
{
    /* Abort OTA operation if handle is valid */
    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    /* Close HTTPS OTA if active */
    if (s_https_ota_handle != NULL) {
        /* Try to abort first (for cancellation), then finish */
        #ifdef ESP_HTTPS_OTA_SUPPORT_ABORT
        esp_https_ota_abort(s_https_ota_handle);
        #endif
        esp_https_ota_finish(s_https_ota_handle);
        s_https_ota_handle = NULL;
    }

    /* Close HTTP client if active */
    if (s_http_client != NULL) {
        esp_http_client_close(s_http_client);
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
    }

    /* Reset state variables */
    s_ota_downloading = false;
    s_ota_progress = 0.0f;
}

/**
 * Check if error is retryable
 */
static bool is_retryable_error(esp_err_t err)
{
    /* Network errors are retryable */
    if (err == ESP_ERR_HTTP_CONNECT ||
        err == ESP_ERR_HTTP_FETCH_HEADER ||
        err == ESP_ERR_HTTP_EAGAIN) {
        return true;
    }
    /* HTTP 5xx errors are retryable (server errors) */
    if (err == ESP_ERR_HTTP_INVALID_TRANSPORT) {
        return true;
    }
    /* Timeout and connection errors are retryable */
    if (err == ESP_FAIL || err == ESP_ERR_TIMEOUT) {
        return true;
    }
    /* Client errors (4xx) and invalid arguments are NOT retryable */
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE) {
        return false;
    }
    /* OTA partition errors are NOT retryable */
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_SIZE) {
        return false;
    }
    return false;
}

/*******************************************************
 *                HTTPS Download Implementation
 *******************************************************/

static esp_err_t download_firmware_https(const char *url)
{
    esp_err_t err;
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = MESH_OTA_CHUNK_SIZE,
        .cert_pem = NULL,  /* Use default certificate validation */
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    ESP_LOGI(TAG, "Starting HTTPS OTA download from: %s", url);

    /* Get OTA partition for logging */
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             s_update_partition->subtype, s_update_partition->address);

    /* Begin HTTPS OTA (handles partition selection and writing automatically) */
    err = esp_https_ota_begin(&ota_config, &s_https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Download and write firmware */
    size_t total_bytes_read = 0;
    size_t image_len = 0;
    int last_logged_progress = -1;

    while (1) {
        err = esp_https_ota_perform(s_https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        /* Update progress using ESP-IDF API functions */
        total_bytes_read = esp_https_ota_get_image_len_read(s_https_ota_handle);
        /* Try to get total image length (may return 0 if unknown) */
        image_len = esp_https_ota_get_image_size(s_https_ota_handle);

        if (image_len > 0) {
            /* Calculate progress percentage */
            s_ota_progress = (float)total_bytes_read / (float)image_len;
            int progress_percent = (int)(s_ota_progress * 100);

            /* Log progress at intervals */
            if (progress_percent >= last_logged_progress + MESH_OTA_PROGRESS_LOG_INTERVAL) {
                ESP_LOGI(TAG, "Download progress: %d%% (%zu/%zu bytes)",
                         progress_percent, total_bytes_read, image_len);
                last_logged_progress = progress_percent;
            }
        } else {
            /* Unknown size, just log bytes read at intervals */
            if (total_bytes_read % (MESH_OTA_CHUNK_SIZE * 10) == 0) {
                ESP_LOGI(TAG, "Download progress: %zu bytes (size unknown)", total_bytes_read);
            }
            /* Set progress to indicate download is in progress (but unknown percentage) */
            s_ota_progress = 0.5f;  /* Indicate progress but unknown total */
        }

        /* Small delay to prevent tight loop */
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (err == ESP_OK) {
        /* Get final bytes read count */
        total_bytes_read = esp_https_ota_get_image_len_read(s_https_ota_handle);
        image_len = esp_https_ota_get_image_size(s_https_ota_handle);

        /* Verify size if available */
        if (image_len > 0 && total_bytes_read != image_len) {
            ESP_LOGE(TAG, "Size mismatch: read %zu bytes, expected %zu bytes",
                     total_bytes_read, image_len);
            esp_https_ota_finish(s_https_ota_handle);
            s_https_ota_handle = NULL;
            return ESP_ERR_INVALID_SIZE;
        }

        ESP_LOGI(TAG, "HTTPS OTA download completed successfully: %zu bytes", total_bytes_read);
        s_ota_progress = 1.0f;
    } else {
        ESP_LOGE(TAG, "HTTPS OTA perform failed: %s", esp_err_to_name(err));
    }

    /* Finish HTTPS OTA */
    esp_https_ota_finish(s_https_ota_handle);
    s_https_ota_handle = NULL;

    /* Check for downgrade after HTTPS OTA completion */
    if (err == ESP_OK && s_update_partition != NULL) {
        esp_err_t downgrade_err = mesh_ota_check_downgrade(s_update_partition);
        if (downgrade_err == ESP_ERR_INVALID_VERSION) {
            /* Downgrade detected - note: partition is already finalized by esp_https_ota_finish() */
            /* Distribution will also check and reject the downgrade */
            ESP_LOGE(TAG, "Downgrade detected after HTTPS download completion");
            return ESP_ERR_INVALID_VERSION;
        } else if (downgrade_err != ESP_OK) {
            /* Other error during downgrade check */
            ESP_LOGE(TAG, "Downgrade check failed: %s", esp_err_to_name(downgrade_err));
            return downgrade_err;
        }
        /* Downgrade check passed - version is same or newer */
    }

    return err;
}

/*******************************************************
 *                HTTP Download Implementation
 *******************************************************/

static esp_err_t download_firmware_http(const char *url)
{
    esp_err_t err;
    size_t total_bytes_read = 0;
    size_t content_length = 0;
    int last_logged_progress = -1;
    char *buffer = NULL;

    ESP_LOGI(TAG, "Starting HTTP OTA download from: %s", url);

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = MESH_OTA_CHUNK_SIZE,
    };

    s_http_client = esp_http_client_init(&config);
    if (s_http_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_ERR_NO_MEM;
    }

    /* Set method to GET */
    esp_http_client_set_method(s_http_client, HTTP_METHOD_GET);

    /* Open connection */
    err = esp_http_client_open(s_http_client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP client open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        return err;
    }

    /* Fetch headers to get status code and content-length */
    err = esp_http_client_fetch_headers(s_http_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP fetch headers failed: %s", esp_err_to_name(err));
        esp_http_client_close(s_http_client);
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        return err;
    }

    /* Check HTTP status code */
    int status_code = esp_http_client_get_status_code(s_http_client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed with status code: %d", status_code);
        esp_http_client_close(s_http_client);
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        /* Return specific error codes for retry logic */
        if (status_code >= 400 && status_code < 500) {
            /* 4xx errors are client errors, not retryable */
            return ESP_ERR_INVALID_ARG;
        } else if (status_code >= 500) {
            /* 5xx errors are server errors, may be retryable */
            return ESP_ERR_HTTP_INVALID_TRANSPORT;
        } else {
            /* Other status codes */
            return ESP_ERR_HTTP_INVALID_TRANSPORT;
        }
    }

    /* Get content length */
    content_length = esp_http_client_get_content_length(s_http_client);
    ESP_LOGI(TAG, "Content-Length: %zu bytes", content_length);

    /* Get OTA partition */
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        esp_http_client_close(s_http_client);
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    /* Begin OTA write */
    err = esp_ota_begin(s_update_partition, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(s_http_client);
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             s_update_partition->subtype, s_update_partition->address);

    /* Allocate buffer for reading */
    buffer = (char *)malloc(MESH_OTA_CHUNK_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
        esp_http_client_close(s_http_client);
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Read and write data in chunks */
    while (1) {
        int data_read = esp_http_client_read(s_http_client, buffer, MESH_OTA_CHUNK_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "HTTP read error: %d", data_read);
            err = ESP_FAIL;
            break;
        }
        if (data_read == 0) {
            /* End of data */
            err = ESP_OK;  /* Successfully reached end of data */
            break;
        }

        /* Write to OTA partition */
        err = esp_ota_write(s_ota_handle, (const void *)buffer, data_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            break;
        }

        total_bytes_read += data_read;

        /* Update progress */
        if (content_length > 0) {
            s_ota_progress = (float)total_bytes_read / (float)content_length;
            int progress_percent = (int)(s_ota_progress * 100);

            /* Log progress at intervals */
            if (progress_percent >= last_logged_progress + MESH_OTA_PROGRESS_LOG_INTERVAL) {
                ESP_LOGI(TAG, "Download progress: %d%% (%zu/%zu bytes)",
                         progress_percent, total_bytes_read, content_length);
                last_logged_progress = progress_percent;
            }
        } else {
            /* Unknown size, just log bytes read */
            if (total_bytes_read % (MESH_OTA_CHUNK_SIZE * 10) == 0) {
                ESP_LOGI(TAG, "Download progress: %zu bytes (size unknown)", total_bytes_read);
            }
            /* Set progress to indicate download is in progress (but unknown percentage) */
            s_ota_progress = 0.5f;  /* Indicate progress but unknown total */
        }
    }

    free(buffer);

    if (err == ESP_OK) {
        /* Verify size if content-length was provided */
        if (content_length > 0 && total_bytes_read != content_length) {
            ESP_LOGE(TAG, "Size mismatch: read %zu bytes, expected %zu bytes",
                     total_bytes_read, content_length);
            esp_ota_abort(s_ota_handle);
            s_ota_handle = 0;
            esp_http_client_close(s_http_client);
            esp_http_client_cleanup(s_http_client);
            s_http_client = NULL;
            return ESP_ERR_INVALID_SIZE;
        }

        /* Finalize OTA write */
        err = esp_ota_end(s_ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
            esp_ota_abort(s_ota_handle);
            s_ota_handle = 0;
        } else {
            ESP_LOGI(TAG, "HTTP OTA download completed successfully: %zu bytes", total_bytes_read);
            
            /* Check for downgrade before marking download as complete */
            if (s_update_partition != NULL) {
                esp_err_t downgrade_err = mesh_ota_check_downgrade(s_update_partition);
                if (downgrade_err == ESP_ERR_INVALID_VERSION) {
                    /* Downgrade detected - abort and clean up */
                    ESP_LOGE(TAG, "Downgrade detected after download, aborting OTA");
                    esp_ota_abort(s_ota_handle);
                    s_ota_handle = 0;
                    return ESP_ERR_INVALID_VERSION;
                } else if (downgrade_err != ESP_OK) {
                    /* Other error during downgrade check (e.g., cannot read version) */
                    ESP_LOGE(TAG, "Downgrade check failed: %s", esp_err_to_name(downgrade_err));
                    esp_ota_abort(s_ota_handle);
                    s_ota_handle = 0;
                    return downgrade_err;
                }
                /* Downgrade check passed - version is same or newer */
            }
            
            s_ota_progress = 1.0f;
        }
    } else {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    /* Clean up HTTP client */
    esp_http_client_close(s_http_client);
    esp_http_client_cleanup(s_http_client);
    s_http_client = NULL;

    return err;
}

/*******************************************************
 *                Public API Implementation
 *******************************************************/

esp_err_t mesh_ota_init(void)
{
    if (s_ota_inited) {
        return ESP_OK;
    }

    /* Get running partition */
    s_running_partition = esp_ota_get_running_partition();
    if (s_running_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return ESP_ERR_NOT_FOUND;
    }

    /* Get update partition */
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "OTA initialized - Running partition: subtype %d at 0x%x, size %d bytes",
             s_running_partition->subtype, s_running_partition->address, s_running_partition->size);
    ESP_LOGI(TAG, "OTA initialized - Update partition: subtype %d at 0x%x, size %d bytes",
             s_update_partition->subtype, s_update_partition->address, s_update_partition->size);

    s_ota_inited = true;
    return ESP_OK;
}

const esp_partition_t* mesh_ota_get_update_partition(void)
{
    if (!s_ota_inited) {
        ESP_LOGW(TAG, "OTA not initialized");
        return NULL;
    }
    return s_update_partition;
}

/*******************************************************
 *                Downgrade Prevention
 *******************************************************/

/* Note: ESP_ERR_INVALID_VERSION is now defined by ESP-IDF (0x10A).
 * We use ESP-IDF's definition directly. Callers should check for ESP_ERR_INVALID_VERSION
 * specifically to detect downgrades.
 */

esp_err_t mesh_ota_check_downgrade(const esp_partition_t *partition)
{
    if (partition == NULL) {
        ESP_LOGE(TAG, "Downgrade check failed: partition is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Get partition description to extract version */
    esp_app_desc_t app_desc;
    esp_err_t err = esp_ota_get_partition_description(partition, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get partition description for downgrade check: %s", esp_err_to_name(err));
        /* Fail-safe: reject if we cannot read version */
        return ESP_ERR_INVALID_ARG;
    }

    /* Get current firmware version (compile-time version is source of truth) */
    const char *current_version = mesh_version_get_string();
    if (current_version == NULL) {
        ESP_LOGE(TAG, "Failed to get current firmware version");
        /* Fail-safe: reject if we cannot get current version */
        return ESP_ERR_INVALID_ARG;
    }

    /* Compare versions */
    int comparison_result;
    err = mesh_version_compare(app_desc.version, current_version, &comparison_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to compare versions (partition: %s, current: %s): %s",
                 app_desc.version, current_version, esp_err_to_name(err));
        /* Fail-safe: reject if comparison fails */
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if downgrade (result < 0 means partition version is older) */
    if (comparison_result < 0) {
        ESP_LOGE(TAG, "Downgrade prevented: Current version %s, attempted version %s",
                 current_version, app_desc.version);
        return ESP_ERR_INVALID_VERSION;  /* Downgrade detected */
    }

    /* Same or newer version - OK to proceed */
    if (comparison_result == 0) {
        ESP_LOGI(TAG, "Version check: Same version %s (re-installation allowed)", current_version);
    } else {
        ESP_LOGI(TAG, "Version check: Upgrade from %s to %s", current_version, app_desc.version);
    }

    return ESP_OK;
}

bool mesh_ota_is_downloading(void)
{
    return s_ota_downloading;
}

float mesh_ota_get_download_progress(void)
{
    if (!s_ota_downloading) {
        return 0.0f;
    }
    return s_ota_progress;
}

esp_err_t mesh_ota_cancel_download(void)
{
    if (!s_ota_downloading) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Cancelling OTA download");

    cleanup_ota_download();

    ESP_LOGI(TAG, "OTA download cancelled");
    return ESP_OK;
}

esp_err_t mesh_ota_download_firmware(const char *url)
{
    esp_err_t err;
    int retry_count = 0;

    /* Validate input */
    if (url == NULL) {
        ESP_LOGE(TAG, "URL is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if initialized */
    if (!s_ota_inited) {
        ESP_LOGE(TAG, "OTA not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if already downloading */
    if (s_ota_downloading) {
        ESP_LOGE(TAG, "Download already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    /* Basic URL validation */
    if (strlen(url) < 8 || (strncasecmp(url, "http://", 7) != 0 && strncasecmp(url, "https://", 8) != 0)) {
        ESP_LOGE(TAG, "Invalid URL format: %s", url);
        return ESP_ERR_INVALID_ARG;
    }

    /* Set download state */
    s_ota_downloading = true;
    s_ota_progress = 0.0f;

    /* Retry loop: 1 initial attempt + up to MESH_OTA_MAX_RETRIES retries */
    while (retry_count <= MESH_OTA_MAX_RETRIES) {
        if (retry_count > 0) {
            ESP_LOGI(TAG, "Retry attempt %d/%d", retry_count, MESH_OTA_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(MESH_OTA_RETRY_DELAY_MS));
        }

        /* Determine protocol and download */
        if (is_https_url(url)) {
            err = download_firmware_https(url);
        } else {
            err = download_firmware_http(url);
        }

        if (err == ESP_OK) {
            /* Success */
            s_ota_downloading = false;
            ESP_LOGI(TAG, "Firmware download completed successfully");
            return ESP_OK;
        }

        /* Check if we should retry */
        if (!is_retryable_error(err)) {
            /* Non-retryable error - stop immediately */
            break;
        }

        if (retry_count >= MESH_OTA_MAX_RETRIES) {
            /* Retries exhausted */
            break;
        }

        /* Prepare for retry */
        retry_count++;
        cleanup_ota_download();
        /* Reset state for retry */
        s_ota_downloading = true;
        s_ota_progress = 0.0f;
    }

    /* Download failed */
    ESP_LOGE(TAG, "Firmware download failed: %s (after %d retries)", esp_err_to_name(err), retry_count);
    cleanup_ota_download();
    s_ota_downloading = false;

    return err;
}

/*******************************************************
 *                Distribution Implementation
 *******************************************************/

/**
 * Simple CRC32 implementation
 */
static uint32_t calculate_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        crc ^= byte;
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}

/**
 * Read firmware block from partition
 */
static esp_err_t read_firmware_block(uint16_t block_num, uint8_t *buffer, size_t buffer_size, size_t *bytes_read)
{
    const esp_partition_t *update_part = mesh_ota_get_update_partition();
    if (update_part == NULL) {
        ESP_LOGE(TAG, "Update partition not available");
        return ESP_ERR_NOT_FOUND;
    }

    size_t offset = (size_t)block_num * MESH_OTA_BLOCK_SIZE;
    size_t remaining = s_firmware_size - offset;
    size_t read_size = MIN(buffer_size, remaining);
    read_size = MIN(read_size, MESH_OTA_BLOCK_SIZE);

    esp_err_t err = esp_partition_read(update_part, offset, buffer, read_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read block %d: %s", block_num, esp_err_to_name(err));
        return err;
    }

    *bytes_read = read_size;
    return ESP_OK;
}

/**
 * Get list of target nodes (excluding root)
 */
static esp_err_t get_target_node_list(mesh_addr_t **node_list, int *node_count)
{
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;

    esp_mesh_get_routing_table((mesh_addr_t *)route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Get root MAC address (we are root, so get our own MAC) */
    mesh_addr_t root_addr;
    uint8_t mac[6];
    esp_err_t mac_err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (mac_err == ESP_OK) {
        memcpy(root_addr.addr, mac, 6);
    } else {
        /* Fallback: use first entry in routing table */
        if (route_table_size > 0) {
            root_addr = route_table[0];
        } else {
            /* No nodes in routing table */
            *node_list = NULL;
            *node_count = 0;
            return ESP_OK;
        }
    }

    /* Count child nodes (excluding root) */
    int child_count = 0;
    for (int i = 0; i < route_table_size; i++) {
        /* Compare MAC addresses to exclude root */
        bool is_root = true;
        for (int j = 0; j < 6; j++) {
            if (route_table[i].addr[j] != root_addr.addr[j]) {
                is_root = false;
                break;
            }
        }
        if (!is_root) {
            child_count++;
        }
    }

    if (child_count == 0) {
        *node_list = NULL;
        *node_count = 0;
        return ESP_OK;
    }

    /* Allocate node list */
    mesh_addr_t *list = (mesh_addr_t *)malloc(child_count * sizeof(mesh_addr_t));
    if (list == NULL) {
        ESP_LOGE(TAG, "Failed to allocate node list");
        return ESP_ERR_NO_MEM;
    }

    /* Copy child nodes */
    int idx = 0;
    for (int i = 0; i < route_table_size; i++) {
        bool is_root = true;
        for (int j = 0; j < 6; j++) {
            if (route_table[i].addr[j] != root_addr.addr[j]) {
                is_root = false;
                break;
            }
        }
        if (!is_root) {
            memcpy(&list[idx], &route_table[i], sizeof(mesh_addr_t));
            idx++;
        }
    }

    *node_list = list;
    *node_count = child_count;
    return ESP_OK;
}

/**
 * Check if node has block
 */
static bool node_has_block(int node_idx, uint16_t block_num)
{
    if (node_idx < 0 || node_idx >= s_node_count || s_node_block_bitmap == NULL) {
        return false;
    }
    int bit_idx = node_idx * s_total_blocks + block_num;
    int byte_idx = bit_idx / 8;
    int bit_offset = bit_idx % 8;
    return (s_node_block_bitmap[byte_idx] & (1 << bit_offset)) != 0;
}

/**
 * Mark node as having block
 */
static void mark_node_has_block(int node_idx, uint16_t block_num)
{
    if (node_idx < 0 || node_idx >= s_node_count || s_node_block_bitmap == NULL) {
        return;
    }
    int bit_idx = node_idx * s_total_blocks + block_num;
    int byte_idx = bit_idx / 8;
    int bit_offset = bit_idx % 8;
    s_node_block_bitmap[byte_idx] |= (1 << bit_offset);
}

/**
 * Send OTA_START command to all nodes
 */
static esp_err_t send_ota_start(void)
{
    mesh_ota_start_t start_msg;
    start_msg.cmd = MESH_CMD_OTA_START;
    start_msg.total_blocks = __builtin_bswap16(s_total_blocks);  /* Big-endian */
    start_msg.firmware_size = __builtin_bswap32(s_firmware_size);  /* Big-endian */

    const char *version = mesh_version_get_string();
    strncpy((char *)start_msg.version, version, sizeof(start_msg.version) - 1);
    start_msg.version[sizeof(start_msg.version) - 1] = '\0';

    mesh_data_t data;
    data.data = (uint8_t *)&start_msg;
    data.size = sizeof(start_msg);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    esp_err_t err = ESP_OK;
    for (int i = 0; i < s_node_count; i++) {
        esp_err_t send_err = esp_mesh_send(&s_node_list[i], &data, MESH_DATA_P2P, NULL, 0);
        if (send_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send OTA_START to node %d: %s", i, esp_err_to_name(send_err));
            err = send_err;  /* Continue sending but remember error */
        }
    }

    return err;
}

/**
 * Send OTA_BLOCK to a single node
 */
static esp_err_t send_ota_block_to_node(const mesh_addr_t *node, uint16_t block_num, uint8_t *block_data, size_t block_size)
{
    mesh_ota_block_header_t header;
    header.cmd = MESH_CMD_OTA_BLOCK;
    header.block_number = __builtin_bswap16(block_num);
    header.total_blocks = __builtin_bswap16(s_total_blocks);
    header.block_size = __builtin_bswap16((uint16_t)block_size);
    header.checksum = __builtin_bswap32(calculate_crc32(block_data, block_size));

    uint8_t *tx_buf = mesh_common_get_tx_buf();
    if (tx_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Copy header */
    memcpy(tx_buf, &header, sizeof(header));
    /* Copy block data */
    memcpy(tx_buf + sizeof(header), block_data, block_size);

    mesh_data_t data;
    data.data = tx_buf;
    data.size = sizeof(header) + block_size;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    return esp_mesh_send(node, &data, MESH_DATA_P2P, NULL, 0);
}

/**
 * Distribution task
 */
static void distribution_task(void *arg)
{
    ESP_LOGI(TAG, "Distribution task started");

    /* Allocate block buffer */
    uint8_t *block_buffer = (uint8_t *)malloc(MESH_OTA_BLOCK_SIZE);
    if (block_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate block buffer");
        s_distributing = false;
        s_distribution_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Send OTA_START to all nodes */
    esp_err_t err = send_ota_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Some OTA_START sends failed, continuing");
    }
    vTaskDelay(pdMS_TO_TICKS(100));  /* Give nodes time to prepare */

    int last_logged_progress = -1;

    /* Distribute each block */
    for (uint16_t block_num = 0; block_num < s_total_blocks; block_num++) {
        if (!s_distributing) {
            ESP_LOGI(TAG, "Distribution cancelled");
            break;
        }

        /* Read block from partition */
        size_t bytes_read = 0;
        err = read_firmware_block(block_num, block_buffer, MESH_OTA_BLOCK_SIZE, &bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read block %d: %s", block_num, esp_err_to_name(err));
            s_nodes_failed = s_node_count;  /* Mark all as failed */
            break;
        }

        /* Send block to all nodes that don't have it yet */
        bool block_complete = false;
        int retry_count = 0;

        while (!block_complete && retry_count <= MESH_OTA_MAX_RETRIES_PER_BLOCK) {
            /* Clear ACK event group */
            xEventGroupClearBits(s_ack_event_group, 0xFFFFFFFF);
            s_last_ack_block = UINT32_MAX;

            /* Send block to nodes that need it */
            int nodes_sent = 0;
            for (int node_idx = 0; node_idx < s_node_count; node_idx++) {
                if (node_has_block(node_idx, block_num)) {
                    continue;  /* Node already has this block */
                }

                err = send_ota_block_to_node(&s_node_list[node_idx], block_num, block_buffer, bytes_read);
                if (err == ESP_OK) {
                    nodes_sent++;
                } else {
                    ESP_LOGW(TAG, "Failed to send block %d to node %d: %s", block_num, node_idx, esp_err_to_name(err));
                }
            }

            if (nodes_sent == 0) {
                /* All nodes already have this block */
                block_complete = true;
                break;
            }

            /* Wait for ACKs with timeout */
            EventBits_t bits = xEventGroupWaitBits(
                s_ack_event_group,
                0xFFFFFFFF,  /* Wait for any bit */
                pdTRUE,      /* Clear bits on exit */
                pdFALSE,     /* Wait for any bit */
                pdMS_TO_TICKS(MESH_OTA_ACK_TIMEOUT_MS)
            );

            if (bits != 0) {
                /* Process ACKs received during wait */
                /* ACKs are handled in mesh_ota_handle_mesh_message */
                /* Check again if all nodes have the block */
                int nodes_with_block = 0;
                for (int node_idx = 0; node_idx < s_node_count; node_idx++) {
                    if (node_has_block(node_idx, block_num)) {
                        nodes_with_block++;
                    }
                }
                if (nodes_with_block == s_node_count) {
                    block_complete = true;
                }
            }

            if (!block_complete) {
                retry_count++;
                if (retry_count <= MESH_OTA_MAX_RETRIES_PER_BLOCK) {
                    ESP_LOGW(TAG, "Block %d: retry %d/%d", block_num, retry_count, MESH_OTA_MAX_RETRIES_PER_BLOCK);
                    /* Small delay before retry to avoid flooding mesh network */
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
        }

        if (!block_complete) {
            ESP_LOGW(TAG, "Block %d: some nodes failed after %d retries", block_num, retry_count);
        }

        /* Update progress */
        int blocks_sent = block_num + 1;
        int total_blocks_received = 0;
        for (int node_idx = 0; node_idx < s_node_count; node_idx++) {
            int node_blocks = 0;
            for (uint16_t b = 0; b <= block_num; b++) {
                if (node_has_block(node_idx, b)) {
                    node_blocks++;
                }
            }
            total_blocks_received += node_blocks;
        }
        float avg_progress = (float)total_blocks_received / (float)(s_node_count * s_total_blocks);
        s_nodes_complete = 0;
        for (int node_idx = 0; node_idx < s_node_count; node_idx++) {
            bool node_complete = true;
            for (uint16_t b = 0; b < s_total_blocks; b++) {
                if (!node_has_block(node_idx, b)) {
                    node_complete = false;
                    break;
                }
            }
            if (node_complete) {
                s_nodes_complete++;
            }
        }
        s_nodes_failed = s_node_count - s_nodes_complete;

        int progress_percent = (int)(avg_progress * 100);
        if (progress_percent >= last_logged_progress + MESH_OTA_PROGRESS_LOG_INTERVAL) {
            ESP_LOGI(TAG, "Distribution progress: %d%% (block %d/%d, nodes complete: %d/%d)",
                     progress_percent, block_num + 1, s_total_blocks, s_nodes_complete, s_node_count);
            last_logged_progress = progress_percent;
        }

        /* Call progress callback */
        if (s_progress_callback != NULL) {
            s_progress_callback(avg_progress, s_nodes_complete, s_node_count, blocks_sent, s_total_blocks);
        }
    }

    /* Distribution complete */
    ESP_LOGI(TAG, "Distribution complete: %d/%d nodes completed, %d failed",
             s_nodes_complete, s_node_count, s_nodes_failed);

    free(block_buffer);
    s_distributing = false;
    s_distribution_task = NULL;
    vTaskDelete(NULL);
}

/**
 * Cleanup distribution state
 */
static void cleanup_distribution(void)
{
    if (s_node_list != NULL) {
        free(s_node_list);
        s_node_list = NULL;
    }
    if (s_node_block_bitmap != NULL) {
        free(s_node_block_bitmap);
        s_node_block_bitmap = NULL;
    }
    if (s_ack_event_group != NULL) {
        vEventGroupDelete(s_ack_event_group);
        s_ack_event_group = NULL;
    }
    s_node_count = 0;
    s_total_blocks = 0;
    s_firmware_size = 0;
    s_nodes_complete = 0;
    s_nodes_failed = 0;
    s_distribution_task = NULL;  /* Task sets this to NULL on exit, but ensure it's cleared */

    /* Cleanup reboot coordination state */
    if (s_reboot_prepare_event_group != NULL) {
        vEventGroupDelete(s_reboot_prepare_event_group);
        s_reboot_prepare_event_group = NULL;
    }
    if (s_reboot_nodes_ready_bitmap != NULL) {
        free(s_reboot_nodes_ready_bitmap);
        s_reboot_nodes_ready_bitmap = NULL;
    }
    s_reboot_coordinating = false;
    s_reboot_nodes_ready = 0;
    s_reboot_nodes_total = 0;
}

esp_err_t mesh_ota_distribute_firmware(void)
{
    esp_err_t err;

    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Only root node can distribute firmware");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_distributing) {
        ESP_LOGE(TAG, "Distribution already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    /* Verify firmware is available */
    const esp_partition_t *update_part = mesh_ota_get_update_partition();
    if (update_part == NULL) {
        ESP_LOGE(TAG, "No update partition available");
        return ESP_ERR_NOT_FOUND;
    }

    /* Check for downgrade before starting distribution */
    esp_err_t downgrade_err = mesh_ota_check_downgrade(update_part);
    if (downgrade_err == ESP_ERR_INVALID_VERSION) {
        ESP_LOGE(TAG, "Downgrade detected, distribution rejected");
        return ESP_ERR_INVALID_VERSION;
    } else if (downgrade_err != ESP_OK) {
        ESP_LOGE(TAG, "Downgrade check failed: %s", esp_err_to_name(downgrade_err));
        return downgrade_err;
    }
    /* Downgrade check passed - version is same or newer */

    /* Get firmware size from partition */
    /* For distribution, we use the full partition size as firmware may have been written to it */
    /* The actual firmware image size could be determined from app description, but since */
    /* the partition was already validated during download, using partition size is safe */
    s_firmware_size = update_part->size;

    if (s_firmware_size == 0) {
        ESP_LOGE(TAG, "Invalid firmware size: %zu", s_firmware_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Calculate number of blocks */
    s_total_blocks = (s_firmware_size + MESH_OTA_BLOCK_SIZE - 1) / MESH_OTA_BLOCK_SIZE;
    if (s_total_blocks > MESH_OTA_MAX_BLOCKS) {
        ESP_LOGE(TAG, "Firmware too large: %d blocks (max %d)", s_total_blocks, MESH_OTA_MAX_BLOCKS);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Starting distribution: %zu bytes, %d blocks", s_firmware_size, s_total_blocks);

    /* Get target node list */
    err = get_target_node_list(&s_node_list, &s_node_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get node list: %s", esp_err_to_name(err));
        return err;
    }

    if (s_node_count == 0) {
        ESP_LOGW(TAG, "No target nodes available");
        cleanup_distribution();
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Target nodes: %d", s_node_count);

    /* Allocate node block bitmap */
    int bitmap_size = (s_node_count * s_total_blocks + 7) / 8;  /* Round up to bytes */
    s_node_block_bitmap = (uint8_t *)malloc(bitmap_size);
    if (s_node_block_bitmap == NULL) {
        ESP_LOGE(TAG, "Failed to allocate block bitmap");
        cleanup_distribution();
        return ESP_ERR_NO_MEM;
    }
    memset(s_node_block_bitmap, 0, bitmap_size);

    /* Create ACK event group */
    s_ack_event_group = xEventGroupCreate();
    if (s_ack_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        cleanup_distribution();
        return ESP_ERR_NO_MEM;
    }

    /* Start distribution task */
    s_distributing = true;
    s_nodes_complete = 0;
    s_nodes_failed = 0;
    xTaskCreate(distribution_task, "ota_distribute", 8192, NULL, 5, &s_distribution_task);
    if (s_distribution_task == NULL) {
        ESP_LOGE(TAG, "Failed to create distribution task");
        cleanup_distribution();
        s_distributing = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Distribution started");
    return ESP_OK;
}

esp_err_t mesh_ota_get_distribution_status(mesh_ota_distribution_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    status->distributing = s_distributing;
    status->total_blocks = s_total_blocks;
    status->nodes_total = s_node_count;
    status->nodes_complete = s_nodes_complete;
    status->nodes_failed = s_nodes_failed;

    if (s_distributing && s_total_blocks > 0) {
        /* Calculate current block and progress */
        int blocks_received = 0;
        for (int node_idx = 0; node_idx < s_node_count; node_idx++) {
            for (uint16_t b = 0; b < s_total_blocks; b++) {
                if (node_has_block(node_idx, b)) {
                    blocks_received++;
                }
            }
        }
        status->overall_progress = (float)blocks_received / (float)(s_node_count * s_total_blocks);
        status->current_block = (uint16_t)(status->overall_progress * s_total_blocks);
    } else {
        status->overall_progress = 0.0f;
        status->current_block = 0;
    }

    return ESP_OK;
}

float mesh_ota_get_distribution_progress(void)
{
    if (!s_distributing || s_total_blocks == 0 || s_node_count == 0) {
        return 0.0f;
    }

    int blocks_received = 0;
    for (int node_idx = 0; node_idx < s_node_count; node_idx++) {
        for (uint16_t b = 0; b < s_total_blocks; b++) {
            if (node_has_block(node_idx, b)) {
                blocks_received++;
            }
        }
    }

    return (float)blocks_received / (float)(s_node_count * s_total_blocks);
}

esp_err_t mesh_ota_cancel_distribution(void)
{
    if (!s_distributing) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Cancelling distribution");
    s_distributing = false;

    /* Wait for task to finish (with timeout) */
    if (s_distribution_task != NULL) {
        if (xTaskGetHandle("ota_distribute") != NULL) {
            /* Task still exists, wait a bit for it to finish */
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    cleanup_distribution();
    ESP_LOGI(TAG, "Distribution cancelled");
    return ESP_OK;
}

esp_err_t mesh_ota_register_progress_callback(mesh_ota_progress_callback_t callback)
{
    s_progress_callback = callback;
    return ESP_OK;
}

esp_err_t mesh_ota_handle_mesh_message(mesh_addr_t *from, uint8_t *data, uint16_t len)
{
    if (from == NULL || data == NULL || len < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!esp_mesh_is_root()) {
        /* Only root handles OTA messages */
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd = data[0];

    if (cmd == MESH_CMD_OTA_REQUEST) {
        /* Leaf node requesting update */
        ESP_LOGI(TAG, "OTA request received from "MACSTR, MAC2STR(from->addr));

        /* Check if distribution is already in progress */
        if (!s_distributing) {
            /* Start distribution */
            esp_err_t err = mesh_ota_distribute_firmware();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start distribution: %s", esp_err_to_name(err));
            }
        }
        return ESP_OK;
    }

    if (cmd == MESH_CMD_OTA_ACK) {
        /* Check if this is a reboot ACK first */
        if (s_reboot_coordinating && s_node_list != NULL && s_node_count > 0) {
            /* PREPARE_REBOOT ACK handling during reboot coordination */
            if (len < sizeof(mesh_ota_ack_t)) {
                return ESP_OK;
            }
            mesh_ota_ack_t *ack = (mesh_ota_ack_t *)data;

            /* Find node index */
            int node_idx = -1;
            for (int i = 0; i < s_node_count; i++) {
                bool match = true;
                for (int j = 0; j < 6; j++) {
                    if (s_node_list[i].addr[j] != from->addr[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    node_idx = i;
                    break;
                }
            }

            if (node_idx >= 0) {
                /* Check if we've already received ACK from this node */
                bool already_acked = false;
                if (s_reboot_nodes_ready_bitmap != NULL) {
                    int byte_idx = node_idx / 8;
                    int bit_offset = node_idx % 8;
                    if ((s_reboot_nodes_ready_bitmap[byte_idx] & (1 << bit_offset)) != 0) {
                        already_acked = true;
                    }
                }

                if (ack->status == 0) {
                    if (!already_acked) {
                        /* Node is ready - mark as ACKed and increment counter */
                        if (s_reboot_nodes_ready_bitmap != NULL) {
                            int byte_idx = node_idx / 8;
                            int bit_offset = node_idx % 8;
                            s_reboot_nodes_ready_bitmap[byte_idx] |= (1 << bit_offset);
                        }
                        s_reboot_nodes_ready++;
                        ESP_LOGI(TAG, "Node %d ready for reboot (%d/%d)",
                                 node_idx, s_reboot_nodes_ready, s_reboot_nodes_total);
                        if (s_reboot_prepare_event_group != NULL) {
                            xEventGroupSetBits(s_reboot_prepare_event_group, 1);
                        }
                    } else {
                        ESP_LOGD(TAG, "Node %d already acknowledged, ignoring duplicate ACK", node_idx);
                    }
                } else {
                    ESP_LOGW(TAG, "Node %d not ready for reboot", node_idx);
                }
            }
            return ESP_OK;
        }

        /* Block acknowledgment (during distribution) */
        if (len < sizeof(mesh_ota_ack_t)) {
            ESP_LOGW(TAG, "Invalid OTA_ACK message size: %d", len);
            return ESP_ERR_INVALID_SIZE;
        }

        if (!s_distributing || s_ack_event_group == NULL || s_node_list == NULL || s_node_count == 0) {
            return ESP_OK;  /* Ignore ACK if not distributing or state not initialized */
        }

        mesh_ota_ack_t *ack = (mesh_ota_ack_t *)data;
        uint16_t block_num = __builtin_bswap16(ack->block_number);

        if (ack->status == 0) {
            /* Find node index */
            int node_idx = -1;
            for (int i = 0; i < s_node_count; i++) {
                bool match = true;
                for (int j = 0; j < 6; j++) {
                    if (s_node_list[i].addr[j] != from->addr[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    node_idx = i;
                    break;
                }
            }

            if (node_idx >= 0 && block_num < s_total_blocks) {
                mark_node_has_block(node_idx, block_num);
                ESP_LOGD(TAG, "ACK received: node %d, block %d", node_idx, block_num);

                /* Signal ACK received */
                s_last_ack_block = block_num;
                memcpy(&s_last_ack_node, from, sizeof(mesh_addr_t));
                xEventGroupSetBits(s_ack_event_group, 1);
            } else if (node_idx < 0) {
                ESP_LOGW(TAG, "ACK received from unknown node "MACSTR, MAC2STR(from->addr));
            } else {
                ESP_LOGW(TAG, "ACK received for invalid block %d (max %d)", block_num, s_total_blocks - 1);
            }
        }
        return ESP_OK;
    }

    if (cmd == MESH_CMD_OTA_STATUS) {
        /* Status query - could respond with current progress */
        /* For now, just log it */
        ESP_LOGI(TAG, "OTA status query received from "MACSTR, MAC2STR(from->addr));
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

/*******************************************************
 *                Leaf Node OTA Reception Implementation
 *******************************************************/

/**
 * Cleanup leaf node OTA reception state
 */
static void cleanup_leaf_ota_reception(void)
{
    if (s_leaf_ota_handle != 0) {
        esp_ota_abort(s_leaf_ota_handle);
        s_leaf_ota_handle = 0;
    }
    if (s_leaf_block_bitmap != NULL) {
        free(s_leaf_block_bitmap);
        s_leaf_block_bitmap = NULL;
    }
    s_leaf_update_partition = NULL;
    s_leaf_total_blocks = 0;
    s_leaf_firmware_size = 0;
    s_leaf_bytes_written = 0;
    s_leaf_firmware_complete = false;
    s_leaf_last_block_time = 0;
    memset(s_leaf_version, 0, sizeof(s_leaf_version));
    s_leaf_receiving = false;
}

/**
 * Check for timeout during block reception
 */
static void check_leaf_reception_timeout(void)
{
    if (!s_leaf_receiving || s_leaf_firmware_complete) {
        return;
    }

    if (s_leaf_last_block_time == 0) {
        /* No blocks received yet, reset timeout when first block arrives */
        return;
    }

    TickType_t current_time = xTaskGetTickCount();
    TickType_t elapsed = current_time - s_leaf_last_block_time;

    if (elapsed > pdMS_TO_TICKS(MESH_OTA_LEAF_BLOCK_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "Block reception timeout (%d ms), aborting OTA", MESH_OTA_LEAF_BLOCK_TIMEOUT_MS);
        cleanup_leaf_ota_reception();
    }
}

/**
 * Check if all blocks have been received
 */
static bool leaf_all_blocks_received(void)
{
    if (s_leaf_block_bitmap == NULL || s_leaf_total_blocks == 0) {
        return false;
    }

    for (uint16_t i = 0; i < s_leaf_total_blocks; i++) {
        int byte_idx = i / 8;
        int bit_offset = i % 8;
        if ((s_leaf_block_bitmap[byte_idx] & (1 << bit_offset)) == 0) {
            return false;
        }
    }
    return true;
}

/**
 * Mark block as received
 */
static void leaf_mark_block_received(uint16_t block_num)
{
    if (s_leaf_block_bitmap == NULL || block_num >= s_leaf_total_blocks) {
        return;
    }
    int byte_idx = block_num / 8;
    int bit_offset = block_num % 8;
    s_leaf_block_bitmap[byte_idx] |= (1 << bit_offset);
}

/**
 * Check if block was already received
 */
static bool leaf_block_received(uint16_t block_num)
{
    if (s_leaf_block_bitmap == NULL || block_num >= s_leaf_total_blocks) {
        return false;
    }
    int byte_idx = block_num / 8;
    int bit_offset = block_num % 8;
    return (s_leaf_block_bitmap[byte_idx] & (1 << bit_offset)) != 0;
}

/**
 * Send OTA_ACK to root node
 */
static esp_err_t send_ota_ack_to_root(uint16_t block_num, uint8_t status)
{
    mesh_ota_ack_t ack;
    ack.cmd = MESH_CMD_OTA_ACK;
    ack.block_number = __builtin_bswap16(block_num);
    ack.status = status;

    mesh_data_t data;
    data.data = (uint8_t *)&ack;
    data.size = sizeof(ack);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Get root node address - try to get parent first */
    mesh_addr_t root_addr;
    esp_err_t err = esp_mesh_get_parent_bssid(&root_addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not get parent address, using broadcast");
        /* Use broadcast address */
        memset(root_addr.addr, 0xFF, 6);
    }

    err = esp_mesh_send(&root_addr, &data, MESH_DATA_P2P, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send ACK for block %d: %s", block_num, esp_err_to_name(err));
    }
    return err;
}

/**
 * Handle OTA_START message (leaf node)
 */
static esp_err_t handle_ota_start_leaf(const mesh_ota_start_t *start_msg)
{
    if (s_leaf_receiving) {
        ESP_LOGW(TAG, "OTA reception already in progress, aborting previous");
        cleanup_leaf_ota_reception();
    }

    /* Convert from big-endian */
    uint16_t total_blocks = __builtin_bswap16(start_msg->total_blocks);
    uint32_t firmware_size = __builtin_bswap32(start_msg->firmware_size);

    ESP_LOGI(TAG, "OTA_START received: %d blocks, %zu bytes, version: %s",
             total_blocks, firmware_size, start_msg->version);

    /* Get inactive OTA partition */
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (update_part == NULL) {
        ESP_LOGE(TAG, "No update partition available");
        return ESP_ERR_NOT_FOUND;
    }

    /* Initialize OTA operation */
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_part, firmware_size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin OTA: %s", esp_err_to_name(err));
        return err;
    }

    /* Allocate block bitmap */
    int bitmap_size = (total_blocks + 7) / 8;  /* Round up to bytes */
    uint8_t *bitmap = (uint8_t *)malloc(bitmap_size);
    if (bitmap == NULL) {
        ESP_LOGE(TAG, "Failed to allocate block bitmap");
        esp_ota_abort(ota_handle);
        return ESP_ERR_NO_MEM;
    }
    memset(bitmap, 0, bitmap_size);

    /* Store state */
    s_leaf_receiving = true;
    s_leaf_ota_handle = ota_handle;
    s_leaf_update_partition = update_part;
    s_leaf_total_blocks = total_blocks;
    s_leaf_firmware_size = firmware_size;
    s_leaf_block_bitmap = bitmap;
    s_leaf_bytes_written = 0;
    s_leaf_firmware_complete = false;
    strncpy(s_leaf_version, start_msg->version, sizeof(s_leaf_version) - 1);
    s_leaf_version[sizeof(s_leaf_version) - 1] = '\0';
    s_leaf_last_block_time = 0;  /* Reset timeout timer */

    ESP_LOGI(TAG, "OTA reception initialized, ready for blocks");
    return ESP_OK;
}

/**
 * Handle OTA_BLOCK message (leaf node)
 */
static esp_err_t handle_ota_block_leaf(const uint8_t *data, uint16_t len)
{
    if (!s_leaf_receiving) {
        ESP_LOGW(TAG, "Received OTA_BLOCK but not receiving update");
        return ESP_ERR_INVALID_STATE;
    }

    if (len < sizeof(mesh_ota_block_header_t)) {
        ESP_LOGE(TAG, "OTA_BLOCK message too small: %d bytes", len);
        return ESP_ERR_INVALID_SIZE;
    }

    const mesh_ota_block_header_t *header = (const mesh_ota_block_header_t *)data;
    uint16_t block_num = __builtin_bswap16(header->block_number);
    uint16_t total_blocks = __builtin_bswap16(header->total_blocks);
    uint16_t block_size = __builtin_bswap16(header->block_size);
    uint32_t checksum = __builtin_bswap32(header->checksum);

    /* Validate header */
    if (total_blocks != s_leaf_total_blocks) {
        ESP_LOGE(TAG, "Block %d: total_blocks mismatch (%d != %d)",
                 block_num, total_blocks, s_leaf_total_blocks);
        send_ota_ack_to_root(block_num, 1);  /* Error status */
        return ESP_ERR_INVALID_ARG;
    }

    if (block_num >= s_leaf_total_blocks) {
        ESP_LOGE(TAG, "Block number out of range: %d >= %d", block_num, s_leaf_total_blocks);
        send_ota_ack_to_root(block_num, 1);
        return ESP_ERR_INVALID_ARG;
    }

    /* Check for duplicate */
    if (leaf_block_received(block_num)) {
        ESP_LOGD(TAG, "Block %d already received, ignoring", block_num);
        send_ota_ack_to_root(block_num, 0);  /* Send ACK anyway */
        return ESP_OK;
    }

    /* Validate message size */
    size_t expected_size = sizeof(mesh_ota_block_header_t) + block_size;
    if (len < expected_size) {
        ESP_LOGE(TAG, "Block %d: message size mismatch (%d < %zu)", block_num, len, expected_size);
        send_ota_ack_to_root(block_num, 1);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Get block data (after header) */
    const uint8_t *block_data = data + sizeof(mesh_ota_block_header_t);

    /* Verify checksum */
    uint32_t calculated_crc = calculate_crc32(block_data, block_size);
    if (calculated_crc != checksum) {
        ESP_LOGE(TAG, "Block %d: checksum mismatch (calculated 0x%08X, expected 0x%08X)",
                 block_num, calculated_crc, checksum);
        send_ota_ack_to_root(block_num, 1);
        return ESP_ERR_INVALID_ARG;
    }

    /* Write block to partition */
    esp_err_t err = esp_ota_write(s_leaf_ota_handle, block_data, block_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write block %d: %s", block_num, esp_err_to_name(err));
        send_ota_ack_to_root(block_num, 1);

        /* Critical error - abort OTA */
        if (err == ESP_ERR_OTA_VALIDATE_FAILED || err == ESP_ERR_INVALID_SIZE) {
            ESP_LOGE(TAG, "Critical OTA error, aborting reception");
            cleanup_leaf_ota_reception();
        }
        return err;
    }

    /* Mark block as received */
    leaf_mark_block_received(block_num);
    s_leaf_bytes_written += block_size;
    s_leaf_last_block_time = xTaskGetTickCount();  /* Update last block time */

    /* Send ACK */
    send_ota_ack_to_root(block_num, 0);  /* Success status */

    ESP_LOGD(TAG, "Block %d/%d written successfully (%zu bytes total)",
             block_num + 1, s_leaf_total_blocks, s_leaf_bytes_written);

    /* Check if all blocks received */
    if (leaf_all_blocks_received()) {
        ESP_LOGI(TAG, "All blocks received, finalizing OTA partition");

        /* Finalize OTA partition */
        err = esp_ota_end(s_leaf_ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to finalize OTA partition: %s", esp_err_to_name(err));
            cleanup_leaf_ota_reception();
            return err;
        }

        /* Verify partition */
        esp_ota_img_states_t ota_state;
        err = esp_ota_get_state_partition(s_leaf_update_partition, &ota_state);
        if (err == ESP_OK && ota_state == ESP_OTA_IMG_VALID) {
            ESP_LOGI(TAG, "OTA partition validated successfully");
            s_leaf_firmware_complete = true;
        } else {
            ESP_LOGE(TAG, "OTA partition validation failed: %s", esp_err_to_name(err));
            cleanup_leaf_ota_reception();
            return err;
        }

        /* Cleanup OTA handle, but keep state for reboot coordination */
        s_leaf_ota_handle = 0;
        s_leaf_receiving = false;  /* No longer receiving blocks */
    } else {
        /* Check for timeout if not complete */
        check_leaf_reception_timeout();
    }

    return ESP_OK;
}

/**
 * Handle leaf node OTA messages
 */
esp_err_t mesh_ota_handle_leaf_message(mesh_addr_t *from, uint8_t *data, uint16_t len)
{
    if (from == NULL || data == NULL || len < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_mesh_is_root()) {
        /* Root node uses separate handler */
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd = data[0];

    if (cmd == MESH_CMD_OTA_START) {
        if (len < sizeof(mesh_ota_start_t)) {
            ESP_LOGW(TAG, "Invalid OTA_START message size: %d", len);
            return ESP_ERR_INVALID_SIZE;
        }
        return handle_ota_start_leaf((const mesh_ota_start_t *)data);
    }

    if (cmd == MESH_CMD_OTA_BLOCK) {
        return handle_ota_block_leaf(data, len);
    }

    if (cmd == MESH_CMD_OTA_PREPARE_REBOOT) {
        if (len < sizeof(mesh_ota_prepare_reboot_t)) {
            ESP_LOGW(TAG, "Invalid PREPARE_REBOOT message size: %d", len);
            return ESP_ERR_INVALID_SIZE;
        }
        const mesh_ota_prepare_reboot_t *msg = (const mesh_ota_prepare_reboot_t *)data;
        uint16_t timeout = __builtin_bswap16(msg->timeout_seconds);

        ESP_LOGI(TAG, "PREPARE_REBOOT received, timeout: %d seconds", timeout);

        /* Verify firmware is complete and valid */
        uint8_t ack_status = 1;  /* Error by default */
        if (s_leaf_firmware_complete && s_leaf_update_partition != NULL) {
            /* Verify partition state */
            esp_ota_img_states_t ota_state;
            esp_err_t err = esp_ota_get_state_partition(s_leaf_update_partition, &ota_state);
            if (err == ESP_OK && ota_state == ESP_OTA_IMG_VALID) {
                ack_status = 0;  /* Ready */
                ESP_LOGI(TAG, "Firmware ready for reboot");
            } else {
                ESP_LOGE(TAG, "Firmware partition not valid: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "Firmware not complete or partition not set");
        }

        /* Send ACK (reuse OTA_ACK structure) */
        mesh_ota_ack_t ack;
        ack.cmd = MESH_CMD_OTA_ACK;
        ack.block_number = 0;  /* Not used for reboot ACK */
        ack.status = ack_status;

        mesh_data_t data;
        data.data = (uint8_t *)&ack;
        data.size = sizeof(ack);
        data.proto = MESH_PROTO_BIN;
        data.tos = MESH_TOS_P2P;

        mesh_addr_t root_addr;
        esp_err_t err = esp_mesh_get_parent_bssid(&root_addr);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not get parent address, using broadcast");
            memset(root_addr.addr, 0xFF, 6);  /* Broadcast */
        }

        err = esp_mesh_send(&root_addr, &data, MESH_DATA_P2P, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send PREPARE_REBOOT ACK: %s", esp_err_to_name(err));
        }
        return ESP_OK;  /* Always return OK - ACK send failure is not critical */
    }

    if (cmd == MESH_CMD_OTA_REBOOT) {
        if (len < sizeof(mesh_ota_reboot_t)) {
            ESP_LOGW(TAG, "Invalid REBOOT message size: %d", len);
            return ESP_ERR_INVALID_SIZE;
        }
        const mesh_ota_reboot_t *msg = (const mesh_ota_reboot_t *)data;
        uint16_t delay_ms = __builtin_bswap16(msg->delay_ms);

        ESP_LOGI(TAG, "REBOOT command received, delay: %d ms", delay_ms);

        /* Verify firmware is ready */
        if (!s_leaf_firmware_complete || s_leaf_update_partition == NULL) {
            ESP_LOGE(TAG, "Cannot reboot: firmware not ready");
            return ESP_ERR_INVALID_STATE;
        }

        /* Check for downgrade before allowing reboot */
        esp_err_t downgrade_err = mesh_ota_check_downgrade(s_leaf_update_partition);
        if (downgrade_err == ESP_ERR_INVALID_VERSION) {
            ESP_LOGE(TAG, "Downgrade detected, reboot rejected");
            /* Send error ACK to root node indicating reboot rejected */
            mesh_ota_ack_t ack;
            ack.cmd = MESH_CMD_OTA_ACK;
            ack.block_number = 0;  /* Not used for reboot ACK */
            ack.status = 1;  /* Error status */
            
            mesh_data_t data;
            data.data = (uint8_t *)&ack;
            data.size = sizeof(ack);
            data.proto = MESH_PROTO_BIN;
            data.tos = MESH_TOS_P2P;
            
            mesh_addr_t root_addr;
            esp_err_t send_err = esp_mesh_get_parent_bssid(&root_addr);
            if (send_err != ESP_OK) {
                ESP_LOGW(TAG, "Could not get parent address for error ACK");
                memset(root_addr.addr, 0xFF, 6);  /* Broadcast */
            }
            
            esp_mesh_send(&root_addr, &data, MESH_DATA_P2P, NULL, 0);
            return ESP_ERR_INVALID_VERSION;
        } else if (downgrade_err != ESP_OK) {
            ESP_LOGE(TAG, "Downgrade check failed: %s", esp_err_to_name(downgrade_err));
            return downgrade_err;
        }
        /* Downgrade check passed - version is same or newer */

        /* Set rollback flag before reboot */
        /* This flag will be checked on next boot to rollback if mesh connection fails */
        esp_err_t rollback_flag_err = mesh_ota_set_rollback_flag();
        if (rollback_flag_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set rollback flag before reboot: %s", esp_err_to_name(rollback_flag_err));
            /* Continue with reboot even if flag setting fails (non-critical) */
        } else {
            ESP_LOGI(TAG, "Rollback flag set before reboot");
        }

        /* Set boot partition */
        esp_err_t err = esp_ota_set_boot_partition(s_leaf_update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
            return err;
        }

        /* Verify boot partition was set correctly */
        const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
        if (boot_partition == NULL) {
            ESP_LOGE(TAG, "Boot partition is NULL");
            return ESP_ERR_INVALID_STATE;
        }
        /* Compare partition addresses/offsets to verify it's the same partition */
        if (boot_partition->address != s_leaf_update_partition->address ||
            boot_partition->size != s_leaf_update_partition->size) {
            ESP_LOGE(TAG, "Boot partition verification failed (address/size mismatch)");
            return ESP_ERR_INVALID_STATE;
        }

        ESP_LOGI(TAG, "Boot partition set, rebooting in %d ms...", delay_ms);

        /* Delay if specified */
        if (delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }

        /* Reboot */
        esp_restart();

        /* Should never reach here */
        return ESP_OK;
    }

    return ESP_ERR_NOT_SUPPORTED;
}

/**
 * Request firmware update from root node
 */
esp_err_t mesh_ota_request_update(void)
{
    if (esp_mesh_is_root()) {
        ESP_LOGW(TAG, "Root node should use mesh_ota_distribute_firmware() instead");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_leaf_receiving) {
        ESP_LOGI(TAG, "Update already in progress");
        return ESP_OK;
    }

    /* Create OTA_REQUEST message */
    uint8_t request_byte = MESH_CMD_OTA_REQUEST;

    mesh_data_t data;
    data.data = &request_byte;
    data.size = 1;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Get root node address */
    mesh_addr_t root_addr;
    esp_err_t err = esp_mesh_get_parent_bssid(&root_addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not get parent address, using broadcast");
        memset(root_addr.addr, 0xFF, 6);  /* Broadcast */
    }

    err = esp_mesh_send(&root_addr, &data, MESH_DATA_P2P, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send OTA_REQUEST: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA update requested from root");
    return ESP_OK;
}

/**
 * Cleanup OTA reception on mesh disconnection
 */
esp_err_t mesh_ota_cleanup_on_disconnect(void)
{
    if (s_leaf_receiving) {
        ESP_LOGW(TAG, "Mesh disconnected during OTA reception, cleaning up");
        cleanup_leaf_ota_reception();
    }
    return ESP_OK;
}

/**
 * Initiate coordinated reboot of all mesh nodes
 */
esp_err_t mesh_ota_initiate_coordinated_reboot(uint16_t timeout_seconds, uint16_t reboot_delay_ms)
{
    if (!esp_mesh_is_root()) {
        ESP_LOGE(TAG, "Only root node can initiate coordinated reboot");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_reboot_coordinating) {
        ESP_LOGE(TAG, "Reboot coordination already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    /* Verify distribution is complete */
    if (s_distributing) {
        ESP_LOGE(TAG, "Distribution still in progress, cannot reboot");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_node_list == NULL || s_node_count == 0) {
        ESP_LOGE(TAG, "No target nodes available");
        return ESP_ERR_NOT_FOUND;
    }

    /* Verify all nodes have all blocks */
    bool all_complete = true;
    for (int node_idx = 0; node_idx < s_node_count; node_idx++) {
        bool node_complete = true;
        for (uint16_t b = 0; b < s_total_blocks; b++) {
            if (!node_has_block(node_idx, b)) {
                node_complete = false;
                break;
            }
        }
        if (!node_complete) {
            all_complete = false;
            break;
        }
    }

    if (!all_complete) {
        ESP_LOGE(TAG, "Not all nodes have complete firmware");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initiating coordinated reboot for %d nodes", s_node_count);

    /* Allocate bitmap FIRST (before setting coordination flag) to avoid race condition */
    int bitmap_size = (s_node_count + 7) / 8;  /* Round up to bytes */
    s_reboot_nodes_ready_bitmap = (uint8_t *)malloc(bitmap_size);
    if (s_reboot_nodes_ready_bitmap == NULL) {
        ESP_LOGE(TAG, "Failed to allocate reboot ACK bitmap");
        return ESP_ERR_NO_MEM;
    }
    memset(s_reboot_nodes_ready_bitmap, 0, bitmap_size);

    /* Create event group for ACK synchronization */
    s_reboot_prepare_event_group = xEventGroupCreate();
    if (s_reboot_prepare_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        free(s_reboot_nodes_ready_bitmap);
        s_reboot_nodes_ready_bitmap = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Initialize reboot coordination state (set flag LAST to ensure state is ready) */
    s_reboot_nodes_total = s_node_count;
    s_reboot_nodes_ready = 0;
    s_reboot_coordinating = true;  /* Set flag last to ensure all state is initialized */

    /* Get firmware version */
    const char *version = mesh_version_get_string();

    /* Prepare PREPARE_REBOOT message */
    mesh_ota_prepare_reboot_t prepare_msg;
    prepare_msg.cmd = MESH_CMD_OTA_PREPARE_REBOOT;
    prepare_msg.timeout_seconds = __builtin_bswap16(timeout_seconds);
    strncpy((char *)prepare_msg.version, version, sizeof(prepare_msg.version) - 1);
    prepare_msg.version[sizeof(prepare_msg.version) - 1] = '\0';

    mesh_data_t data;
    data.data = (uint8_t *)&prepare_msg;
    data.size = sizeof(prepare_msg);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    /* Send PREPARE_REBOOT to all nodes */
    esp_err_t err = ESP_OK;
    for (int i = 0; i < s_node_count; i++) {
        esp_err_t send_err = esp_mesh_send(&s_node_list[i], &data, MESH_DATA_P2P, NULL, 0);
        if (send_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send PREPARE_REBOOT to node %d: %s", i, esp_err_to_name(send_err));
            err = send_err;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send PREPARE_REBOOT to some nodes");
        /* Cleanup reboot coordination state */
        if (s_reboot_prepare_event_group != NULL) {
            vEventGroupDelete(s_reboot_prepare_event_group);
            s_reboot_prepare_event_group = NULL;
        }
        if (s_reboot_nodes_ready_bitmap != NULL) {
            free(s_reboot_nodes_ready_bitmap);
            s_reboot_nodes_ready_bitmap = NULL;
        }
        s_reboot_coordinating = false;
        s_reboot_nodes_ready = 0;
        s_reboot_nodes_total = 0;
        return err;
    }

    ESP_LOGI(TAG, "Waiting for PREPARE_REBOOT ACKs (timeout: %d seconds)", timeout_seconds);

    /* Wait for all nodes to ACK (with timeout) */
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_seconds * 1000);
    TickType_t start_time = xTaskGetTickCount();
    TickType_t elapsed_time = 0;

    while (s_reboot_nodes_ready < s_reboot_nodes_total && elapsed_time < timeout_ticks) {
        /* Wait for ACK event with remaining timeout */
        TickType_t remaining = timeout_ticks - elapsed_time;
        if (remaining <= 0) {
            break;  /* Timeout expired */
        }

        (void)xEventGroupWaitBits(
            s_reboot_prepare_event_group,
            0xFFFFFFFF,
            pdTRUE,
            pdFALSE,
            remaining
        );

        /* Update elapsed time after wait */
        elapsed_time = xTaskGetTickCount() - start_time;

        /* Check if all nodes are ready (may have changed during wait) */
        if (s_reboot_nodes_ready >= s_reboot_nodes_total) {
            break;  /* All nodes ready, exit immediately */
        }

        /* Small delay only if not at timeout, to allow more ACKs */
        if (elapsed_time < timeout_ticks) {
            TickType_t delay_remaining = timeout_ticks - elapsed_time;
            TickType_t delay_ticks = pdMS_TO_TICKS(100);
            if (delay_ticks > delay_remaining) {
                delay_ticks = delay_remaining;  /* Don't delay past timeout */
            }
            vTaskDelay(delay_ticks);
            elapsed_time = xTaskGetTickCount() - start_time;
        }
    }

    if (s_reboot_nodes_ready < s_reboot_nodes_total) {
        ESP_LOGW(TAG, "Timeout: only %d/%d nodes ready for reboot",
                 s_reboot_nodes_ready, s_reboot_nodes_total);
        /* Cleanup reboot coordination state */
        if (s_reboot_prepare_event_group != NULL) {
            vEventGroupDelete(s_reboot_prepare_event_group);
            s_reboot_prepare_event_group = NULL;
        }
        if (s_reboot_nodes_ready_bitmap != NULL) {
            free(s_reboot_nodes_ready_bitmap);
            s_reboot_nodes_ready_bitmap = NULL;
        }
        s_reboot_coordinating = false;
        s_reboot_nodes_ready = 0;
        s_reboot_nodes_total = 0;
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "All %d nodes ready for reboot", s_reboot_nodes_ready);

    /* Set rollback flag before coordinated reboot */
    /* This flag will be checked on next boot to rollback if mesh connection fails */
    esp_err_t rollback_flag_err = mesh_ota_set_rollback_flag();
    if (rollback_flag_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set rollback flag before reboot: %s", esp_err_to_name(rollback_flag_err));
        /* Continue with reboot even if flag setting fails (non-critical) */
    } else {
        ESP_LOGI(TAG, "Rollback flag set before coordinated reboot");
    }

    /* Verify root node can reboot before sending REBOOT to leaf nodes */
    /* This prevents leaf nodes from rebooting if root node cannot reboot */
    if (s_update_partition != NULL) {
        /* Verify root node boot partition can be set (pre-flight check) */
        /* Note: We don't actually set it here, just verify it's possible */
        const esp_partition_t *current_boot = esp_ota_get_boot_partition();
        if (current_boot == NULL) {
            ESP_LOGE(TAG, "Cannot get current boot partition for root node");
            /* Cleanup and return error before sending REBOOT to leaf nodes */
            if (s_reboot_prepare_event_group != NULL) {
                vEventGroupDelete(s_reboot_prepare_event_group);
                s_reboot_prepare_event_group = NULL;
            }
            if (s_reboot_nodes_ready_bitmap != NULL) {
                free(s_reboot_nodes_ready_bitmap);
                s_reboot_nodes_ready_bitmap = NULL;
            }
            s_reboot_coordinating = false;
            s_reboot_nodes_ready = 0;
            s_reboot_nodes_total = 0;
            return ESP_ERR_INVALID_STATE;
        }

        /* Verify update partition is valid and different from current boot partition */
        if (s_update_partition->address == current_boot->address) {
            ESP_LOGE(TAG, "Root node: update partition is same as boot partition");
            /* Cleanup and return error before sending REBOOT to leaf nodes */
            if (s_reboot_prepare_event_group != NULL) {
                vEventGroupDelete(s_reboot_prepare_event_group);
                s_reboot_prepare_event_group = NULL;
            }
            if (s_reboot_nodes_ready_bitmap != NULL) {
                free(s_reboot_nodes_ready_bitmap);
                s_reboot_nodes_ready_bitmap = NULL;
            }
            s_reboot_coordinating = false;
            s_reboot_nodes_ready = 0;
            s_reboot_nodes_total = 0;
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        ESP_LOGW(TAG, "Root node: no update partition available");
        /* Allow reboot coordination to proceed - root node will skip its own reboot */
    }

    /* All nodes ready, send REBOOT command */
    mesh_ota_reboot_t reboot_msg;
    reboot_msg.cmd = MESH_CMD_OTA_REBOOT;
    reboot_msg.delay_ms = __builtin_bswap16(reboot_delay_ms);

    data.data = (uint8_t *)&reboot_msg;
    data.size = sizeof(reboot_msg);

    /* Send REBOOT to all nodes */
    for (int i = 0; i < s_node_count; i++) {
        esp_err_t send_err = esp_mesh_send(&s_node_list[i], &data, MESH_DATA_P2P, NULL, 0);
        if (send_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send REBOOT to node %d: %s", i, esp_err_to_name(send_err));
        }
    }

    ESP_LOGI(TAG, "REBOOT command sent to all nodes (delay: %d ms)", reboot_delay_ms);

    /* Cleanup coordination state */
    if (s_reboot_prepare_event_group != NULL) {
        vEventGroupDelete(s_reboot_prepare_event_group);
        s_reboot_prepare_event_group = NULL;
    }
    if (s_reboot_nodes_ready_bitmap != NULL) {
        free(s_reboot_nodes_ready_bitmap);
        s_reboot_nodes_ready_bitmap = NULL;
    }
    s_reboot_coordinating = false;
    s_reboot_nodes_ready = 0;
    s_reboot_nodes_total = 0;

    /* Root node must also reboot to use new firmware */
    /* Verify root node has firmware ready (from download) */
    if (s_update_partition != NULL) {
        /* Set boot partition for root node */
        esp_err_t err = esp_ota_set_boot_partition(s_update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Root node failed to set boot partition: %s", esp_err_to_name(err));
            /* At this point REBOOT command has been sent, but we still return error */
            /* This is an edge case - leaf nodes will reboot, root won't */
            return err;
        }

        /* Verify boot partition was set correctly */
        const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
        if (boot_partition == NULL || boot_partition->address != s_update_partition->address) {
            ESP_LOGE(TAG, "Root node boot partition verification failed");
            /* At this point REBOOT command has been sent, but we still return error */
            return ESP_ERR_INVALID_STATE;
        }

        ESP_LOGI(TAG, "Root node boot partition set, rebooting in %d ms", reboot_delay_ms);

        /* Wait for delay (allows other nodes to receive REBOOT command first) */
        vTaskDelay(pdMS_TO_TICKS(reboot_delay_ms));

        /* Reboot root node */
        ESP_LOGI(TAG, "Root node rebooting...");
        esp_restart();
        /* Never returns */
    } else {
        ESP_LOGW(TAG, "Root node: no update partition available, skipping reboot");
    }

    return ESP_OK;
}

/*******************************************************
 *                Rollback Flag Management
 *******************************************************/

#define MESH_OTA_ROLLBACK_NAMESPACE  "mesh"
#define MESH_OTA_ROLLBACK_KEY        "ota_rollback"
#define MESH_OTA_ROLLBACK_COUNT_KEY  "ota_rollback_count"
#define MESH_OTA_ROLLBACK_MAX_ATTEMPTS 3

esp_err_t mesh_ota_set_rollback_flag(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    /* Open NVS namespace */
    err = nvs_open(MESH_OTA_ROLLBACK_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace for rollback flag: %s", esp_err_to_name(err));
        return err;
    }

    /* Set rollback flag to 1 */
    err = nvs_set_u8(nvs_handle, MESH_OTA_ROLLBACK_KEY, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set rollback flag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    /* Reset rollback counter to 0 (this is first boot after update, counter will be incremented on failure) */
    err = nvs_set_u8(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to reset rollback counter: %s", esp_err_to_name(err));
        /* Continue even if counter reset fails */
    }

    /* Commit changes */
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit rollback flag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    ESP_LOGI(TAG, "Rollback flag set in NVS (counter reset to 0)");
    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t mesh_ota_clear_rollback_flag(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    /* Open NVS namespace */
    err = nvs_open(MESH_OTA_ROLLBACK_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace for rollback flag: %s", esp_err_to_name(err));
        return err;
    }

    /* Erase rollback flag */
    err = nvs_erase_key(nvs_handle, MESH_OTA_ROLLBACK_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase rollback flag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    /* Also clear rollback attempt counter */
    nvs_erase_key(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY);  /* Ignore errors for counter - reset to 0 */

    /* Commit changes */
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit rollback flag clear: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    ESP_LOGI(TAG, "Rollback flag cleared from NVS");
    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t mesh_ota_get_rollback_flag(bool *rollback_needed)
{
    if (!rollback_needed) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    /* Open NVS namespace */
    err = nvs_open(MESH_OTA_ROLLBACK_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace for rollback flag: %s", esp_err_to_name(err));
        *rollback_needed = false;
        return err;
    }

    /* Read rollback flag */
    uint8_t flag_value = 0;
    err = nvs_get_u8(nvs_handle, MESH_OTA_ROLLBACK_KEY, &flag_value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Flag not found, rollback not needed */
        *rollback_needed = false;
        nvs_close(nvs_handle);
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read rollback flag: %s", esp_err_to_name(err));
        *rollback_needed = false;
        nvs_close(nvs_handle);
        return err;
    }

    /* Flag exists, check value */
    *rollback_needed = (flag_value == 1);
    nvs_close(nvs_handle);
    return ESP_OK;
}

/*******************************************************
 *                Rollback Check Implementation
 *******************************************************/

/* Rollback timeout task state */
static TaskHandle_t s_rollback_timeout_task = NULL;

esp_err_t mesh_ota_check_rollback(void)
{
    bool rollback_needed = false;
    esp_err_t err = mesh_ota_get_rollback_flag(&rollback_needed);
    if (err != ESP_OK) {
        /* On error, assume rollback not needed and continue normal boot */
        ESP_LOGW(TAG, "Failed to read rollback flag, assuming no rollback needed: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    if (!rollback_needed) {
        /* No rollback needed, normal boot */
        return ESP_OK;
    }

    /* Rollback flag is set - check rollback attempt counter to prevent infinite loops */
    nvs_handle_t nvs_handle;
    err = nvs_open(MESH_OTA_ROLLBACK_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for rollback counter: %s", esp_err_to_name(err));
        /* Clear rollback flag to prevent infinite loops */
        mesh_ota_clear_rollback_flag();
        return err;
    }

    uint8_t rollback_count = 0;
    err = nvs_get_u8(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY, &rollback_count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to read rollback counter: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        /* Clear rollback flag to prevent infinite loops */
        mesh_ota_clear_rollback_flag();
        return err;
    }

    /* Check if we've exceeded maximum rollback attempts */
    if (rollback_count >= MESH_OTA_ROLLBACK_MAX_ATTEMPTS) {
        ESP_LOGE(TAG, "Rollback attempt limit (%d) exceeded, clearing rollback flag to prevent infinite loop",
                 MESH_OTA_ROLLBACK_MAX_ATTEMPTS);
        /* Clear rollback flag and counter to prevent infinite loops */
        nvs_erase_key(nvs_handle, MESH_OTA_ROLLBACK_KEY);
        nvs_erase_key(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_STATE;
    }

    /* If counter is 0, this is the first boot after update - monitor mesh connection, don't rollback yet */
    /* The timeout task will handle rollback if mesh connection fails */
    if (rollback_count == 0) {
        ESP_LOGI(TAG, "Rollback flag detected on first boot after update, will monitor mesh connection (counter: 0)");
        nvs_close(nvs_handle);
        return ESP_OK;  /* Continue normal boot, timeout task will handle monitoring */
    }

    /* Counter > 0 means we've already tried and mesh failed - rollback now */
    /* Increment counter for this rollback attempt */
    rollback_count++;
    err = nvs_set_u8(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY, rollback_count);
    if (err == ESP_OK) {
        nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Rollback flag detected after mesh connection failure, attempting rollback (attempt %d/%d)", rollback_count, MESH_OTA_ROLLBACK_MAX_ATTEMPTS);

    /* Get current boot partition */
    const esp_partition_t *current_boot = esp_ota_get_running_partition();
    if (current_boot == NULL) {
        ESP_LOGE(TAG, "Failed to get current boot partition, cannot rollback");
        mesh_ota_clear_rollback_flag();
        return ESP_ERR_NOT_FOUND;
    }

    /* Get other OTA partition (the one we should rollback to) */
    const esp_partition_t *rollback_partition = esp_ota_get_next_update_partition(NULL);
    if (rollback_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get rollback partition, cannot rollback");
        mesh_ota_clear_rollback_flag();
        return ESP_ERR_NOT_FOUND;
    }

    /* Verify rollback partition is different from current boot partition */
    if (rollback_partition->address == current_boot->address) {
        ESP_LOGW(TAG, "Rollback partition is same as current boot partition, clearing rollback flag");
        mesh_ota_clear_rollback_flag();
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Rolling back from partition at 0x%08x to partition at 0x%08x",
             current_boot->address, rollback_partition->address);

    /* Switch boot partition */
    err = esp_ota_set_boot_partition(rollback_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set rollback boot partition: %s", esp_err_to_name(err));
        mesh_ota_clear_rollback_flag();
        return err;
    }

    /* Verify partition switch succeeded */
    const esp_partition_t *new_boot = esp_ota_get_boot_partition();
    if (new_boot == NULL || new_boot->address != rollback_partition->address) {
        ESP_LOGE(TAG, "Rollback partition switch verification failed");
        mesh_ota_clear_rollback_flag();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Rollback partition set successfully, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));  /* Give time for log messages */
    esp_restart();
    /* Never returns */
    return ESP_OK;
}

/*******************************************************
 *                Rollback Timeout Task
 *******************************************************/

#define MESH_OTA_ROLLBACK_TIMEOUT_MS  300000  /* 5 minutes */

static void rollback_timeout_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Rollback timeout task started, monitoring mesh connection for %d ms", MESH_OTA_ROLLBACK_TIMEOUT_MS);

    /* Wait for timeout duration */
    vTaskDelay(pdMS_TO_TICKS(MESH_OTA_ROLLBACK_TIMEOUT_MS));

    /* After timeout, check if mesh is still connected */
    bool mesh_connected = mesh_common_is_running();
    
    if (mesh_connected) {
        /* Mesh is connected after timeout period - connection is stable, clear rollback flag */
        ESP_LOGI(TAG, "Mesh connection stable after rollback timeout period, clearing rollback flag");
        esp_err_t clear_err = mesh_ota_clear_rollback_flag();
        if (clear_err == ESP_OK) {
            ESP_LOGI(TAG, "Rollback flag cleared after successful mesh connection");
        } else {
            ESP_LOGW(TAG, "Failed to clear rollback flag: %s", esp_err_to_name(clear_err));
        }
        s_rollback_timeout_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Mesh is not connected after timeout - connection failed, increment counter and keep flag set */
    /* The flag and incremented counter will trigger rollback on next boot */
    ESP_LOGW(TAG, "Mesh connection failed after rollback timeout (%d ms), incrementing rollback counter", MESH_OTA_ROLLBACK_TIMEOUT_MS);
    
    /* Increment rollback counter so next boot knows to rollback */
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(MESH_OTA_ROLLBACK_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        uint8_t rollback_count = 0;
        nvs_get_u8(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY, &rollback_count);  /* Ignore errors */
        rollback_count++;
        nvs_set_u8(nvs_handle, MESH_OTA_ROLLBACK_COUNT_KEY, rollback_count);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGW(TAG, "Rollback counter incremented to %d, rollback will happen on next boot", rollback_count);
    }
    
    /* Flag remains set, counter incremented - next boot will trigger rollback */
    s_rollback_timeout_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t mesh_ota_start_rollback_timeout(void)
{
    if (s_rollback_timeout_task != NULL) {
        /* Timeout task already running */
        ESP_LOGW(TAG, "Rollback timeout task already running");
        return ESP_OK;
    }

    /* Create timeout task */
    BaseType_t ret = xTaskCreate(
        rollback_timeout_task,
        "rollback_timeout",
        4096,  /* Stack size */
        NULL,  /* Parameters */
        5,     /* Priority (medium) */
        &s_rollback_timeout_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create rollback timeout task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Rollback timeout task started");
    return ESP_OK;
}

esp_err_t mesh_ota_stop_rollback_timeout(void)
{
    if (s_rollback_timeout_task == NULL) {
        /* No timeout task running */
        return ESP_OK;
    }

    /* Delete timeout task */
    vTaskDelete(s_rollback_timeout_task);
    s_rollback_timeout_task = NULL;

    ESP_LOGI(TAG, "Rollback timeout task stopped");
    return ESP_OK;
}
