#include "mesh_web.h"
#include "light_neopixel.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include <string.h>
#include <stdlib.h>

static const char *WEB_TAG = "mesh_web";
static httpd_handle_t server_handle = NULL;

/* Forward declarations */
static esp_err_t api_heartbeat_handler(httpd_req_t *req);
static esp_err_t api_nodes_handler(httpd_req_t *req);
static esp_err_t api_color_get_handler(httpd_req_t *req);
static esp_err_t api_color_post_handler(httpd_req_t *req);
static esp_err_t index_handler(httpd_req_t *req);

/* HTML page with embedded CSS and JavaScript */
static const char html_page[] =
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
"<title>Mesh Network Control</title>"
"<style>"
"* { margin: 0; padding: 0; box-sizing: border-box; }"
"body {"
"  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;"
"  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
"  min-height: 100vh;"
"  display: flex;"
"  align-items: center;"
"  justify-content: center;"
"  padding: 20px;"
"}"
".container {"
"  background: white;"
"  border-radius: 20px;"
"  box-shadow: 0 20px 60px rgba(0,0,0,0.3);"
"  padding: 40px;"
"  max-width: 500px;"
"  width: 100%;"
"}"
"h1 {"
"  color: #333;"
"  margin-bottom: 30px;"
"  text-align: center;"
"  font-size: 28px;"
"}"
".node-count {"
"  text-align: center;"
"  margin-bottom: 30px;"
"}"
".node-count-label {"
"  font-size: 14px;"
"  color: #666;"
"  margin-bottom: 8px;"
"}"
".node-count-value {"
"  font-size: 48px;"
"  font-weight: bold;"
"  color: #667eea;"
"  font-family: 'Courier New', monospace;"
"}"
".heartbeat {"
"  text-align: center;"
"  margin-bottom: 30px;"
"}"
".heartbeat-label {"
"  font-size: 14px;"
"  color: #666;"
"  margin-bottom: 8px;"
"}"
".heartbeat-value {"
"  font-size: 48px;"
"  font-weight: bold;"
"  color: #667eea;"
"  font-family: 'Courier New', monospace;"
"}"
".color-section {"
"  text-align: center;"
"  position: relative;"
"}"
".color-label {"
"  font-size: 14px;"
"  color: #666;"
"  margin-bottom: 12px;"
"}"
".color-box {"
"  width: 200px;"
"  height: 200px;"
"  margin: 0 auto 20px;"
"  border-radius: 12px;"
"  border: 4px solid #ddd;"
"  cursor: pointer;"
"  transition: transform 0.2s, box-shadow 0.2s;"
"  box-shadow: 0 4px 12px rgba(0,0,0,0.15);"
"}"
".color-box:hover {"
"  transform: scale(1.05);"
"  box-shadow: 0 6px 20px rgba(0,0,0,0.25);"
"}"
".color-box:active {"
"  transform: scale(0.98);"
"}"
".color-picker-container {"
"  display: none;"
"  margin-top: 20px;"
"}"
".color-picker-container.show {"
"  display: block;"
"}"
"input[type=\"color\"] {"
"  width: 100%;"
"  height: 60px;"
"  border: none;"
"  border-radius: 8px;"
"  cursor: pointer;"
"}"
"#colorPicker {"
"  position: absolute;"
"  opacity: 0;"
"  width: 200px;"
"  height: 200px;"
"  top: 28px;"
"  left: 50%;"
"  margin-left: -100px;"
"  cursor: pointer;"
"  pointer-events: auto;"
"  z-index: 10;"
"}"
".apply-btn {"
"  display: none;"
"  width: 100%;"
"  padding: 15px;"
"  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
"  color: white;"
"  border: none;"
"  border-radius: 8px;"
"  font-size: 16px;"
"  font-weight: bold;"
"  cursor: pointer;"
"  transition: transform 0.2s, box-shadow 0.2s;"
"  box-shadow: 0 4px 12px rgba(102, 126, 234, 0.4);"
"}"
".apply-btn.show {"
"  display: block;"
"}"
".apply-btn:hover {"
"  transform: translateY(-2px);"
"  box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6);"
"}"
".apply-btn:active {"
"  transform: translateY(0);"
"}"
".error {"
"  color: #e74c3c;"
"  margin-top: 10px;"
"  font-size: 14px;"
"  text-align: center;"
"}"
"@media (max-width: 600px) {"
"  .container { padding: 20px; }"
"  .color-box { width: 150px; height: 150px; }"
"}"
"</style>"
"</head>"
"<body>"
"<div class=\"container\">"
"<h1>Mesh Network Control</h1>"
"<div class=\"node-count\">"
"<div class=\"node-count-label\">Node Count</div>"
"<div class=\"node-count-value\" id=\"nodeCount\">0</div>"
"</div>"
"<div class=\"heartbeat\">"
"<div class=\"heartbeat-label\">Heartbeats</div>"
"<div class=\"heartbeat-value\" id=\"heartbeat\">0</div>"
"</div>"
"<div class=\"color-section\">"
"<div class=\"color-label\">Current Color</div>"
"<div class=\"color-box\" id=\"colorBox\" style=\"background-color: rgb(0, 0, 155);\"></div>"
"<input type=\"color\" id=\"colorPicker\" value=\"#00009b\">"
"<div class=\"color-picker-container\" id=\"colorPickerContainer\">"
"<button class=\"apply-btn\" id=\"applyBtn\">Apply Color</button>"
"<div class=\"error\" id=\"error\"></div>"
"</div>"
"</div>"
"</div>"
"<script>"
"let currentR = 0, currentG = 0, currentB = 155;"
"let selectedR = 0, selectedG = 0, selectedB = 155;"
"let updateInterval;"
""
"function updateNodeCount() {"
"  fetch('/api/nodes')"
"    .then(response => response.json())"
"    .then(data => {"
"      document.getElementById('nodeCount').textContent = data.nodes;"
"    })"
"    .catch(err => {"
"      console.error('Node count update error:', err);"
"    });"
"}"
""
"function rgbToHex(r, g, b) {"
"  return '#' + [r, g, b].map(x => {"
"    const hex = x.toString(16);"
"    return hex.length === 1 ? '0' + hex : hex;"
"  }).join('');"
"}"
""
"function hexToRgb(hex) {"
"  const result = /^#?([a-f\\d]{2})([a-f\\d]{2})([a-f\\d]{2})$/i.exec(hex);"
"  return result ? {"
"    r: parseInt(result[1], 16),"
"    g: parseInt(result[2], 16),"
"    b: parseInt(result[3], 16)"
"  } : null;"
"}"
""
"function updateHeartbeat() {"
"  fetch('/api/heartbeat')"
"    .then(response => response.json())"
"    .then(data => {"
"      document.getElementById('heartbeat').textContent = data.heartbeat;"
"    })"
"    .catch(err => {"
"      console.error('Heartbeat update error:', err);"
"    });"
"}"
""
"function loadColor() {"
"  fetch('/api/color')"
"    .then(response => response.json())"
"    .then(data => {"
"      currentR = data.r;"
"      currentG = data.g;"
"      currentB = data.b;"
"      const colorBox = document.getElementById('colorBox');"
"      colorBox.style.backgroundColor = 'rgb(' + currentR + ', ' + currentG + ', ' + currentB + ')';"
"      const colorPicker = document.getElementById('colorPicker');"
"      colorPicker.value = rgbToHex(currentR, currentG, currentB);"
"      selectedR = currentR;"
"      selectedG = currentG;"
"      selectedB = currentB;"
"    })"
"    .catch(err => {"
"      console.error('Color load error:', err);"
"    });"
"}"
""
"function applyColor() {"
"  const errorDiv = document.getElementById('error');"
"  errorDiv.textContent = '';"
""
"  const data = {"
"    r: selectedR,"
"    g: selectedG,"
"    b: selectedB"
"  };"
""
"  fetch('/api/color', {"
"    method: 'POST',"
"    headers: {"
"      'Content-Type': 'application/json'"
"    },"
"    body: JSON.stringify(data)"
"  })"
"  .then(response => response.json())"
"  .then(result => {"
"    if (result.success) {"
"      currentR = selectedR;"
"      currentG = selectedG;"
"      currentB = selectedB;"
"      const colorBox = document.getElementById('colorBox');"
"      colorBox.style.backgroundColor = 'rgb(' + currentR + ', ' + currentG + ', ' + currentB + ')';"
"      const colorPickerContainer = document.getElementById('colorPickerContainer');"
"      colorPickerContainer.classList.remove('show');"
"      const applyBtn = document.getElementById('applyBtn');"
"      applyBtn.classList.remove('show');"
"    } else {"
"      errorDiv.textContent = 'Error: ' + (result.error || 'Failed to apply color');"
"    }"
"  })"
"  .catch(err => {"
"    errorDiv.textContent = 'Network error: ' + err.message;"
"    console.error('Apply color error:', err);"
"  });"
"}"
""
"document.addEventListener('DOMContentLoaded', function() {"
"  loadColor();"
"  updateNodeCount();"
"  updateHeartbeat();"
"  updateInterval = setInterval(function() {"
"    updateNodeCount();"
"    updateHeartbeat();"
"  }, 500);"
""
"  const colorBox = document.getElementById('colorBox');"
"  colorBox.addEventListener('click', function() {"
"    const colorPicker = document.getElementById('colorPicker');"
"    colorPicker.click();"
"  });"
""
"  const colorPicker = document.getElementById('colorPicker');"
"  colorPicker.addEventListener('input', function(e) {"
"    const rgb = hexToRgb(e.target.value);"
"    if (rgb) {"
"      selectedR = rgb.r;"
"      selectedG = rgb.g;"
"      selectedB = rgb.b;"
"      const colorBox = document.getElementById('colorBox');"
"      colorBox.style.backgroundColor = 'rgb(' + selectedR + ', ' + selectedG + ', ' + selectedB + ')';"
"      const colorPickerContainer = document.getElementById('colorPickerContainer');"
"      const applyBtn = document.getElementById('applyBtn');"
"      if (!colorPickerContainer.classList.contains('show')) {"
"        colorPickerContainer.classList.add('show');"
"        applyBtn.classList.add('show');"
"      }"
"    }"
"  });"
""
"  const applyBtn = document.getElementById('applyBtn');"
"  applyBtn.addEventListener('click', applyColor);"
"});"
"</script>"
"</body>"
"</html>";

/* API: GET /api/heartbeat - Returns current heartbeat counter */
static esp_err_t api_heartbeat_handler(httpd_req_t *req)
{
    uint32_t heartbeat = mesh_get_heartbeat_count();
    char response[64];
    int len = snprintf(response, sizeof(response), "{\"heartbeat\":%lu}", (unsigned long)heartbeat);

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
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.server_port = 80;

    ESP_LOGI(WEB_TAG, "Starting web server on port %d", config.server_port);

    if (httpd_start(&server_handle, &config) == ESP_OK) {
        esp_err_t reg_err;
        /* Register URI handlers */
        httpd_uri_t heartbeat_uri = {
            .uri       = "/api/heartbeat",
            .method    = HTTP_GET,
            .handler   = api_heartbeat_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &heartbeat_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register heartbeat URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

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

