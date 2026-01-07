#include "mesh_web.h"
#include "light_neopixel.h"
#include "plugin_system.h"
#include "mesh_commands.h"
#include "mesh_ota.h"
#include "mesh_version.h"
#include "mesh_common.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *WEB_TAG = "mesh_web";
static httpd_handle_t server_handle = NULL;

/* Simple HTML page with plugin selection and control buttons */
static const char simple_html_page[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>Plugin Control</title>\n"
"    <style>\n"
"        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
"        body {\n"
"            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;\n"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
"            min-height: 100vh;\n"
"            display: flex;\n"
"            justify-content: center;\n"
"            align-items: center;\n"
"            padding: 20px;\n"
"        }\n"
"        .container {\n"
"            background: white;\n"
"            border-radius: 16px;\n"
"            box-shadow: 0 20px 60px rgba(0,0,0,0.3);\n"
"            padding: 40px;\n"
"            max-width: 500px;\n"
"            width: 100%;\n"
"        }\n"
"        h1 {\n"
"            color: #333;\n"
"            margin-bottom: 30px;\n"
"            text-align: center;\n"
"            font-size: 28px;\n"
"        }\n"
"        .plugin-select {\n"
"            margin-bottom: 30px;\n"
"        }\n"
"        label {\n"
"            display: block;\n"
"            color: #555;\n"
"            margin-bottom: 8px;\n"
"            font-weight: 500;\n"
"        }\n"
"        select {\n"
"            width: 100%;\n"
"            padding: 12px;\n"
"            border: 2px solid #e0e0e0;\n"
"            border-radius: 8px;\n"
"            font-size: 16px;\n"
"            background: white;\n"
"            cursor: pointer;\n"
"            transition: border-color 0.3s;\n"
"        }\n"
"        select:hover { border-color: #667eea; }\n"
"        select:focus { outline: none; border-color: #667eea; }\n"
"        .button-group {\n"
"            display: flex;\n"
"            gap: 12px;\n"
"            margin-bottom: 20px;\n"
"        }\n"
"        button {\n"
"            flex: 1;\n"
"            padding: 14px 24px;\n"
"            border: none;\n"
"            border-radius: 8px;\n"
"            font-size: 16px;\n"
"            font-weight: 600;\n"
"            cursor: pointer;\n"
"            transition: all 0.3s;\n"
"            text-transform: uppercase;\n"
"            letter-spacing: 0.5px;\n"
"        }\n"
"        button:hover { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.2); }\n"
"        button:active { transform: translateY(0); }\n"
"        button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }\n"
"        .btn-play {\n"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
"            color: white;\n"
"        }\n"
"        .btn-pause {\n"
"            background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);\n"
"            color: white;\n"
"        }\n"
"        .btn-rewind {\n"
"            background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%);\n"
"            color: white;\n"
"        }\n"
"        .status {\n"
"            padding: 12px;\n"
"            border-radius: 8px;\n"
"            margin-top: 20px;\n"
"            text-align: center;\n"
"            font-size: 14px;\n"
"            min-height: 40px;\n"
"            display: flex;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
"        }\n"
"        .status.success { background: #d4edda; color: #155724; }\n"
"        .status.error { background: #f8d7da; color: #721c24; }\n"
"        .status.info { background: #d1ecf1; color: #0c5460; }\n"
"        .active-plugin {\n"
"            margin-top: 20px;\n"
"            padding: 12px;\n"
"            background: #f8f9fa;\n"
"            border-radius: 8px;\n"
"            text-align: center;\n"
"            color: #666;\n"
"            font-size: 14px;\n"
"        }\n"
"        .active-plugin strong { color: #667eea; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <h1>Plugin Control</h1>\n"
"        <div class=\"plugin-select\">\n"
"            <label for=\"pluginSelect\">Select Plugin:</label>\n"
"            <select id=\"pluginSelect\"></select>\n"
"        </div>\n"
"        <div class=\"button-group\">\n"
"            <button class=\"btn-play\" id=\"playBtn\" onclick=\"handlePlay()\">Play</button>\n"
"            <button class=\"btn-pause\" id=\"pauseBtn\" onclick=\"handlePause()\">Pause</button>\n"
"            <button class=\"btn-rewind\" id=\"rewindBtn\" onclick=\"handleRewind()\">Rewind</button>\n"
"        </div>\n"
"        <div class=\"active-plugin\" id=\"activePlugin\">No active plugin</div>\n"
"        <div class=\"status\" id=\"status\" style=\"display: none;\"></div>\n"
"    </div>\n"
"    <script>\n"
"        let plugins = [];\n"
"        let activePlugin = null;\n"
"\n"
"        async function loadPlugins() {\n"
"            try {\n"
"                const response = await fetch('/api/plugins');\n"
"                const data = await response.json();\n"
"                plugins = data.plugins || [];\n"
"                const select = document.getElementById('pluginSelect');\n"
"                select.innerHTML = '<option value=\"\">-- Select Plugin --</option>';\n"
"                plugins.forEach(plugin => {\n"
"                    const option = document.createElement('option');\n"
"                    option.value = plugin;\n"
"                    option.textContent = plugin;\n"
"                    select.appendChild(option);\n"
"                });\n"
"            } catch (error) {\n"
"                showStatus('Error loading plugins: ' + error.message, 'error');\n"
"            }\n"
"        }\n"
"\n"
"        async function loadActivePlugin() {\n"
"            try {\n"
"                const response = await fetch('/api/plugin/active');\n"
"                const data = await response.json();\n"
"                activePlugin = data.active;\n"
"                const activeDiv = document.getElementById('activePlugin');\n"
"                if (activePlugin) {\n"
"                    activeDiv.innerHTML = '';\n"
"                    const strong = document.createElement('strong');\n"
"                    strong.textContent = 'Active: ';\n"
"                    activeDiv.appendChild(strong);\n"
"                    activeDiv.appendChild(document.createTextNode(activePlugin));\n"
"                    const select = document.getElementById('pluginSelect');\n"
"                    select.value = activePlugin;\n"
"                } else {\n"
"                    activeDiv.textContent = 'No active plugin';\n"
"                }\n"
"            } catch (error) {\n"
"                console.error('Error loading active plugin:', error);\n"
"            }\n"
"        }\n"
"\n"
"        async function handlePlay() {\n"
"            const select = document.getElementById('pluginSelect');\n"
"            const pluginName = select.value;\n"
"            if (!pluginName) {\n"
"                showStatus('Please select a plugin first', 'error');\n"
"                return;\n"
"            }\n"
"            try {\n"
"                const response = await fetch('/api/plugin/activate', {\n"
"                    method: 'POST',\n"
"                    headers: { 'Content-Type': 'application/json' },\n"
"                    body: JSON.stringify({ name: pluginName })\n"
"                });\n"
"                const data = await response.json();\n"
"                if (data.success) {\n"
"                    showStatus('Plugin activated: ' + pluginName, 'success');\n"
"                    await loadActivePlugin();\n"
"                } else {\n"
"                    showStatus('Error: ' + (data.error || 'Activation failed'), 'error');\n"
"                }\n"
"            } catch (error) {\n"
"                showStatus('Error: ' + error.message, 'error');\n"
"            }\n"
"        }\n"
"\n"
"        async function handlePause() {\n"
"            const select = document.getElementById('pluginSelect');\n"
"            const pluginName = select.value;\n"
"            if (!pluginName) {\n"
"                showStatus('Please select a plugin first', 'error');\n"
"                return;\n"
"            }\n"
"            try {\n"
"                const response = await fetch('/api/plugin/pause', {\n"
"                    method: 'POST',\n"
"                    headers: { 'Content-Type': 'application/json' },\n"
"                    body: JSON.stringify({ name: pluginName })\n"
"                });\n"
"                const data = await response.json();\n"
"                if (data.success) {\n"
"                    showStatus('Plugin paused: ' + pluginName, 'info');\n"
"                    await loadActivePlugin();\n"
"                } else {\n"
"                    showStatus('Error: ' + (data.error || 'Pause failed'), 'error');\n"
"                }\n"
"            } catch (error) {\n"
"                showStatus('Error: ' + error.message, 'error');\n"
"            }\n"
"        }\n"
"\n"
"        async function handleRewind() {\n"
"            const select = document.getElementById('pluginSelect');\n"
"            const pluginName = select.value;\n"
"            if (!pluginName) {\n"
"                showStatus('Please select a plugin first', 'error');\n"
"                return;\n"
"            }\n"
"            try {\n"
"                const response = await fetch('/api/plugin/reset', {\n"
"                    method: 'POST',\n"
"                    headers: { 'Content-Type': 'application/json' },\n"
"                    body: JSON.stringify({ name: pluginName })\n"
"                });\n"
"                const data = await response.json();\n"
"                if (data.success) {\n"
"                    showStatus('Plugin reset: ' + pluginName, 'info');\n"
"                    await loadActivePlugin();\n"
"                } else {\n"
"                    showStatus('Error: ' + (data.error || 'Reset failed'), 'error');\n"
"                }\n"
"            } catch (error) {\n"
"                showStatus('Error: ' + error.message, 'error');\n"
"            }\n"
"        }\n"
"\n"
"        function showStatus(message, type) {\n"
"            const statusDiv = document.getElementById('status');\n"
"            statusDiv.textContent = message;\n"
"            statusDiv.className = 'status ' + type;\n"
"            statusDiv.style.display = 'flex';\n"
"            setTimeout(() => {\n"
"                statusDiv.style.display = 'none';\n"
"            }, 3000);\n"
"        }\n"
"\n"
"        // Load plugins and active plugin on page load\n"
"        loadPlugins();\n"
"        loadActivePlugin();\n"
"        // Poll for active plugin changes every 2 seconds\n"
"        setInterval(loadActivePlugin, 2000);\n"
"    </script>\n"
"</body>\n"
"</html>\n";

/* Forward declarations */
static void mesh_web_diagnostic_task(void *pvParameters);
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
static esp_err_t api_plugin_stop_handler(httpd_req_t *req);
static esp_err_t api_plugin_pause_handler(httpd_req_t *req);
static esp_err_t api_plugin_reset_handler(httpd_req_t *req);
static esp_err_t api_plugins_list_handler(httpd_req_t *req);
static esp_err_t index_handler(httpd_req_t *req);

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

    /* Calculate expected payload size using plugin helper */
    uint16_t payload_size_result = 0;
    esp_err_t helper_err = plugin_get_helper("sequence", 0x01, &num_rows, &payload_size_result);  /* SEQUENCE_HELPER_PAYLOAD_SIZE */
    if (helper_err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to calculate payload size\"}", -1);
        return ESP_FAIL;
    }
    expected_size = payload_size_result;

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

    /* Validate color data length using plugin helper */
    uint16_t color_data_size_result = 0;
    helper_err = plugin_get_helper("sequence", 0x03, &num_rows, &color_data_size_result);  /* SEQUENCE_HELPER_COLOR_DATA_SIZE */
    if (helper_err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to calculate color data size\"}", -1);
        return ESP_FAIL;
    }
    if (color_data_len != color_data_size_result) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid color data size\"}", -1);
        return ESP_FAIL;
    }

    /* Store and broadcast sequence data using plugin operation */
    typedef struct {
        uint8_t rhythm;
        uint8_t num_rows;
        uint8_t *color_data;
        uint16_t color_data_len;
    } store_params_t;
    store_params_t store_params = {
        .rhythm = rhythm,
        .num_rows = num_rows,
        .color_data = color_data,
        .color_data_len = color_data_len
    };
    esp_err_t err = plugin_execute_operation("sequence", 0x01, &store_params);  /* SEQUENCE_OP_STORE */

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

    /* Get pointer using plugin query */
    uint16_t pointer = 0;
    esp_err_t query_err = plugin_query_state("sequence", 0x02, &pointer);  /* SEQUENCE_QUERY_GET_POINTER */
    if (query_err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "0", -1);
        return ESP_FAIL;
    }
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

    /* Execute start operation using plugin query interface */
    esp_err_t err = plugin_execute_operation("sequence", 0x02, NULL);  /* SEQUENCE_OP_START */

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
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can pause sequence\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    /* Execute pause operation using plugin query interface */
    esp_err_t err = plugin_execute_operation("sequence", 0x03, NULL);  /* SEQUENCE_OP_PAUSE */

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

    /* Execute reset operation using plugin query interface */
    esp_err_t err = plugin_execute_operation("sequence", 0x04, NULL);  /* SEQUENCE_OP_RESET */

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

    /* Query active state using plugin query interface */
    bool active = false;
    esp_err_t query_err = plugin_query_state("sequence", 0x01, &active);  /* SEQUENCE_QUERY_IS_ACTIVE */
    if (query_err != ESP_OK) {
        /* If query fails, assume not active */
        active = false;
    }

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

    /* Parse JSON: {"name": "effect_strobe"} or {"name": "effect_fade"} or {"name": "sequence"} */
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

    /* Parse JSON: {"name": "effect_strobe"} or {"name": "effect_fade"} or {"name": "sequence"} */
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

/* API: POST /api/plugin/stop - Stop plugin (pause then deactivate) */
static esp_err_t api_plugin_stop_handler(httpd_req_t *req)
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

    /* Parse JSON: {"name": "effect_strobe"} or {"name": "effect_fade"} or {"name": "sequence"} */
    char plugin_name[64] = {0};
    if (sscanf(content, "{\"name\":\"%63[^\"]\"}", plugin_name) != 1) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }

    /* Check if plugin is active */
    if (!plugin_is_active(plugin_name)) {
        /* Plugin is not active, just return success */
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char response[128];
        snprintf(response, sizeof(response), "{\"success\":true,\"plugin\":\"%s\",\"message\":\"Plugin was not active\"}", plugin_name);
        return httpd_resp_send(req, response, -1);
    }

    /* Get plugin info to check if it has on_pause callback */
    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin != NULL && plugin->callbacks.on_pause != NULL) {
        /* Call on_pause callback first (graceful stop) */
        esp_err_t pause_err = plugin->callbacks.on_pause();
        if (pause_err != ESP_OK) {
            ESP_LOGW(WEB_TAG, "Plugin '%s' on_pause callback returned error: %s", plugin_name, esp_err_to_name(pause_err));
            /* Continue with deactivation even if pause fails */
        }
    }

    /* Force deactivation (mutual exclusivity enforcement) */
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

/* API: POST /api/plugin/pause - Pause plugin playback */
static esp_err_t api_plugin_pause_handler(httpd_req_t *req)
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

    /* Parse JSON: {"name": "effect_strobe"} or {"name": "effect_fade"} or {"name": "sequence"} */
    char plugin_name[64] = {0};
    if (sscanf(content, "{\"name\":\"%63[^\"]\"}", plugin_name) != 1) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }

    /* Get plugin ID by name */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name(plugin_name, &plugin_id);
    if (err != ESP_OK) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Plugin not found\"}");
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_PAUSE] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_PAUSE;

    /* Send PAUSE command via plugin system */
    err = plugin_system_handle_plugin_command(cmd_data, sizeof(cmd_data));
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

/* API: POST /api/plugin/reset - Reset plugin state */
static esp_err_t api_plugin_reset_handler(httpd_req_t *req)
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

    /* Parse JSON: {"name": "effect_strobe"} or {"name": "effect_fade"} or {"name": "sequence"} */
    char plugin_name[64] = {0};
    if (sscanf(content, "{\"name\":\"%63[^\"]\"}", plugin_name) != 1) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }

    /* Get plugin ID by name */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name(plugin_name, &plugin_id);
    if (err != ESP_OK) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Plugin not found\"}");
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_RESET] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_RESET;

    /* Send RESET command via plugin system */
    err = plugin_system_handle_plugin_command(cmd_data, sizeof(cmd_data));
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

/* GET / - Serves simple HTML page with plugin selection and control */
static esp_err_t index_handler(httpd_req_t *req)
{
    ESP_LOGI(WEB_TAG, "index_handler called - URI: %s", req->uri);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, simple_html_page, HTTPD_RESP_USE_STRLEN);
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

    // #region agent log
    esp_netif_t *netif_sta = mesh_common_get_netif_sta();
    esp_netif_ip_info_t ip_info;
    esp_err_t ip_info_err = ESP_FAIL;
    bool netif_up = false;
    if (netif_sta != NULL) {
        ip_info_err = esp_netif_get_ip_info(netif_sta, &ip_info);
        netif_up = esp_netif_is_netif_up(netif_sta);
    }
    ESP_LOGI(WEB_TAG, "[DEBUG HYP-1] Before httpd_start - netif_sta:%p, netif_up:%d, ip_info_err:0x%x, ip:" IPSTR,
             netif_sta, netif_up ? 1 : 0, ip_info_err, IP2STR(&ip_info.ip));
    // #endregion

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 30;  /* Updated: nodes, color_get, color_post, sequence_post, sequence_pointer, sequence_start, sequence_stop, sequence_reset, sequence_status, ota_download, ota_status, ota_cancel, ota_version, ota_distribute, ota_distribution_status, ota_distribution_progress, ota_distribution_cancel, ota_reboot, plugin_activate, plugin_deactivate, plugin_active, plugin_stop, plugin_pause, plugin_reset, plugins_list, index = 26 handlers (30 for future expansion) */
    config.stack_size = 8192;
    config.server_port = 80;
    config.max_open_sockets = 4;  /* Reduced to 4 (3 internal + 1 connection) to leave sockets for UDP listeners and mDNS */
    config.lru_purge_enable = true;  /* Enable automatic cleanup of closed connections to prevent resource leaks */

    // #region agent log
    ESP_LOGI(WEB_TAG, "[DEBUG HYP-2] HTTP config - max_uri_handlers:%d, stack_size:%d, port:%d, max_open_sockets:%d, backlog_conn:%d",
             config.max_uri_handlers, config.stack_size, config.server_port, config.max_open_sockets, config.backlog_conn);
    // #endregion

    ESP_LOGI(WEB_TAG, "Starting web server on port %d", config.server_port);

    esp_err_t httpd_start_err = httpd_start(&server_handle, &config);
    // #region agent log
    ESP_LOGI(WEB_TAG, "[DEBUG HYP-3] httpd_start result:0x%x, server_handle:%p", httpd_start_err, server_handle);
    if (netif_sta != NULL) {
        ip_info_err = esp_netif_get_ip_info(netif_sta, &ip_info);
        netif_up = esp_netif_is_netif_up(netif_sta);
        size_t free_heap = esp_get_free_heap_size();
        ESP_LOGI(WEB_TAG, "[DEBUG HYP-3] After httpd_start - netif_up:%d, ip_info_err:0x%x, ip:" IPSTR ", free_heap:%zu",
                 netif_up ? 1 : 0, ip_info_err, IP2STR(&ip_info.ip), free_heap);
    }
    // #endregion

    if (httpd_start_err == ESP_OK) {
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

        httpd_uri_t plugin_stop_uri = {
            .uri       = "/api/plugin/stop",
            .method    = HTTP_POST,
            .handler   = api_plugin_stop_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_stop_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register plugin stop URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t plugin_pause_uri = {
            .uri       = "/api/plugin/pause",
            .method    = HTTP_POST,
            .handler   = api_plugin_pause_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_pause_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register plugin pause URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t plugin_reset_uri = {
            .uri       = "/api/plugin/reset",
            .method    = HTTP_POST,
            .handler   = api_plugin_reset_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_reset_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register plugin reset URI: 0x%x", reg_err);
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

        // #region agent log
        /* Start periodic diagnostic task to monitor network state */
        static bool diagnostic_task_started = false;
        if (!diagnostic_task_started) {
            diagnostic_task_started = true;
            xTaskCreate(mesh_web_diagnostic_task, "web_diag", 2048, NULL, 1, NULL);
        }
        // #endregion

        return ESP_OK;
    }

    ESP_LOGE(WEB_TAG, "Error starting web server");
    return ESP_FAIL;
}

/* Forward declaration */
static void mesh_web_diagnostic_task(void *pvParameters);

/**
 * @brief Diagnostic task to monitor network interface and HTTP server state
 *
 * This task periodically checks network interface state and logs diagnostic
 * information to help identify connection issues.
 */
static void mesh_web_diagnostic_task(void *pvParameters)
{
    ESP_LOGI(WEB_TAG, "[DIAG] Diagnostic task started");

    while (server_handle != NULL && esp_mesh_is_root()) {
        // #region agent log
        esp_netif_t *netif_sta = mesh_common_get_netif_sta();
        if (netif_sta != NULL) {
            esp_netif_ip_info_t ip_info;
            esp_err_t ip_err = esp_netif_get_ip_info(netif_sta, &ip_info);
            bool is_up = esp_netif_is_netif_up(netif_sta);

            ESP_LOGI(WEB_TAG, "[DIAG] Network state - netif_sta:%p, is_up:%d, ip_err:0x%x, ip:" IPSTR,
                     netif_sta, is_up ? 1 : 0, ip_err, IP2STR(&ip_info.ip));
        } else {
            ESP_LOGW(WEB_TAG, "[DIAG] Network state - netif_sta is NULL");
        }

        if (server_handle != NULL) {
            size_t free_heap = esp_get_free_heap_size();
            ESP_LOGI(WEB_TAG, "[DIAG] Server state - handle:%p, free_heap:%zu bytes",
                     server_handle, free_heap);
        }
        // #endregion

        vTaskDelay(pdMS_TO_TICKS(5000)); /* Check every 5 seconds */
    }

    ESP_LOGI(WEB_TAG, "[DIAG] Diagnostic task exiting");
    vTaskDelete(NULL);
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

