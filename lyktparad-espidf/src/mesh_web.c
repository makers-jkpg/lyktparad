#include "mesh_web.h"
#include "light_neopixel.h"
#include "plugins/sequence/sequence_plugin.h"
#include "plugin_system.h"
#include "mesh_ota.h"
#include "mesh_version.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "generated_html.h"

static const char *WEB_TAG = "mesh_web";
static httpd_handle_t server_handle = NULL;

/* Forward declarations */
static esp_err_t api_nodes_handler(httpd_req_t *req);
static esp_err_t api_color_get_handler(httpd_req_t *req);
static esp_err_t api_color_post_handler(httpd_req_t *req);
static esp_err_t api_sequence_post_handler(httpd_req_t *req);
static esp_err_t api_sequence_pointer_handler(httpd_req_t *req);
static esp_err_t api_sequence_start_handler(httpd_req_t *req);
static esp_err_t api_sequence_stop_handler(httpd_req_t *req);
static esp_err_t api_sequence_reset_handler(httpd_req_t *req);
static esp_err_t api_sequence_status_handler(httpd_req_t *req);
static esp_err_t api_ota_download_post_handler(httpd_req_t *req);
static esp_err_t api_ota_status_get_handler(httpd_req_t *req);
static esp_err_t api_ota_cancel_post_handler(httpd_req_t *req);
static esp_err_t api_ota_version_get_handler(httpd_req_t *req);
static esp_err_t api_ota_distribute_post_handler(httpd_req_t *req);
static esp_err_t api_ota_distribution_status_get_handler(httpd_req_t *req);
static esp_err_t api_ota_distribution_progress_get_handler(httpd_req_t *req);
static esp_err_t api_ota_distribution_cancel_post_handler(httpd_req_t *req);
static esp_err_t api_ota_reboot_post_handler(httpd_req_t *req);
static esp_err_t api_plugin_activate_handler(httpd_req_t *req);
static esp_err_t api_plugin_deactivate_handler(httpd_req_t *req);
static esp_err_t api_plugin_active_handler(httpd_req_t *req);
static esp_err_t api_plugins_list_handler(httpd_req_t *req);
static esp_err_t index_handler(httpd_req_t *req);

/* HTML page is now generated from template and plugins - see generated_html.h */
/* The html_page variable is defined in generated_html.h */

/* API: GET /api/nodes - Returns number of nodes in mesh */
static esp_err_t api_nodes_handler(httpd_req_t *req)
{
    int node_count = mesh_get_node_count();
    char response[64];
    int len = snprintf(response, sizeof(response), "{\"nodes\":%d}", node_count);

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"error\":\"Response formatting error\"}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response, len);
}

/* API: GET /api/color - Returns current RGB color */
static esp_err_t api_color_get_handler(httpd_req_t *req)
{
    uint8_t r, g, b;
    bool is_set;
    mesh_get_current_rgb(&r, &g, &b, &is_set);

    char response[128];
    int len = snprintf(response, sizeof(response),
                      "{\"r\":%d,\"g\":%d,\"b\":%d,\"is_set\":%s}",
                      r, g, b, is_set ? "true" : "false");

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"error\":\"Response formatting error\"}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response, len);
}

/* API: POST /api/color - Accepts RGB values and applies them */
static esp_err_t api_color_post_handler(httpd_req_t *req)
{
    char content[256];
    /* httpd_req_recv() will only read up to sizeof(content) - 1 bytes, protecting against buffer overflow */
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    content[ret] = '\0';

    /* Simple JSON parsing for {"r":X,"g":Y,"b":Z} */
    uint8_t r = 0, g = 0, b = 0;
    char *r_str = strstr(content, "\"r\":");
    char *g_str = strstr(content, "\"g\":");
    char *b_str = strstr(content, "\"b\":");

    if (r_str && g_str && b_str) {
        int r_val = atoi(r_str + 4);
        int g_val = atoi(g_str + 4);
        int b_val = atoi(b_str + 4);

        /* Validate RGB values before casting (check for negative and overflow) */
        if (r_val < 0 || r_val > 255 || g_val < 0 || g_val > 255 || b_val < 0 || b_val > 255) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"success\":false,\"error\":\"RGB values must be 0-255\"}", -1);
            return ESP_FAIL;
        }

        r = (uint8_t)r_val;
        g = (uint8_t)g_val;
        b = (uint8_t)b_val;
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }

    /* Apply color via mesh */
    esp_err_t err = mesh_send_rgb(r, g, b);

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to send RGB command\"}", -1);
        return ESP_FAIL;
    }
}

/* API: POST /api/sequence - Receives sequence data (variable length: rhythm + length + color data) */
static esp_err_t api_sequence_post_handler(httpd_req_t *req)
{
    /* Use maximum size buffer (386 bytes for 16 rows) */
    uint8_t content[2 + 384];  /* rhythm + length + max color data */
    int total_received = 0;
    int ret;
    uint8_t num_rows = 0;
    uint16_t expected_size = 0;

    /* Read minimum payload first (rhythm + length = 2 bytes) */
    while (total_received < 2) {
        ret = httpd_req_recv(req, (char *)(content + total_received), 2 - total_received);
        if (ret <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request or connection closed\"}", -1);
            return ESP_FAIL;
        }
        total_received += ret;
    }

    /* Extract and validate length to calculate expected size */
    num_rows = content[1];
    // #region agent log
    int64_t timestamp = esp_timer_get_time() / 1000;
    ESP_LOGI("DEBUG", "{\"location\":\"mesh_web.c:1422\",\"message\":\"api_sequence_post num_rows received\",\"data\":{\"num_rows\":%d,\"hypothesisId\":\"E\"},\"timestamp\":%lld}",
             num_rows, timestamp);
    // #endregion
    if (num_rows < 1 || num_rows > 16) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Sequence length must be 1-16 rows\"}", -1);
        return ESP_FAIL;
    }

    /* Calculate expected payload size */
    expected_size = sequence_payload_size(num_rows);

    /* Read remaining payload */
    while (total_received < expected_size) {
        int remaining = expected_size - total_received;
        ret = httpd_req_recv(req, (char *)(content + total_received), remaining);
        if (ret <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request or connection closed\"}", -1);
            return ESP_FAIL;
        }
        total_received += ret;
        /* Safety check: prevent reading more than expected */
        if (total_received > expected_size) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"success\":false,\"error\":\"Payload size exceeded\"}", -1);
            return ESP_FAIL;
        }
    }

    if (total_received != expected_size) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid payload size\"}", -1);
        return ESP_FAIL;
    }

    /* Extract rhythm byte (first byte) */
    uint8_t rhythm = content[0];

    /* Validate rhythm range (1-255) - uint8_t cannot exceed 255 */
    if (rhythm == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Rhythm value must be 1-255\"}", -1);
        return ESP_FAIL;
    }

    /* Extract color data pointer and length */
    uint8_t *color_data = &content[2];
    uint16_t color_data_len = total_received - 2;

    /* Validate color data length */
    if (color_data_len != sequence_color_data_size(num_rows)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid color data size\"}", -1);
        return ESP_FAIL;
    }

    /* Store and broadcast sequence data */
    esp_err_t err = sequence_plugin_root_store_and_broadcast(rhythm, num_rows, color_data, color_data_len);

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to store and broadcast sequence\"}", -1);
        return ESP_FAIL;
    }
}

/* API: GET /api/sequence/pointer - Returns current sequence pointer (0-255) as plain text */
static esp_err_t api_sequence_pointer_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "0", -1);
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t pointer = sequence_plugin_root_get_pointer();
    char response[16];
    int len = snprintf(response, sizeof(response), "%d", pointer);

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "0", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response, len);
}

/* API: POST /api/sequence/start - Start sequence playback */
static esp_err_t api_sequence_start_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can start sequence\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sequence_plugin_root_start();

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Start failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: POST /api/sequence/stop - Stop sequence playback */
static esp_err_t api_sequence_stop_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can stop sequence\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sequence_plugin_root_stop();

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Stop failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: POST /api/sequence/reset - Reset sequence pointer */
static esp_err_t api_sequence_reset_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can reset sequence\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sequence_plugin_root_reset();

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Reset failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: GET /api/sequence/status - Returns sequence status */
static esp_err_t api_sequence_status_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"active\":false}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    bool active = sequence_plugin_root_is_active();

    char response[128];
    int len = snprintf(response, sizeof(response), "{\"active\":%s}",
                      active ? "true" : "false");

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"active\":false}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response, len);
}

/* API: POST /api/ota/download - Initiates firmware download */
static esp_err_t api_ota_download_post_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can download firmware\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    content[ret] = '\0';

    /* Simple JSON parsing for {"url":"..."} */
    char *url_str = strstr(content, "\"url\":");
    if (url_str == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Missing url field\"}", -1);
        return ESP_FAIL;
    }

    /* Extract URL value */
    url_str += 6;  /* Skip "url": */
    while (*url_str == ' ' || *url_str == '\t') url_str++;  /* Skip whitespace */
    if (*url_str == '"') {
        url_str++;  /* Skip opening quote */
        char *url_end = strchr(url_str, '"');
        if (url_end == NULL) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid URL format\"}", -1);
            return ESP_FAIL;
        }
        *url_end = '\0';  /* Null-terminate URL */
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid URL format\"}", -1);
        return ESP_FAIL;
    }

    /* Validate URL length */
    if (strlen(url_str) == 0 || strlen(url_str) > 400) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid URL length\"}", -1);
        return ESP_FAIL;
    }

    /* Start download */
    esp_err_t err = mesh_ota_download_firmware(url_str);

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else if (err == ESP_ERR_INVALID_VERSION) {
        /* Downgrade detected - return 409 Conflict */
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Downgrade prevented: Firmware version is older than current version\"}", -1);
        return ESP_FAIL;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Download failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: GET /api/ota/status - Returns download status */
static esp_err_t api_ota_status_get_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"downloading\":false,\"progress\":0.0}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    bool downloading = mesh_ota_is_downloading();
    float progress = mesh_ota_get_download_progress();

    char response[128];
    int len = snprintf(response, sizeof(response), "{\"downloading\":%s,\"progress\":%.2f}",
                      downloading ? "true" : "false", progress);

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"downloading\":false,\"progress\":0.0}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

/* API: POST /api/ota/cancel - Cancels ongoing download */
static esp_err_t api_ota_cancel_post_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can cancel download\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = mesh_ota_cancel_download();

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Cancel failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: GET /api/ota/version - Returns current firmware version */
static esp_err_t api_ota_version_get_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"version\":\"unknown\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    const char *version = mesh_version_get_string();
    char response[64];
    int len = snprintf(response, sizeof(response), "{\"version\":\"%s\"}", version);

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"version\":\"unknown\"}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

/* API: POST /api/ota/distribute - Start firmware distribution */
static esp_err_t api_ota_distribute_post_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can distribute firmware\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = mesh_ota_distribute_firmware();

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else if (err == ESP_ERR_INVALID_VERSION) {
        /* Downgrade detected - return 409 Conflict */
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Downgrade prevented: Firmware version is older than current version\"}", -1);
        return ESP_FAIL;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Distribution failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: GET /api/ota/distribution/status - Get distribution status */
static esp_err_t api_ota_distribution_status_get_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"distributing\":false,\"total_blocks\":0,\"current_block\":0,\"overall_progress\":0.0,\"nodes_total\":0,\"nodes_complete\":0,\"nodes_failed\":0}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    mesh_ota_distribution_status_t status;
    esp_err_t err = mesh_ota_get_distribution_status(&status);

    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"distributing\":false,\"total_blocks\":0,\"current_block\":0,\"overall_progress\":0.0,\"nodes_total\":0,\"nodes_complete\":0,\"nodes_failed\":0}", -1);
        return ESP_FAIL;
    }

    char response[256];
    int len = snprintf(response, sizeof(response),
                      "{\"distributing\":%s,\"total_blocks\":%d,\"current_block\":%d,\"overall_progress\":%.2f,\"nodes_total\":%d,\"nodes_complete\":%d,\"nodes_failed\":%d}",
                      status.distributing ? "true" : "false",
                      status.total_blocks,
                      status.current_block,
                      status.overall_progress,
                      status.nodes_total,
                      status.nodes_complete,
                      status.nodes_failed);

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"distributing\":false,\"total_blocks\":0,\"current_block\":0,\"overall_progress\":0.0,\"nodes_total\":0,\"nodes_complete\":0,\"nodes_failed\":0}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

/* API: GET /api/ota/distribution/progress - Get distribution progress */
static esp_err_t api_ota_distribution_progress_get_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"progress\":0.0}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    float progress = mesh_ota_get_distribution_progress();

    char response[64];
    int len = snprintf(response, sizeof(response), "{\"progress\":%.2f}", progress);

    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"progress\":0.0}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

/* API: POST /api/ota/distribution/cancel - Cancel distribution */
static esp_err_t api_ota_distribution_cancel_post_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can cancel distribution\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = mesh_ota_cancel_distribution();

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Cancel failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: POST /api/ota/reboot - Initiate coordinated reboot */
static esp_err_t api_ota_reboot_post_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can initiate reboot\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    /* Default timeout and delay values */
    uint16_t timeout_seconds = 10;
    uint16_t reboot_delay_ms = 1000;

    /* Parse optional JSON body for timeout and delay */
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret > 0) {
        content[ret] = '\0';
        /* Simple JSON parsing - look for timeout and delay_ms */
        char *timeout_str = strstr(content, "\"timeout\"");
        char *delay_str = strstr(content, "\"delay\"");
        if (timeout_str) {
            int parsed = sscanf(timeout_str, "\"timeout\":%hu", &timeout_seconds);
            if (parsed != 1) timeout_seconds = 10;
        }
        if (delay_str) {
            int parsed = sscanf(delay_str, "\"delay\":%hu", &reboot_delay_ms);
            if (parsed != 1) reboot_delay_ms = 1000;
        }
    }

    esp_err_t err = mesh_ota_initiate_coordinated_reboot(timeout_seconds, reboot_delay_ms);

    if (err == ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":true}", -1);
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Reboot failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }
}

/* API: POST /api/plugin/activate - Activate plugin by name */
static esp_err_t api_plugin_activate_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    content[ret] = '\0';

    /* Parse JSON: {"name": "effects"} or {"name": "sequence"} */
    char plugin_name[64] = {0};
    if (sscanf(content, "{\"name\":\"%63[^\"]\"}", plugin_name) != 1) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }

    /* Activate plugin */
    esp_err_t err = plugin_activate(plugin_name);
    if (err != ESP_OK) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    /* Send success response */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char response[128];
    snprintf(response, sizeof(response), "{\"success\":true,\"plugin\":\"%s\"}", plugin_name);
    return httpd_resp_send(req, response, -1);
}

/* API: POST /api/plugin/deactivate - Deactivate plugin by name */
static esp_err_t api_plugin_deactivate_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    content[ret] = '\0';

    /* Parse JSON: {"name": "effects"} or {"name": "sequence"} */
    char plugin_name[64] = {0};
    if (sscanf(content, "{\"name\":\"%63[^\"]\"}", plugin_name) != 1) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }

    /* Deactivate plugin */
    esp_err_t err = plugin_deactivate(plugin_name);
    if (err != ESP_OK) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    /* Send success response */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char response[128];
    snprintf(response, sizeof(response), "{\"success\":true,\"plugin\":\"%s\"}", plugin_name);
    return httpd_resp_send(req, response, -1);
}

/* API: GET /api/plugin/active - Get currently active plugin */
static esp_err_t api_plugin_active_handler(httpd_req_t *req)
{
    const char *active = plugin_get_active();
    char response[128];
    if (active != NULL) {
        snprintf(response, sizeof(response), "{\"active\":\"%s\"}", active);
    } else {
        snprintf(response, sizeof(response), "{\"active\":null}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response, -1);
}

/* API: GET /api/plugins - Get list of all registered plugins */
static esp_err_t api_plugins_list_handler(httpd_req_t *req)
{
    const char *names[16]; /* MAX_PLUGINS = 16 */
    uint8_t count = 0;
    esp_err_t err = plugin_get_all_names(names, 16, &count);

    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"error\":\"Failed to get plugin list\"}", -1);
        return ESP_FAIL;
    }

    /* Build JSON array of plugin names */
    char response[512] = "{\"plugins\":[";
    int offset = strlen(response);
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) {
            response[offset++] = ',';
        }
        offset += snprintf(response + offset, sizeof(response) - offset, "\"%s\"", names[i]);
        if (offset >= (int)sizeof(response) - 10) {
            break; /* Prevent buffer overflow */
        }
    }
    offset += snprintf(response + offset, sizeof(response) - offset, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response, -1);
}

/* GET / - Serves main HTML page */
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

/* Start HTTP web server */
esp_err_t mesh_web_server_start(void)
{
    /* Only start on root node */
    if (!esp_mesh_is_root()) {
        ESP_LOGI(WEB_TAG, "Not root node, web server not started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if server is already running */
    if (server_handle != NULL) {
        ESP_LOGW(WEB_TAG, "Web server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 25;  /* Updated: nodes, color_get, color_post, sequence_post, sequence_pointer, sequence_start, sequence_stop, sequence_reset, sequence_status, ota_download, ota_status, ota_cancel, ota_version, ota_distribute, ota_distribution_status, ota_distribution_progress, ota_distribution_cancel, ota_reboot, plugin_activate, plugin_deactivate, plugin_active, plugins_list, index = 23 handlers */
    config.stack_size = 8192;
    config.server_port = 80;

    ESP_LOGI(WEB_TAG, "Starting web server on port %d", config.server_port);

    if (httpd_start(&server_handle, &config) == ESP_OK) {
        esp_err_t reg_err;
        /* Register URI handlers */
        httpd_uri_t nodes_uri = {
            .uri       = "/api/nodes",
            .method    = HTTP_GET,
            .handler   = api_nodes_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &nodes_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register nodes URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t color_get_uri = {
            .uri       = "/api/color",
            .method    = HTTP_GET,
            .handler   = api_color_get_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &color_get_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register color GET URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t color_post_uri = {
            .uri       = "/api/color",
            .method    = HTTP_POST,
            .handler   = api_color_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &color_post_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register color POST URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t sequence_post_uri = {
            .uri       = "/api/sequence",
            .method    = HTTP_POST,
            .handler   = api_sequence_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &sequence_post_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register sequence POST URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t sequence_pointer_uri = {
            .uri       = "/api/sequence/pointer",
            .method    = HTTP_GET,
            .handler   = api_sequence_pointer_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &sequence_pointer_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register sequence pointer URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t sequence_start_uri = {
            .uri       = "/api/sequence/start",
            .method    = HTTP_POST,
            .handler   = api_sequence_start_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &sequence_start_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register sequence start URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t sequence_stop_uri = {
            .uri       = "/api/sequence/stop",
            .method    = HTTP_POST,
            .handler   = api_sequence_stop_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &sequence_stop_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register sequence stop URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t sequence_reset_uri = {
            .uri       = "/api/sequence/reset",
            .method    = HTTP_POST,
            .handler   = api_sequence_reset_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &sequence_reset_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register sequence reset URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t sequence_status_uri = {
            .uri       = "/api/sequence/status",
            .method    = HTTP_GET,
            .handler   = api_sequence_status_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &sequence_status_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register sequence status URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_download_uri = {
            .uri       = "/api/ota/download",
            .method    = HTTP_POST,
            .handler   = api_ota_download_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_download_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA download URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_status_uri = {
            .uri       = "/api/ota/status",
            .method    = HTTP_GET,
            .handler   = api_ota_status_get_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_status_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA status URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_cancel_uri = {
            .uri       = "/api/ota/cancel",
            .method    = HTTP_POST,
            .handler   = api_ota_cancel_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_cancel_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA cancel URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_version_uri = {
            .uri       = "/api/ota/version",
            .method    = HTTP_GET,
            .handler   = api_ota_version_get_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_version_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA version URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_distribute_uri = {
            .uri       = "/api/ota/distribute",
            .method    = HTTP_POST,
            .handler   = api_ota_distribute_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_distribute_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA distribute URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_distribution_status_uri = {
            .uri       = "/api/ota/distribution/status",
            .method    = HTTP_GET,
            .handler   = api_ota_distribution_status_get_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_distribution_status_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA distribution status URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_distribution_progress_uri = {
            .uri       = "/api/ota/distribution/progress",
            .method    = HTTP_GET,
            .handler   = api_ota_distribution_progress_get_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_distribution_progress_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA distribution progress URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_distribution_cancel_uri = {
            .uri       = "/api/ota/distribution/cancel",
            .method    = HTTP_POST,
            .handler   = api_ota_distribution_cancel_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_distribution_cancel_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA distribution cancel URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t ota_reboot_uri = {
            .uri       = "/api/ota/reboot",
            .method    = HTTP_POST,
            .handler   = api_ota_reboot_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &ota_reboot_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register OTA reboot URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t plugin_activate_uri = {
            .uri       = "/api/plugin/activate",
            .method    = HTTP_POST,
            .handler   = api_plugin_activate_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_activate_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register plugin activate URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t plugin_deactivate_uri = {
            .uri       = "/api/plugin/deactivate",
            .method    = HTTP_POST,
            .handler   = api_plugin_deactivate_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_deactivate_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register plugin deactivate URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t plugin_active_uri = {
            .uri       = "/api/plugin/active",
            .method    = HTTP_GET,
            .handler   = api_plugin_active_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_active_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register plugin active URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t plugins_list_uri = {
            .uri       = "/api/plugins",
            .method    = HTTP_GET,
            .handler   = api_plugins_list_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugins_list_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register plugins list URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &index_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register index URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        ESP_LOGI(WEB_TAG, "Web server started successfully");
        return ESP_OK;
    }

    ESP_LOGE(WEB_TAG, "Error starting web server");
    return ESP_FAIL;
}

/* Stop HTTP web server */
esp_err_t mesh_web_server_stop(void)
{
    if (server_handle == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(WEB_TAG, "Stopping web server");
    esp_err_t err = httpd_stop(server_handle);
    server_handle = NULL;

    if (err == ESP_OK) {
        ESP_LOGI(WEB_TAG, "Web server stopped");
    } else {
        ESP_LOGE(WEB_TAG, "Error stopping web server: 0x%x", err);
    }

    return err;
}

