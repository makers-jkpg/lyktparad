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
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <string.h>
#include <strings.h>  /* for strncasecmp */
#include <stdlib.h>   /* for malloc/free */
#include "freertos/task.h"

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
