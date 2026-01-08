#include "mesh_web.h"
#include "light_neopixel.h"
#include "plugin_system.h"
#include "plugin_web_ui.h"
#include "mesh_commands.h"
#include "mesh_ota.h"
#include "mesh_version.h"
#include "mesh_common.h"
#include "mesh_udp_bridge.h"
#include "mesh_root.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <strings.h>  /* for strcasecmp */
#include <stdlib.h>
#include <stdio.h>
#include "lwip/inet.h"

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
"            padding: 20px;\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            align-items: center;\n"
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
"            font-size: 24px;\n"
"            font-weight: normal;\n"
"            cursor: pointer;\n"
"            transition: all 0.3s;\n"
"            text-transform: none;\n"
"            letter-spacing: 0;\n"
"            line-height: 1;\n"
"            display: flex;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
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
"        .btn-stop {\n"
"            background: linear-gradient(135deg, #fa709a 0%, #fee140 100%);\n"
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
"        .tab-navigation {\n"
"            display: flex;\n"
"            flex-direction: row;\n"
"            gap: 8px;\n"
"            margin-bottom: 20px;\n"
"            border-bottom: 2px solid #ddd;\n"
"            padding-bottom: 0;\n"
"        }\n"
"        .tab-button {\n"
"            padding: 12px 24px;\n"
"            border: none;\n"
"            border-bottom: 3px solid transparent;\n"
"            background: transparent;\n"
"            color: #666;\n"
"            font-size: 1em;\n"
"            font-weight: 600;\n"
"            cursor: pointer;\n"
"            transition: all 0.3s;\n"
"            min-height: 44px;\n"
"            position: relative;\n"
"            bottom: -2px;\n"
"        }\n"
"        .tab-button:hover {\n"
"            color: #667eea;\n"
"            background-color: rgba(102, 126, 234, 0.05);\n"
"        }\n"
"        .tab-button.active {\n"
"            color: #667eea;\n"
"            border-bottom-color: #667eea;\n"
"            background-color: transparent;\n"
"        }\n"
"        .tab-button:focus {\n"
"            outline: none;\n"
"            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);\n"
"        }\n"
"        .tab-content {\n"
"            display: none;\n"
"        }\n"
"        .tab-content.active {\n"
"            display: block;\n"
"        }\n"
"        .tab-content h1 {\n"
"            color: #333;\n"
"            margin: 30px 0;\n"
"            text-align: center;\n"
"            font-size: 28px;\n"
"        }\n"
"        h2 {\n"
"            color: #333;\n"
"            margin-bottom: 20px;\n"
"            font-size: 24px;\n"
"        }\n"
"        .external-server-config {\n"
"            margin-top: 20px;\n"
"        }\n"
"        .form-group {\n"
"            margin-bottom: 20px;\n"
"        }\n"
"        .url-input {\n"
"            width: 100%;\n"
"            padding: 12px;\n"
"            border: 2px solid #e0e0e0;\n"
"            border-radius: 8px;\n"
"            font-size: 16px;\n"
"            background: white;\n"
"            transition: border-color 0.3s;\n"
"        }\n"
"        .url-input:hover { border-color: #667eea; }\n"
"        .url-input:focus { outline: none; border-color: #667eea; }\n"
"        .url-input.invalid { border-color: #f5576c; }\n"
"        .validation-message {\n"
"            display: block;\n"
"            margin-top: 5px;\n"
"            font-size: 14px;\n"
"            color: #666;\n"
"        }\n"
"        .validation-message.error {\n"
"            color: #f5576c;\n"
"        }\n"
"        .btn-primary {\n"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
"            color: white;\n"
"        }\n"
"        .btn-secondary {\n"
"            background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);\n"
"            color: white;\n"
"        }\n"
"        .status-indicator {\n"
"            display: flex;\n"
"            align-items: center;\n"
"            gap: 10px;\n"
"            margin-top: 20px;\n"
"            padding: 12px;\n"
"            background: #f8f9fa;\n"
"            border-radius: 8px;\n"
"        }\n"
"        .stage-label {\n"
"            font-weight: 600;\n"
"            color: #666;\n"
"        }\n"
"        .stage-text {\n"
"            color: #666;\n"
"        }\n"
"        .stage-text.stage-active {\n"
"            color: #667eea;\n"
"            font-weight: 600;\n"
"        }\n"
"        .current-config {\n"
"            margin-top: 15px;\n"
"            padding: 12px;\n"
"            background: #f8f9fa;\n"
"            border-radius: 8px;\n"
"        }\n"
"        .ota-version-display {\n"
"            display: flex;\n"
"            align-items: center;\n"
"            gap: 10px;\n"
"        }\n"
"        .ota-version-display label {\n"
"            font-weight: 600;\n"
"            color: #666;\n"
"            margin: 0;\n"
"        }\n"
"        .version-value {\n"
"            color: #333;\n"
"        }\n"
"        .message-area {\n"
"            margin-top: 15px;\n"
"            min-height: 20px;\n"
"            padding: 12px;\n"
"            border-radius: 8px;\n"
"            font-size: 14px;\n"
"        }\n"
"        .message-area.status-message.success {\n"
"            background: #d4edda;\n"
"            color: #155724;\n"
"        }\n"
"        .message-area.status-message.error {\n"
"            background: #f8d7da;\n"
"            color: #721c24;\n"
"        }\n"
"        .limited-mode-hidden {\n"
"            display: none !important;\n"
"        }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"tab-navigation\" role=\"tablist\">\n"
"        <button class=\"tab-button active\" data-tab=\"plugins\" role=\"tab\" aria-selected=\"true\" aria-controls=\"tab-panel-plugins\" id=\"tab-button-plugins\">Plugins</button>\n"
"        <button class=\"tab-button\" data-tab=\"info\" role=\"tab\" aria-selected=\"false\" aria-controls=\"tab-panel-info\" id=\"tab-button-info\">Info</button>\n"
"        <button class=\"tab-button\" data-tab=\"config\" role=\"tab\" aria-selected=\"false\" aria-controls=\"tab-panel-config\" id=\"tab-button-config\">Config</button>\n"
"    </div>\n"
"    <div class=\"tab-content active\" data-tab=\"plugins\" role=\"tabpanel\" id=\"tab-panel-plugins\" aria-labelledby=\"tab-button-plugins\">\n"
"        <div class=\"container\">\n"
"        <h1>Plugin Control</h1>\n"
"        <div class=\"plugin-select\">\n"
"            <label for=\"pluginSelect\">Select Plugin:</label>\n"
"            <select id=\"pluginSelect\"></select>\n"
"        </div>\n"
"        <div class=\"button-group\">\n"
"            <button class=\"btn-play\" id=\"playBtn\" onclick=\"handlePlay()\" aria-label=\"Play\">▶</button>\n"
"            <button class=\"btn-pause\" id=\"pauseBtn\" onclick=\"handlePause()\" aria-label=\"Pause\">⏸</button>\n"
"            <button class=\"btn-rewind\" id=\"rewindBtn\" onclick=\"handleRewind()\" aria-label=\"Rewind\">⏪</button>\n"
"            <button class=\"btn-stop\" id=\"stopBtn\" onclick=\"handleStop()\" aria-label=\"Stop\">⏹</button>\n"
"        </div>\n"
"        <div class=\"active-plugin\" id=\"activePlugin\">No active plugin</div>\n"
"        <div class=\"status\" id=\"status\" style=\"display: none;\"></div>\n"
"    </div>\n"
"    </div>\n"
"    <div class=\"tab-content\" data-tab=\"info\" role=\"tabpanel\" id=\"tab-panel-info\" aria-labelledby=\"tab-button-info\" hidden>\n"
"        <h1>Info</h1>\n"
"    </div>\n"
"    <div class=\"tab-content\" data-tab=\"config\" role=\"tabpanel\" id=\"tab-panel-config\" aria-labelledby=\"tab-button-config\" hidden>\n"
"        <div class=\"container\">\n"
"            <h2>External Web Server Configuration</h2>\n"
"            <div class=\"external-server-config\">\n"
"                <div class=\"form-group\">\n"
"                    <label for=\"external-server-ip\">Server IP or Hostname:</label>\n"
"                    <input type=\"text\" id=\"external-server-ip\" name=\"ip\" class=\"url-input\"\n"
"                           placeholder=\"192.168.1.100 or example.com\"\n"
"                           aria-label=\"External server IP or hostname\">\n"
"                    <span id=\"external-server-ip-validation\" class=\"validation-message\" aria-live=\"polite\"></span>\n"
"                </div>\n"
"                <div class=\"form-group\">\n"
"                    <label for=\"external-server-port\">Port:</label>\n"
"                    <input type=\"number\" id=\"external-server-port\" name=\"port\" class=\"url-input\"\n"
"                           min=\"1\" max=\"65535\" value=\"8082\"\n"
"                           aria-label=\"External server UDP port\">\n"
"                    <span id=\"external-server-port-validation\" class=\"validation-message\" aria-live=\"polite\"></span>\n"
"                </div>\n"
"                <div class=\"form-group\">\n"
"                    <label for=\"external-server-onboard-only\" style=\"display: flex; align-items: center; gap: 8px;\">\n"
"                        <input type=\"checkbox\" id=\"external-server-onboard-only\" name=\"onboard_only\"\n"
"                               aria-label=\"Onboard HTTP Only - Disable external server functionality\">\n"
"                        <span>Onboard HTTP Only (Disable external server functionality)</span>\n"
"                    </label>\n"
"                </div>\n"
"                <div class=\"button-group\">\n"
"                    <button id=\"external-server-save\" type=\"button\" class=\"btn btn-primary\" aria-label=\"Save external server configuration\">Save</button>\n"
"                    <button id=\"external-server-clear\" type=\"button\" class=\"btn btn-secondary\" aria-label=\"Clear external server configuration\">Clear</button>\n"
"                </div>\n"
"                <div id=\"external-server-status\" class=\"status-indicator\" style=\"display: none;\">\n"
"                    <span class=\"stage-label\">Status:</span>\n"
"                    <span id=\"external-server-status-text\" class=\"stage-text\">Not configured</span>\n"
"                </div>\n"
"                <div id=\"external-server-current\" class=\"current-config\" style=\"display: none;\">\n"
"                    <div class=\"ota-version-display\">\n"
"                        <label>Current Configuration:</label>\n"
"                        <span id=\"external-server-current-value\" class=\"version-value\">Not configured</span>\n"
"                    </div>\n"
"                </div>\n"
"                <div id=\"external-server-message\" class=\"message-area\" aria-live=\"polite\" aria-atomic=\"true\"></div>\n"
"            </div>\n"
"        </div>\n"
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
"                if (!select) {\n"
"                    console.error('pluginSelect element not found');\n"
"                    return;\n"
"                }\n"
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
"                if (!activeDiv) {\n"
"                    console.error('activePlugin element not found');\n"
"                    return;\n"
"                }\n"
"                if (activePlugin) {\n"
"                    activeDiv.innerHTML = '';\n"
"                    const strong = document.createElement('strong');\n"
"                    strong.textContent = 'Active: ';\n"
"                    activeDiv.appendChild(strong);\n"
"                    activeDiv.appendChild(document.createTextNode(activePlugin));\n"
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
"            if (!select) {\n"
"                console.error('pluginSelect element not found');\n"
"                showStatus('Error: plugin selector not found', 'error');\n"
"                return;\n"
"            }\n"
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
"                    setTimeout(() => loadActivePlugin(), 1000);\n"
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
"            if (!select) {\n"
"                console.error('pluginSelect element not found');\n"
"                showStatus('Error: plugin selector not found', 'error');\n"
"                return;\n"
"            }\n"
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
"                    setTimeout(() => loadActivePlugin(), 1000);\n"
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
"            if (!select) {\n"
"                console.error('pluginSelect element not found');\n"
"                showStatus('Error: plugin selector not found', 'error');\n"
"                return;\n"
"            }\n"
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
"                    setTimeout(() => loadActivePlugin(), 1000);\n"
"                } else {\n"
"                    showStatus('Error: ' + (data.error || 'Reset failed'), 'error');\n"
"                }\n"
"            } catch (error) {\n"
"                showStatus('Error: ' + error.message, 'error');\n"
"            }\n"
"        }\n"
"\n"
"        async function handleStop() {\n"
"            const select = document.getElementById('pluginSelect');\n"
"            if (!select) {\n"
"                console.error('pluginSelect element not found');\n"
"                showStatus('Error: plugin selector not found', 'error');\n"
"                return;\n"
"            }\n"
"            const pluginName = select.value;\n"
"            if (!pluginName) {\n"
"                showStatus('Please select a plugin first', 'error');\n"
"                return;\n"
"            }\n"
"            try {\n"
"                const response = await fetch('/api/plugin/stop', {\n"
"                    method: 'POST',\n"
"                    headers: { 'Content-Type': 'application/json' },\n"
"                    body: JSON.stringify({ name: pluginName })\n"
"                });\n"
"                const data = await response.json();\n"
"                if (data.success) {\n"
"                    showStatus('Plugin stopped: ' + pluginName, 'info');\n"
"                    setTimeout(() => loadActivePlugin(), 1000);\n"
"                } else {\n"
"                    showStatus('Error: ' + (data.error || 'Stop failed'), 'error');\n"
"                }\n"
"            } catch (error) {\n"
"                showStatus('Error: ' + error.message, 'error');\n"
"            }\n"
"        }\n"
"\n"
"        function showStatus(message, type) {\n"
"            const statusDiv = document.getElementById('status');\n"
"            if (!statusDiv) {\n"
"                console.error('status element not found');\n"
"                return;\n"
"            }\n"
"            statusDiv.textContent = message;\n"
"            statusDiv.className = 'status ' + type;\n"
"            statusDiv.style.display = 'flex';\n"
"            setTimeout(() => {\n"
"                statusDiv.style.display = 'none';\n"
"            }, 3000);\n"
"        }\n"
"\n"
"        function switchTab(tabName) {\n"
"            if (!tabName) {\n"
"                console.warn('switchTab called without tab name');\n"
"                return;\n"
"            }\n"
"\n"
"            const allTabButtons = document.querySelectorAll('.tab-button');\n"
"            allTabButtons.forEach(button => {\n"
"                button.classList.remove('active');\n"
"                button.setAttribute('aria-selected', 'false');\n"
"            });\n"
"\n"
"            const allTabContent = document.querySelectorAll('.tab-content');\n"
"            allTabContent.forEach(content => {\n"
"                content.classList.remove('active');\n"
"                content.setAttribute('hidden', '');\n"
"            });\n"
"\n"
"            const clickedButton = document.querySelector('.tab-button[data-tab=\"' + tabName + '\"]');\n"
"            if (clickedButton) {\n"
"                clickedButton.classList.add('active');\n"
"                clickedButton.setAttribute('aria-selected', 'true');\n"
"            } else {\n"
"                console.warn('Tab button not found for tab: ' + tabName);\n"
"            }\n"
"\n"
"            const targetContent = document.querySelector('.tab-content[data-tab=\"' + tabName + '\"]');\n"
"            if (targetContent) {\n"
"                targetContent.classList.add('active');\n"
"                targetContent.removeAttribute('hidden');\n"
"            } else {\n"
"                console.warn('Tab content not found for tab: ' + tabName);\n"
"            }\n"
"        }\n"
"\n"
"        document.addEventListener('DOMContentLoaded', function() {\n"
"            const tabButtons = document.querySelectorAll('.tab-button');\n"
"            tabButtons.forEach(button => {\n"
"                button.addEventListener('click', function() {\n"
"                    const tabName = this.getAttribute('data-tab');\n"
"                    if (tabName) {\n"
"                        switchTab(tabName);\n"
"                    }\n"
"                });\n"
"                button.addEventListener('keydown', function(e) {\n"
"                    if (e.key === 'Enter' || e.key === ' ') {\n"
"                        e.preventDefault();\n"
"                        const tabName = this.getAttribute('data-tab');\n"
"                        if (tabName) {\n"
"                            switchTab(tabName);\n"
"                        }\n"
"                    } else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {\n"
"                        e.preventDefault();\n"
"                        const currentIndex = Array.from(tabButtons).indexOf(this);\n"
"                        let targetIndex;\n"
"                        if (e.key === 'ArrowLeft') {\n"
"                            targetIndex = currentIndex > 0 ? currentIndex - 1 : tabButtons.length - 1;\n"
"                        } else {\n"
"                            targetIndex = currentIndex < tabButtons.length - 1 ? currentIndex + 1 : 0;\n"
"                        }\n"
"                        const targetButton = tabButtons[targetIndex];\n"
"                        if (!targetButton) {\n"
"                            console.warn('Target button not found at index: ' + targetIndex);\n"
"                            return;\n"
"                        }\n"
"                        const tabName = targetButton.getAttribute('data-tab');\n"
"                        if (tabName) {\n"
"                            switchTab(tabName);\n"
"                            targetButton.focus();\n"
"                        }\n"
"                    }\n"
"                });\n"
"            });\n"
"\n"
"            switchTab('plugins');\n"
"\n"
"            // Load plugins and active plugin on page load\n"
"            loadPlugins();\n"
"            loadActivePlugin();\n"
"            // Poll for active plugin changes every 5 seconds\n"
"            setInterval(loadActivePlugin, 5000);\n"
"\n"
"            // Initialize external server configuration\n"
"            initializeExternalServerConfig();\n"
"        });\n"
"\n"
"        function validateIpOrHostname(value) {\n"
"            if (!value || value.trim().length === 0) {\n"
"                return { valid: false, message: 'IP/hostname cannot be empty' };\n"
"            }\n"
"            if (value.length > 253) {\n"
"                return { valid: false, message: 'IP/hostname is too long (max 253 characters)' };\n"
"            }\n"
"            return { valid: true, message: '' };\n"
"        }\n"
"\n"
"        function validatePort(value) {\n"
"            const port = parseInt(value, 10);\n"
"            if (isNaN(port) || port < 1 || port > 65535) {\n"
"                return { valid: false, message: 'Port must be between 1 and 65535' };\n"
"            }\n"
"            return { valid: true, message: '' };\n"
"        }\n"
"\n"
"        function updateLimitedModeUI(limitedMode) {\n"
"            const pluginsTabButton = document.querySelector('.tab-button[data-tab=\"plugins\"]');\n"
"            const infoTabButton = document.querySelector('.tab-button[data-tab=\"info\"]');\n"
"            const pluginsTabContent = document.querySelector('.tab-content[data-tab=\"plugins\"]');\n"
"            const infoTabContent = document.querySelector('.tab-content[data-tab=\"info\"]');\n"
"\n"
"            if (limitedMode) {\n"
"                if (pluginsTabButton) pluginsTabButton.classList.add('limited-mode-hidden');\n"
"                if (infoTabButton) infoTabButton.classList.add('limited-mode-hidden');\n"
"                if (pluginsTabContent) pluginsTabContent.classList.add('limited-mode-hidden');\n"
"                if (infoTabContent) infoTabContent.classList.add('limited-mode-hidden');\n"
"\n"
"                const currentTab = document.querySelector('.tab-button.active');\n"
"                if (currentTab && (currentTab.getAttribute('data-tab') === 'plugins' || currentTab.getAttribute('data-tab') === 'info')) {\n"
"                    switchTab('config');\n"
"                }\n"
"            } else {\n"
"                if (pluginsTabButton) pluginsTabButton.classList.remove('limited-mode-hidden');\n"
"                if (infoTabButton) infoTabButton.classList.remove('limited-mode-hidden');\n"
"                if (pluginsTabContent) pluginsTabContent.classList.remove('limited-mode-hidden');\n"
"                if (infoTabContent) infoTabContent.classList.remove('limited-mode-hidden');\n"
"            }\n"
"        }\n"
"\n"
"        async function loadExternalServerConfig() {\n"
"            try {\n"
"                const response = await fetch('/api/settings/external-server');\n"
"                if (!response.ok) {\n"
"                    const errorData = await response.json().catch(() => ({}));\n"
"                    throw new Error(errorData.error || 'HTTP ' + response.status);\n"
"                }\n"
"                const data = await response.json();\n"
"\n"
"                const ipInput = document.getElementById('external-server-ip');\n"
"                const portInput = document.getElementById('external-server-port');\n"
"                const statusDiv = document.getElementById('external-server-status');\n"
"                const statusText = document.getElementById('external-server-status-text');\n"
"                const currentDiv = document.getElementById('external-server-current');\n"
"                const currentValue = document.getElementById('external-server-current-value');\n"
"\n"
"                const onboardOnlyCheckbox = document.getElementById('external-server-onboard-only');\n"
"                if (onboardOnlyCheckbox) {\n"
"                    onboardOnlyCheckbox.checked = data.onboard_only || false;\n"
"                }\n"
"\n"
"                if (data.onboard_only) {\n"
"                    if (ipInput) ipInput.disabled = true;\n"
"                    if (portInput) portInput.disabled = true;\n"
"                    if (statusDiv) statusDiv.style.display = 'flex';\n"
"                    if (statusText) {\n"
"                        statusText.textContent = 'Onboard HTTP Only (External server disabled)';\n"
"                        statusText.className = 'stage-text';\n"
"                    }\n"
"                    if (currentDiv) currentDiv.style.display = 'none';\n"
"                } else if (data.ip && data.port) {\n"
"                    if (ipInput) {\n"
"                        ipInput.value = data.ip;\n"
"                        ipInput.disabled = false;\n"
"                    }\n"
"                    if (portInput) {\n"
"                        portInput.value = data.port;\n"
"                        portInput.disabled = false;\n"
"                    }\n"
"                    if (statusDiv) statusDiv.style.display = 'flex';\n"
"                    if (statusText) {\n"
"                        if (data.server_discovered) {\n"
"                            statusText.textContent = data.limited_mode ? 'Active (Limited Mode)' : 'Configured (Discovered)';\n"
"                        } else {\n"
"                            statusText.textContent = 'Configured (Not Discovered)';\n"
"                        }\n"
"                        statusText.className = data.limited_mode ? 'stage-text stage-active' : 'stage-text';\n"
"                    }\n"
"                    if (currentDiv) currentDiv.style.display = 'block';\n"
"                    if (currentValue) currentValue.textContent = data.ip + ':' + data.port;\n"
"                } else {\n"
"                    if (ipInput) ipInput.disabled = false;\n"
"                    if (portInput) portInput.disabled = false;\n"
"                    if (statusDiv) statusDiv.style.display = 'flex';\n"
"                    if (statusText) {\n"
"                        if (data.server_discovered) {\n"
"                            statusText.textContent = 'Auto-discovered (No manual config)';\n"
"                        } else {\n"
"                            statusText.textContent = 'Not configured';\n"
"                        }\n"
"                        statusText.className = 'stage-text';\n"
"                    }\n"
"                    if (currentDiv) currentDiv.style.display = 'none';\n"
"                }\n"
"\n"
"                updateLimitedModeUI(data.limited_mode || false);\n"
"            } catch (error) {\n"
"                console.error('Failed to load external server configuration:', error);\n"
"                const messageArea = document.getElementById('external-server-message');\n"
"                if (messageArea) {\n"
"                    messageArea.textContent = 'Failed to load configuration: ' + error.message;\n"
"                    messageArea.className = 'message-area status-message error';\n"
"                    messageArea.style.display = 'block';\n"
"                }\n"
"            }\n"
"        }\n"
"\n"
"        function showExternalServerMessage(message, type) {\n"
"            const messageArea = document.getElementById('external-server-message');\n"
"            if (!messageArea) return;\n"
"\n"
"            messageArea.textContent = message;\n"
"            messageArea.className = 'message-area status-message ' + type;\n"
"            messageArea.style.display = 'block';\n"
"\n"
"            if (type === 'success') {\n"
"                setTimeout(function() {\n"
"                    if (messageArea.className.includes('success')) {\n"
"                        messageArea.style.display = 'none';\n"
"                    }\n"
"                }, 5000);\n"
"            }\n"
"        }\n"
"\n"
"        async function handleExternalServerSave() {\n"
"            const ipInput = document.getElementById('external-server-ip');\n"
"            const portInput = document.getElementById('external-server-port');\n"
"            const onboardOnlyCheckbox = document.getElementById('external-server-onboard-only');\n"
"            const saveButton = document.getElementById('external-server-save');\n"
"            const ipValidation = document.getElementById('external-server-ip-validation');\n"
"            const portValidation = document.getElementById('external-server-port-validation');\n"
"\n"
"            if (!saveButton) {\n"
"                console.error('External server form elements not found');\n"
"                return;\n"
"            }\n"
"\n"
"            if (ipValidation) {\n"
"                ipValidation.textContent = '';\n"
"                ipValidation.className = 'validation-message';\n"
"            }\n"
"            if (portValidation) {\n"
"                portValidation.textContent = '';\n"
"                portValidation.className = 'validation-message';\n"
"            }\n"
"\n"
"            const onboardOnly = onboardOnlyCheckbox ? onboardOnlyCheckbox.checked : false;\n"
"\n"
"            if (!onboardOnly) {\n"
"                if (!ipInput || !portInput) {\n"
"                    console.error('External server form elements not found');\n"
"                    return;\n"
"                }\n"
"\n"
"                const ipValidationResult = validateIpOrHostname(ipInput.value);\n"
"                if (!ipValidationResult.valid) {\n"
"                    if (ipValidation) {\n"
"                        ipValidation.textContent = ipValidationResult.message;\n"
"                        ipValidation.className = 'validation-message error';\n"
"                    }\n"
"                    if (ipInput) ipInput.classList.add('invalid');\n"
"                    return;\n"
"                }\n"
"                if (ipInput) ipInput.classList.remove('invalid');\n"
"\n"
"                const portValidationResult = validatePort(portInput.value);\n"
"                if (!portValidationResult.valid) {\n"
"                    if (portValidation) {\n"
"                        portValidation.textContent = portValidationResult.message;\n"
"                        portValidation.className = 'validation-message error';\n"
"                    }\n"
"                    if (portInput) portInput.classList.add('invalid');\n"
"                    return;\n"
"                }\n"
"                if (portInput) portInput.classList.remove('invalid');\n"
"            }\n"
"\n"
"            saveButton.disabled = true;\n"
"            saveButton.textContent = 'Saving...';\n"
"\n"
"            try {\n"
"                const requestBody = { onboard_only: onboardOnly };\n"
"                if (!onboardOnly && ipInput && portInput) {\n"
"                    requestBody.ip = ipInput.value.trim();\n"
"                    requestBody.port = parseInt(portInput.value, 10);\n"
"                }\n"
"\n"
"                const response = await fetch('/api/settings/external-server', {\n"
"                    method: 'POST',\n"
"                    headers: { 'Content-Type': 'application/json' },\n"
"                    body: JSON.stringify(requestBody)\n"
"                });\n"
"\n"
"                if (!response.ok) {\n"
"                    const errorData = await response.json().catch(function() { return {}; });\n"
"                    throw new Error(errorData.error || 'HTTP error: ' + response.status);\n"
"                }\n"
"\n"
"                const data = await response.json();\n"
"                if (!data.success) {\n"
"                    throw new Error(data.error || 'Failed to save configuration');\n"
"                }\n"
"\n"
"                showExternalServerMessage('Configuration saved successfully. Limited mode: ' + (data.limited_mode ? 'Active' : 'Inactive'), 'success');\n"
"                await loadExternalServerConfig();\n"
"            } catch (error) {\n"
"                console.error('Failed to save external server configuration:', error);\n"
"                showExternalServerMessage('Failed to save configuration: ' + error.message, 'error');\n"
"            } finally {\n"
"                saveButton.disabled = false;\n"
"                saveButton.textContent = 'Save';\n"
"            }\n"
"        }\n"
"\n"
"        async function handleExternalServerClear() {\n"
"            const clearButton = document.getElementById('external-server-clear');\n"
"            if (!clearButton) {\n"
"                console.error('Clear button not found');\n"
"                return;\n"
"            }\n"
"\n"
"            if (!confirm('Are you sure you want to clear the external server configuration?')) {\n"
"                return;\n"
"            }\n"
"\n"
"            clearButton.disabled = true;\n"
"            clearButton.textContent = 'Clearing...';\n"
"\n"
"            try {\n"
"                const response = await fetch('/api/settings/external-server', {\n"
"                    method: 'DELETE'\n"
"                });\n"
"\n"
"                if (!response.ok) {\n"
"                    const errorData = await response.json().catch(function() { return {}; });\n"
"                    throw new Error(errorData.error || 'HTTP error: ' + response.status);\n"
"                }\n"
"\n"
"                const data = await response.json();\n"
"                if (!data.success) {\n"
"                    throw new Error(data.error || 'Failed to clear configuration');\n"
"                }\n"
"\n"
"                showExternalServerMessage('Configuration cleared successfully', 'success');\n"
"\n"
"                const ipInput = document.getElementById('external-server-ip');\n"
"                const portInput = document.getElementById('external-server-port');\n"
"                if (ipInput) ipInput.value = '';\n"
"                if (portInput) portInput.value = '8082';\n"
"\n"
"                await loadExternalServerConfig();\n"
"            } catch (error) {\n"
"                console.error('Failed to clear external server configuration:', error);\n"
"                showExternalServerMessage('Failed to clear configuration: ' + error.message, 'error');\n"
"            } finally {\n"
"                clearButton.disabled = false;\n"
"                clearButton.textContent = 'Clear';\n"
"            }\n"
"        }\n"
"\n"
"        function initializeExternalServerConfig() {\n"
"            loadExternalServerConfig();\n"
"\n"
"            const saveButton = document.getElementById('external-server-save');\n"
"            const clearButton = document.getElementById('external-server-clear');\n"
"            const ipInput = document.getElementById('external-server-ip');\n"
"            const portInput = document.getElementById('external-server-port');\n"
"\n"
"            if (saveButton) {\n"
"                saveButton.addEventListener('click', handleExternalServerSave);\n"
"            }\n"
"\n"
"            if (clearButton) {\n"
"                clearButton.addEventListener('click', handleExternalServerClear);\n"
"            }\n"
"\n"
"            if (ipInput) {\n"
"                ipInput.addEventListener('keydown', function(e) {\n"
"                    if (e.key === 'Enter') {\n"
"                        e.preventDefault();\n"
"                        handleExternalServerSave();\n"
"                    }\n"
"                });\n"
"            }\n"
"\n"
"            if (portInput) {\n"
"                portInput.addEventListener('keydown', function(e) {\n"
"                    if (e.key === 'Enter') {\n"
"                        e.preventDefault();\n"
"                        handleExternalServerSave();\n"
"                    }\n"
"                });\n"
"            }\n"
"        }\n"
"    </script>\n"
"</body>\n"
"</html>\n";

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
static esp_err_t api_settings_external_server_get_handler(httpd_req_t *req);
static esp_err_t api_settings_external_server_post_handler(httpd_req_t *req);
static esp_err_t api_settings_external_server_delete_handler(httpd_req_t *req);
bool mesh_web_is_limited_mode(void);
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
static esp_err_t api_plugin_data_handler(httpd_req_t *req);
static esp_err_t api_plugin_bundle_handler(httpd_req_t *req);
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
/* Note: This endpoint is maintained for backward compatibility. It now uses the plugin system API to ensure proper broadcasting. */
static esp_err_t api_sequence_start_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can start sequence\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    /* Use plugin system API to ensure proper broadcasting to child nodes */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name("sequence", &plugin_id);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Sequence plugin not found\"}", -1);
        return ESP_FAIL;
    }

    /* Activate plugin (this handles START command and broadcasting) */
    err = plugin_activate("sequence");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Start failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"success\":true}", -1);
    return ESP_OK;
}

/* API: POST /api/sequence/stop - Stop sequence playback */
/* Note: This endpoint is maintained for backward compatibility. It now uses the plugin system API to ensure proper broadcasting. */
static esp_err_t api_sequence_stop_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can stop sequence\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    /* Use plugin system API to ensure proper broadcasting to child nodes */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name("sequence", &plugin_id);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Sequence plugin not found\"}", -1);
        return ESP_FAIL;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_STOP] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_STOP;

    /* Send STOP command via plugin system (from API - processes locally and broadcasts) */
    err = plugin_system_handle_plugin_command_from_api(cmd_data, sizeof(cmd_data));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Stop failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"success\":true}", -1);
    return ESP_OK;
}

/* API: POST /api/sequence/reset - Reset sequence pointer */
/* Note: This endpoint is maintained for backward compatibility. It now uses the plugin system API to ensure proper broadcasting. */
static esp_err_t api_sequence_reset_handler(httpd_req_t *req)
{
    if (!esp_mesh_is_root()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Only root node can reset sequence\"}", -1);
        return ESP_ERR_INVALID_STATE;
    }

    /* Use plugin system API to ensure proper broadcasting to child nodes */
    uint8_t plugin_id;
    esp_err_t err = plugin_get_id_by_name("sequence", &plugin_id);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Sequence plugin not found\"}", -1);
        return ESP_FAIL;
    }

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_RESET] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_RESET;

    /* Send RESET command via plugin system (from API - processes locally and broadcasts) */
    err = plugin_system_handle_plugin_command_from_api(cmd_data, sizeof(cmd_data));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"Reset failed: %s\"}", esp_err_to_name(err));
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"success\":true}", -1);
    return ESP_OK;
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
        /* Ensure at least one plugin remains active if activation failed (root node only) */
        if (esp_mesh_is_root()) {
            bool has_active_plugin_after_failure = plugin_system_has_active_plugin();
            esp_err_t ensure_err = mesh_root_ensure_active_plugin();
            if (ensure_err != ESP_OK) {
                ESP_LOGW(WEB_TAG, "Failed to ensure active plugin after activation failure: %s",
                         esp_err_to_name(ensure_err));
                /* Continue with error response even if default activation fails */
            } else if (!has_active_plugin_after_failure) {
                /* No plugin was active after failure, so rgb_effect was activated */
                ESP_LOGI(WEB_TAG, "Default plugin 'rgb_effect' activated after activation failure");
            }
        }

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

    /* Ensure at least one plugin remains active (root node only) */
    if (esp_mesh_is_root()) {
        /* Check if any plugin is active after deactivation */
        bool has_active_plugin_after_deactivation = plugin_system_has_active_plugin();
        esp_err_t ensure_err = mesh_root_ensure_active_plugin();
        if (ensure_err != ESP_OK) {
            ESP_LOGW(WEB_TAG, "Failed to ensure active plugin after deactivation: %s",
                     esp_err_to_name(ensure_err));
            /* Continue with deactivation response even if default activation fails */
        } else if (!has_active_plugin_after_deactivation) {
            /* No plugin was active after deactivation, so rgb_effect was activated */
            ESP_LOGI(WEB_TAG, "Default plugin 'rgb_effect' activated after deactivation");
        }
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

    /* Construct plugin command: [PLUGIN_ID] [PLUGIN_CMD_STOP] */
    uint8_t cmd_data[2];
    cmd_data[0] = plugin_id;
    cmd_data[1] = PLUGIN_CMD_STOP;

    /* Send STOP command via plugin system (from API - processes locally and broadcasts) */
    err = plugin_system_handle_plugin_command_from_api(cmd_data, sizeof(cmd_data));
    if (err != ESP_OK) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, error_msg, -1);
        return ESP_FAIL;
    }

    /* Ensure at least one plugin remains active (root node only) */
    if (esp_mesh_is_root()) {
        bool has_active_plugin_after_stop = plugin_system_has_active_plugin();
        esp_err_t ensure_err = mesh_root_ensure_active_plugin();
        if (ensure_err != ESP_OK) {
            ESP_LOGW(WEB_TAG, "Failed to ensure active plugin after stop: %s",
                     esp_err_to_name(ensure_err));
            /* Continue with stop response even if default activation fails */
        } else if (!has_active_plugin_after_stop) {
            /* No plugin was active after stop, so rgb_effect was activated */
            ESP_LOGI(WEB_TAG, "Default plugin 'rgb_effect' activated after stop");
        }
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

    /* Send PAUSE command via plugin system (from API - processes locally and broadcasts) */
    err = plugin_system_handle_plugin_command_from_api(cmd_data, sizeof(cmd_data));
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

    /* Send RESET command via plugin system (from API - processes locally and broadcasts) */
    err = plugin_system_handle_plugin_command_from_api(cmd_data, sizeof(cmd_data));
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

/**
 * @brief Check if LIMITED_MODE is active.
 *
 * LIMITED_MODE is active when both conditions are met:
 * - Manual external server IP is configured in NVS
 * - Registration with external server succeeded
 *
 * @return true if LIMITED_MODE is active, false otherwise
 */
bool mesh_web_is_limited_mode(void)
{
    /* Check if manual IP is configured */
    char manual_ip[64] = {0};
    uint16_t manual_port = 0;
    esp_err_t err = mesh_udp_bridge_get_manual_config(manual_ip, sizeof(manual_ip), &manual_port, NULL, 0);
    if (err != ESP_OK) {
        /* No manual IP configured */
        ESP_LOGD(WEB_TAG, "LIMITED_MODE check: no manual IP configured");
        return false;
    }

    /* Check if registration succeeded */
    bool is_registered = mesh_udp_bridge_is_registered();
    if (!is_registered) {
        /* Manual IP configured but registration not successful */
        ESP_LOGD(WEB_TAG, "LIMITED_MODE check: manual IP configured (%s:%d) but not registered", manual_ip, manual_port);
        return false;
    }

    /* Both conditions met: LIMITED_MODE is active */
    ESP_LOGD(WEB_TAG, "LIMITED_MODE check: active (manual IP: %s:%d, registered: true)", manual_ip, manual_port);
    return true;
}

/* API: GET /api/settings/external-server - Get external server configuration */
static esp_err_t api_settings_external_server_get_handler(httpd_req_t *req)
{
    ESP_LOGD(WEB_TAG, "GET /api/settings/external-server requested");
    char response[512];
    char manual_ip[64] = {0};
    uint16_t manual_port = 0;
    esp_err_t err = mesh_udp_bridge_get_manual_config(manual_ip, sizeof(manual_ip), &manual_port, NULL, 0);

    bool onboard_only = mesh_udp_bridge_is_onboard_only();
    bool manual_ip_set = (err == ESP_OK);
    bool server_discovered = mesh_udp_bridge_is_server_discovered();
    bool limited_mode = mesh_web_is_limited_mode();
    ESP_LOGD(WEB_TAG, "Configuration state: onboard_only=%s, manual_ip_set=%s, server_discovered=%s, limited_mode=%s",
             onboard_only ? "true" : "false", manual_ip_set ? "true" : "false",
             server_discovered ? "true" : "false", limited_mode ? "true" : "false");

    if (manual_ip_set) {
        /* Manual configuration exists */
        int len = snprintf(response, sizeof(response),
                          "{\"ip\":\"%s\",\"port\":%d,\"onboard_only\":%s,\"manual_ip_set\":true,\"server_discovered\":%s,\"limited_mode\":%s}",
                          manual_ip, manual_port, onboard_only ? "true" : "false",
                          server_discovered ? "true" : "false", limited_mode ? "true" : "false");
        if (len < 0 || len >= (int)sizeof(response)) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"error\":\"Response formatting error\"}", -1);
            return ESP_FAIL;
        }
    } else if (err == ESP_ERR_NOT_FOUND) {
        /* No manual configuration */
        int len = snprintf(response, sizeof(response),
                          "{\"ip\":null,\"port\":null,\"onboard_only\":%s,\"manual_ip_set\":false,\"server_discovered\":%s,\"limited_mode\":%s}",
                          onboard_only ? "true" : "false", server_discovered ? "true" : "false", limited_mode ? "true" : "false");
        if (len < 0 || len >= (int)sizeof(response)) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"error\":\"Response formatting error\"}", -1);
            return ESP_FAIL;
        }
    } else {
        /* Error reading configuration */
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"error\":\"Failed to read configuration\"}", -1);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, response, -1);
}

/* API: POST /api/settings/external-server - Set external server configuration */
static esp_err_t api_settings_external_server_post_handler(httpd_req_t *req)
{
    ESP_LOGI(WEB_TAG, "POST /api/settings/external-server requested");
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    /* Check if request body is too large */
    if (ret >= (int)sizeof(content) - 1) {
        /* Check if there's more data */
        char dummy;
        int more = httpd_req_recv(req, &dummy, 1);
        if (more > 0) {
            httpd_resp_set_status(req, "413 Payload Too Large");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"success\":false,\"error\":\"Request body too large\"}", -1);
            return ESP_FAIL;
        }
    }

    content[ret] = '\0';

    /* Simple JSON parsing for {"ip":"...","port":...,"onboard_only":...} */
    char *ip_str = strstr(content, "\"ip\":");
    char *port_str = strstr(content, "\"port\":");
    char *onboard_only_str = strstr(content, "\"onboard_only\":");

    /* Extract onboard_only flag (optional, defaults to false) */
    bool onboard_only = false;
    if (onboard_only_str != NULL) {
        onboard_only_str += 15; /* Skip past "onboard_only": */
        while (*onboard_only_str != '\0' && onboard_only_str < content + ret && *onboard_only_str == ' ') onboard_only_str++;
        if (onboard_only_str < content + ret && *onboard_only_str != '\0') {
            if (strncmp(onboard_only_str, "true", 4) == 0) {
                onboard_only = true;
            } else if (strncmp(onboard_only_str, "false", 5) == 0) {
                onboard_only = false;
            } else {
                /* Invalid boolean value */
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_send(req, "{\"success\":false,\"error\":\"onboard_only must be true or false\"}", -1);
                return ESP_FAIL;
            }
        }
    }

    /* Handle onboard_only option */
    if (onboard_only) {
        /* Clear manual IP configuration */
        esp_err_t clear_err = mesh_udp_bridge_clear_manual_server_ip();
        if (clear_err != ESP_OK && clear_err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(WEB_TAG, "Failed to clear manual IP when setting onboard_only: %s", esp_err_to_name(clear_err));
        }
        /* Set runtime option */
        esp_err_t set_err = mesh_udp_bridge_set_onboard_only(true);
        if (set_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to set onboard_only option: %s", esp_err_to_name(set_err));
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to set onboard_only option\"}", -1);
            return ESP_FAIL;
        }
        ESP_LOGI(WEB_TAG, "ONLY_ONBOARD_HTTP runtime option enabled via web UI");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        return httpd_resp_send(req, "{\"success\":true,\"onboard_only\":true}", -1);
    }

    /* onboard_only is false - handle IP/port configuration */
    /* Clear runtime option */
    esp_err_t clear_opt_err = mesh_udp_bridge_set_onboard_only(false);
    if (clear_opt_err != ESP_OK) {
        ESP_LOGW(WEB_TAG, "Failed to clear onboard_only option: %s", esp_err_to_name(clear_opt_err));
    }

    /* If IP not provided, clear manual IP and return success */
    if (!ip_str || !port_str) {
        /* Clear manual IP configuration */
        esp_err_t clear_err = mesh_udp_bridge_clear_manual_server_ip();
        if (clear_err != ESP_OK && clear_err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(WEB_TAG, "Failed to clear manual IP: %s", esp_err_to_name(clear_err));
        }
        ESP_LOGI(WEB_TAG, "Manual IP configuration cleared via web UI");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        return httpd_resp_send(req, "{\"success\":true,\"onboard_only\":false}", -1);
    }

    /* Extract IP/hostname */
    ip_str += 5; /* Skip past "ip": */
    /* Check bounds after skipping */
    if (ip_str >= content + ret) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }
    while (*ip_str != '\0' && ip_str < content + ret && (*ip_str == ' ' || *ip_str == '"')) ip_str++; /* Skip whitespace and quotes */
    if (ip_str >= content + ret || *ip_str == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }
    char ip_end = (*ip_str == '"') ? '"' : ',';
    char ip_value[64] = {0};
    int ip_idx = 0;
    while (*ip_str != '\0' && ip_str < content + ret && *ip_str != ip_end && ip_idx < (int)sizeof(ip_value) - 1) {
        ip_value[ip_idx++] = *ip_str++;
    }
    ip_value[ip_idx] = '\0';

    /* Extract port */
    port_str += 6; /* Skip past "port": */
    /* Check bounds after skipping */
    if (port_str >= content + ret) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }
    while (*port_str != '\0' && port_str < content + ret && *port_str == ' ') port_str++; /* Skip whitespace */
    if (port_str >= content + ret || *port_str == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid JSON format\"}", -1);
        return ESP_FAIL;
    }

    /* Validate port string is numeric */
    char *port_end = port_str;
    while (port_end < content + ret && *port_end != '\0' && *port_end >= '0' && *port_end <= '9') port_end++;
    if (port_end == port_str || (port_end < content + ret && *port_end != '\0' && *port_end != ' ' && *port_end != ',' && *port_end != '}')) {
        /* Port contains non-numeric characters */
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Port must be a number\"}", -1);
        return ESP_FAIL;
    }

    int port_val = atoi(port_str);

    /* Validate port range */
    if (port_val < 1 || port_val > 65535) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Port must be 1-65535\"}", -1);
        return ESP_FAIL;
    }

    /* Validate IP/hostname is not empty */
    if (strlen(ip_value) == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"IP/hostname cannot be empty\"}", -1);
        return ESP_FAIL;
    }

    /* Resolve hostname to get resolved IP (needed for both connection test and storage) */
    ESP_LOGI(WEB_TAG, "Resolving hostname: %s", ip_value);
    char resolved_ip[16] = {0};
    esp_err_t resolve_err = mesh_udp_bridge_resolve_hostname(ip_value, resolved_ip, sizeof(resolved_ip));
    if (resolve_err != ESP_OK) {
        ESP_LOGW(WEB_TAG, "Failed to resolve hostname '%s': %s", ip_value, esp_err_to_name(resolve_err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to resolve hostname\"}", -1);
        return ESP_FAIL;
    }
    ESP_LOGI(WEB_TAG, "Hostname resolved: %s -> %s", ip_value, resolved_ip);

    /* Test connection using resolved IP */
    ESP_LOGI(WEB_TAG, "Testing connection to %s:%d", resolved_ip, port_val);
    bool test_result = mesh_udp_bridge_test_connection(resolved_ip, (uint16_t)port_val);
    if (!test_result) {
        ESP_LOGW(WEB_TAG, "Connection test failed for %s:%d", resolved_ip, port_val);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Connection test failed\"}", -1);
        return ESP_FAIL;
    }

    ESP_LOGI(WEB_TAG, "Connection test succeeded for %s:%d", resolved_ip, port_val);

    /* Store configuration using already-resolved IP to avoid redundant DNS lookup */
    ESP_LOGI(WEB_TAG, "Storing manual configuration: %s:%d (resolved: %s)", ip_value, port_val, resolved_ip);
    esp_err_t err = mesh_udp_bridge_store_manual_config(ip_value, (uint16_t)port_val, resolved_ip);
    if (err != ESP_OK) {
        ESP_LOGE(WEB_TAG, "Failed to store manual configuration: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to store configuration\"}", -1);
        return ESP_FAIL;
    }

    /* Convert resolved IP to network byte order and set registration */
    struct in_addr addr;
    if (inet_aton(resolved_ip, &addr) != 0) {
        uint8_t ip_bytes[4];
        memcpy(ip_bytes, &addr.s_addr, 4);
        mesh_udp_bridge_set_registration(true, ip_bytes, (uint16_t)port_val);
        ESP_LOGI(WEB_TAG, "Manual server IP set: %s:%d (resolved: %s)", ip_value, port_val, resolved_ip);
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to convert resolved IP\"}", -1);
        return ESP_FAIL;
    }

    /* Attempt registration */
    esp_err_t reg_err = mesh_udp_bridge_register();
    if (reg_err != ESP_OK && reg_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(WEB_TAG, "Registration failed after setting manual IP: %s", esp_err_to_name(reg_err));
        /* Continue anyway - registration might succeed later */
    }

    /* Check if LIMITED_MODE is now active */
    bool limited_mode = mesh_web_is_limited_mode();
    if (limited_mode) {
        ESP_LOGI(WEB_TAG, "LIMITED_MODE entered: external server configured and registered");
    } else {
        ESP_LOGI(WEB_TAG, "LIMITED_MODE not active: configuration stored but registration pending");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char response[256];
    int len = snprintf(response, sizeof(response), "{\"success\":true,\"onboard_only\":false,\"limited_mode\":%s}", limited_mode ? "true" : "false");
    if (len < 0 || len >= (int)sizeof(response)) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"error\":\"Response formatting error\"}", -1);
        return ESP_FAIL;
    }
    return httpd_resp_send(req, response, -1);
}

/* API: DELETE /api/settings/external-server - Clear external server configuration */
static esp_err_t api_settings_external_server_delete_handler(httpd_req_t *req)
{
    ESP_LOGI(WEB_TAG, "DELETE /api/settings/external-server requested");
    /* Clear manual configuration */
    esp_err_t err = mesh_udp_bridge_clear_manual_server_ip();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(WEB_TAG, "Failed to clear manual IP (may not be set): %s", esp_err_to_name(err));
    }

    /* Clear runtime onboard_only option */
    esp_err_t clear_opt_err = mesh_udp_bridge_set_onboard_only(false);
    if (clear_opt_err != ESP_OK) {
        ESP_LOGW(WEB_TAG, "Failed to clear onboard_only option: %s", esp_err_to_name(clear_opt_err));
    }

    ESP_LOGI(WEB_TAG, "External server configuration cleared (manual IP and onboard_only option)");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"success\":true,\"onboard_only\":false,\"limited_mode\":false}", -1);
}

/*******************************************************
 *                Plugin Data Endpoint Helpers
 *******************************************************/

/**
 * @brief Extract plugin name from URL
 *
 * Extracts plugin name from URL pattern: /api/plugin/<plugin-name>/data or /api/plugin/<plugin-name>/bundle
 *
 * @param uri Request URI (e.g., "/api/plugin/rgb_effect/data" or "/api/plugin/rgb_effect/bundle")
 * @param plugin_name Output buffer for plugin name (must be at least 32 bytes)
 * @param name_size Size of plugin_name buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if URL format is invalid
 */
static esp_err_t extract_plugin_name_from_url(const char *uri, char *plugin_name, size_t name_size)
{
    const char *prefix = "/api/plugin/";
    const char *suffix_data = "/data";
    const char *suffix_bundle = "/bundle";

    /* Check if URI starts with prefix */
    if (strncmp(uri, prefix, strlen(prefix)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Find start of plugin name */
    const char *name_start = uri + strlen(prefix);

    /* Find end of plugin name (either /data or /bundle) */
    /* Check URI length to ensure we can safely check for suffixes */
    size_t uri_len = strlen(uri);
    size_t prefix_len = strlen(prefix);
    if (uri_len < prefix_len + 1) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Find suffix position - check for /data first, then /bundle */
    const char *name_end = NULL;
    const char *data_pos = strstr(name_start, suffix_data);
    const char *bundle_pos = strstr(name_start, suffix_bundle);

    /* Verify suffix is at valid position (not embedded in plugin name) */
    /* Since plugin names are validated to [a-zA-Z0-9_-], they can't contain "/" */
    /* So we just need to find the first occurrence and verify it's followed by end or query */
    if (data_pos != NULL) {
        size_t suffix_len = strlen(suffix_data);
        char next_char = data_pos[suffix_len];
        if (next_char == '\0' || next_char == '?' || next_char == '#') {
            name_end = data_pos;
        }
    }

    if (name_end == NULL && bundle_pos != NULL) {
        size_t suffix_len = strlen(suffix_bundle);
        char next_char = bundle_pos[suffix_len];
        if (next_char == '\0' || next_char == '?' || next_char == '#') {
            name_end = bundle_pos;
        }
    }

    if (name_end == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate name length */
    size_t name_len = name_end - name_start;
    if (name_len == 0 || name_len >= name_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Copy plugin name */
    strncpy(plugin_name, name_start, name_len);
    plugin_name[name_len] = '\0';

    return ESP_OK;
}

/**
 * @brief Validate plugin name format
 *
 * Validates plugin name against regex: ^[a-zA-Z0-9_-]+$
 *
 * @param name Plugin name to validate
 * @return true if valid, false otherwise
 */
static bool is_valid_plugin_name(const char *name)
{
    if (name == NULL || strlen(name) == 0) {
        return false;
    }

    for (const char *p = name; *p != '\0'; p++) {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-')) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Validate Content-Type header
 *
 * Validates that Content-Type is application/octet-stream
 *
 * @param req HTTP request
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG if invalid
 */
static esp_err_t validate_content_type(httpd_req_t *req)
{
    size_t content_type_len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (content_type_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char content_type[64];
    if (content_type_len >= sizeof(content_type)) {
        content_type_len = sizeof(content_type) - 1;
    }

    esp_err_t ret = httpd_req_get_hdr_value_str(req, "Content-Type", content_type, content_type_len + 1);
    if (ret != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check for application/octet-stream (case-insensitive) */
    if (strcasecmp(content_type, "application/octet-stream") != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief Check if mesh is busy (non-blocking)
 *
 * Checks if mesh is busy/congested (e.g., OTA in progress).
 * This is a fast, non-blocking check.
 *
 * @return true if mesh is busy, false if available
 */
static bool is_mesh_busy(void)
{
    /* Check if OTA download is in progress */
    if (mesh_ota_is_downloading()) {
        return true;
    }

    /* Check if OTA distribution is in progress */
    mesh_ota_distribution_status_t ota_status;
    if (mesh_ota_get_distribution_status(&ota_status) == ESP_OK && ota_status.distributing) {
        return true;
    }

    /* Check if mesh is not ready (not root or not connected) */
    if (!esp_mesh_is_root()) {
        return true;  /* Only root node can forward data */
    }

    return false;
}

/**
 * @brief Read request body as raw bytes
 *
 * Reads HTTP request body as raw bytes, handling partial reads.
 * Reads until all data is received (httpd_req_recv returns 0) or buffer is full.
 *
 * @param req HTTP request
 * @param buffer Output buffer for data
 * @param buffer_size Size of buffer
 * @param bytes_read Output parameter for number of bytes read
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t read_request_body(httpd_req_t *req, uint8_t *buffer, size_t buffer_size, size_t *bytes_read)
{
    if (buffer == NULL || bytes_read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *bytes_read = 0;
    size_t total_received = 0;

    /* Read data in chunks until all data is received or buffer is full */
    while (total_received < buffer_size) {
        size_t remaining = buffer_size - total_received;
        int ret = httpd_req_recv(req, (char *)(buffer + total_received), remaining);

        if (ret <= 0) {
            if (ret == 0) {
                /* Connection closed or no more data - all data received */
                *bytes_read = total_received;
                return ESP_OK;
            }
            /* Error (negative value indicates error) */
            *bytes_read = total_received;
            return ESP_FAIL;
        }

        total_received += ret;

        /* If we read less than requested, we've likely received all available data */
        /* However, continue reading in case more data arrives */
        /* The loop will exit when httpd_req_recv returns 0 (no more data) */
    }

    /* Buffer is full - check if there's more data beyond our limit */
    if (total_received >= buffer_size) {
        /* Try to read one more byte to detect if payload exceeds 512 byte limit */
        /* If we can read even one more byte, the payload is too large */
        uint8_t dummy;
        int more = httpd_req_recv(req, (char *)&dummy, 1);
        if (more > 0) {
            /* More data available - payload exceeds 512 byte limit */
            *bytes_read = total_received;
            return ESP_ERR_INVALID_SIZE;
        }
        /* If more == 0, all data received and payload is exactly 512 bytes (valid) */
        /* If more < 0, error occurred (but we've already read valid data up to limit) */
    }

    *bytes_read = total_received;
    return ESP_OK;
}

/* API: GET /api/plugin/<plugin-name>/bundle - Returns JSON bundle with HTML/CSS/JS */
static esp_err_t api_plugin_bundle_handler(httpd_req_t *req)
{
    /* Extract plugin name from URL */
    char plugin_name[32];
    esp_err_t err = extract_plugin_name_from_url(req->uri, plugin_name, sizeof(plugin_name));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    /* Validate plugin name format */
    if (!is_valid_plugin_name(plugin_name)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    /* Check if plugin exists and has web UI */
    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin == NULL || plugin->web_ui == NULL) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Plugin not found\"}", -1);
        return ESP_FAIL;
    }

    /* Calculate required buffer size (dry-run mode) */
    size_t required_size = 0;
    err = plugin_get_web_bundle(plugin_name, NULL, 0, &required_size);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Internal server error\"}", -1);
        return ESP_FAIL;
    }

    /* Allocate buffer for JSON bundle */
    char *json_buffer = (char *)malloc(required_size);
    if (json_buffer == NULL) {
        ESP_LOGE(WEB_TAG, "Failed to allocate buffer for bundle (size: %zu)", required_size);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Internal server error\"}", -1);
        return ESP_FAIL;
    }

    /* Build JSON bundle */
    err = plugin_get_web_bundle(plugin_name, json_buffer, required_size, &required_size);
    if (err != ESP_OK) {
        free(json_buffer);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Internal server error\"}", -1);
        return ESP_FAIL;
    }

    /* Send JSON response */
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    err = httpd_resp_send(req, json_buffer, -1);

    /* Free buffer */
    free(json_buffer);

    if (err != ESP_OK) {
        ESP_LOGE(WEB_TAG, "Failed to send bundle response: 0x%x", err);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* API: POST /api/plugin/<plugin-name>/data - Accepts raw bytes data and forwards to mesh */
static esp_err_t api_plugin_data_handler(httpd_req_t *req)
{
    /* Extract plugin name from URL */
    char plugin_name[32];
    esp_err_t err = extract_plugin_name_from_url(req->uri, plugin_name, sizeof(plugin_name));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    /* Validate plugin name format */
    if (!is_valid_plugin_name(plugin_name)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    /* Check if plugin exists */
    const plugin_info_t *plugin = plugin_get_by_name(plugin_name);
    if (plugin == NULL) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Plugin not found\"}", -1);
        return ESP_FAIL;
    }

    /* Check mesh status (non-blocking) */
    if (is_mesh_busy()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Service unavailable\"}", -1);
        return ESP_FAIL;
    }

    /* Validate Content-Type header */
    err = validate_content_type(req);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid request\"}", -1);
        return ESP_FAIL;
    }

    /* Read request body as raw bytes (max 512 bytes) */
    uint8_t data[512];
    size_t bytes_read = 0;
    err = read_request_body(req, data, sizeof(data), &bytes_read);
    if (err == ESP_ERR_INVALID_SIZE) {
        /* Payload exceeds 512 bytes */
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Payload too large\"}", -1);
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Internal server error\"}", -1);
        return ESP_FAIL;
    }

    /* Note: Zero-length data is valid and should be forwarded (bytes_read == 0 is OK) */

    /* Process command locally on root node (if root node) */
    /* Root node processes the command locally before forwarding to child nodes */
    if (esp_mesh_is_root() && plugin != NULL) {
        /* Construct command buffer: [PLUGIN_CMD_DATA:1] [DATA:N] */
        uint8_t cmd_buffer[513];  /* PLUGIN_CMD_DATA (1) + data (max 512) */
        cmd_buffer[0] = PLUGIN_CMD_DATA;
        if (bytes_read > 0) {
            memcpy(&cmd_buffer[1], data, bytes_read);
        }

        /* Call plugin's command handler directly (local processing) */
        if (plugin->callbacks.command_handler != NULL) {
            esp_err_t handler_err = plugin->callbacks.command_handler(cmd_buffer, bytes_read + 1);
            if (handler_err != ESP_OK) {
                ESP_LOGW(WEB_TAG, "Plugin '%s' command handler returned error: %s", plugin_name, esp_err_to_name(handler_err));
                /* Continue with forwarding even if local processing fails */
            }
        }
    }

    /* Forward to mesh (broadcast to child nodes) */
    err = plugin_forward_data_to_mesh(plugin_name, data, (uint16_t)bytes_read);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Internal server error\"}", -1);
        return ESP_FAIL;
    }

    /* Return success */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"success\":true}", -1);
    return ESP_OK;
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

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 32;  /* Updated: nodes, color_get, color_post, sequence_post, sequence_pointer, sequence_start, sequence_stop, sequence_reset, sequence_status, ota_download, ota_status, ota_cancel, ota_version, ota_distribute, ota_distribution_status, ota_distribution_progress, ota_distribution_cancel, ota_reboot, plugin_activate, plugin_deactivate, plugin_active, plugin_stop, plugin_pause, plugin_reset, plugins_list, plugin_bundle, plugin_data, index = 28 handlers (32 for future expansion) */
    config.stack_size = 8192;
    config.server_port = 80;
    config.max_open_sockets = 4;  /* Reduced to 4 (3 internal + 1 connection) to leave sockets for UDP listeners and mDNS */
    config.lru_purge_enable = true;  /* Enable automatic cleanup of closed connections to prevent resource leaks */

    ESP_LOGI(WEB_TAG, "Starting web server on port %d", config.server_port);

    esp_err_t httpd_start_err = httpd_start(&server_handle, &config);

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

        /* Register plugin bundle endpoint handler */
        /* Note: ESP-IDF HTTP server doesn't support wildcards, so we register with a base URI */
        /* The handler will parse the URI manually to extract plugin name from /api/plugin/<name>/bundle */
        httpd_uri_t plugin_bundle_uri = {
            .uri       = "/api/plugin/",  /* Base URI pattern - handler will parse full URI manually */
            .method    = HTTP_GET,
            .handler   = api_plugin_bundle_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_bundle_uri);
        if (reg_err != ESP_OK) {
            /* If registration fails, it might be because ESP-IDF doesn't support prefix matching */
            /* In that case, we'll need to use a catch-all handler or different approach */
            ESP_LOGW(WEB_TAG, "Failed to register plugin bundle URI (may need catch-all approach): 0x%x", reg_err);
            /* Continue anyway - handler registration failure is not critical for server startup */
            /* The endpoint can be added via catch-all handler if needed */
        }

        /* Register plugin data endpoint handler */
        /* Note: ESP-IDF HTTP server doesn't support wildcards, so we register with a base URI */
        /* The handler will parse the URI manually to extract plugin name from /api/plugin/<name>/data */
        /* If ESP-IDF doesn't match this base URI, we may need to use a catch-all handler approach */
        httpd_uri_t plugin_data_uri = {
            .uri       = "/api/plugin/",  /* Base URI pattern - handler will parse full URI manually */
            .method    = HTTP_POST,
            .handler   = api_plugin_data_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &plugin_data_uri);
        if (reg_err != ESP_OK) {
            /* If registration fails, it might be because ESP-IDF doesn't support prefix matching */
            /* In that case, we'll need to use a catch-all handler or different approach */
            ESP_LOGW(WEB_TAG, "Failed to register plugin data URI (may need catch-all approach): 0x%x", reg_err);
            /* Continue anyway - handler registration failure is not critical for server startup */
            /* The endpoint can be added via catch-all handler if needed */
        }

        httpd_uri_t settings_external_server_get_uri = {
            .uri       = "/api/settings/external-server",
            .method    = HTTP_GET,
            .handler   = api_settings_external_server_get_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &settings_external_server_get_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register external server GET URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t settings_external_server_post_uri = {
            .uri       = "/api/settings/external-server",
            .method    = HTTP_POST,
            .handler   = api_settings_external_server_post_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &settings_external_server_post_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register external server POST URI: 0x%x", reg_err);
            httpd_stop(server_handle);
            server_handle = NULL;
            return ESP_FAIL;
        }

        httpd_uri_t settings_external_server_delete_uri = {
            .uri       = "/api/settings/external-server",
            .method    = HTTP_DELETE,
            .handler   = api_settings_external_server_delete_handler,
            .user_ctx  = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &settings_external_server_delete_uri);
        if (reg_err != ESP_OK) {
            ESP_LOGE(WEB_TAG, "Failed to register external server DELETE URI: 0x%x", reg_err);
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

