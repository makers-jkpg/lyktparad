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
#include "light_neopixel.h"
#include "mesh_config.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_mesh.h"
#include "esp_mac.h"
#include <string.h>
#include <strings.h>  /* for strncasecmp */
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
        image_len = esp_https_ota_get_image_len(s_https_ota_handle);

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
        image_len = esp_https_ota_get_image_len(s_https_ota_handle);

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
    static const uint32_t crc_table[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
        0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
    };

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
}

esp_err_t mesh_ota_distribute_firmware(void)
{
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

    /* Get firmware size from partition */
    /* For distribution, we use the full partition size as firmware may have been written to it */
    /* The actual firmware image size could be determined from app description, but since */
    /* the partition was already validated during download, using partition size is safe */
    s_firmware_size = update_part->size;

    if (s_firmware_size == 0 || s_firmware_size > update_part->size) {
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
        TickType_t timeout = pdMS_TO_TICKS(5000);
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
        /* Block acknowledgment */
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
