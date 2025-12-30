#include "mesh_web.h"
#include "light_neopixel.h"
#include "node_sequence.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *WEB_TAG = "mesh_web";
static httpd_handle_t server_handle = NULL;

/* Forward declarations */
static esp_err_t api_nodes_handler(httpd_req_t *req);
static esp_err_t api_color_get_handler(httpd_req_t *req);
static esp_err_t api_color_post_handler(httpd_req_t *req);
static esp_err_t api_sequence_post_handler(httpd_req_t *req);
static esp_err_t api_sequence_pointer_handler(httpd_req_t *req);
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
"  max-width: 1200px;"
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
".grid-section {"
"  margin-bottom: 30px;"
"}"
".grid-container {"
"  display: grid;"
"  grid-template-columns: auto repeat(16, 1fr);"
"  grid-template-rows: auto repeat(16, 1fr);"
"  gap: 2px;"
"  max-width: 100%;"
"  margin: 0 auto;"
"  padding: 10px;"
"}"
".grid-label {"
"  font-weight: bold;"
"  text-align: center;"
"  display: flex;"
"  align-items: center;"
"  justify-content: center;"
"  cursor: pointer;"
"  user-select: none;"
"  min-width: 44px;"
"  min-height: 44px;"
"  font-size: 12px;"
"  color: #333;"
"  background: #f5f5f5;"
"  border-radius: 4px;"
"}"
".grid-label:hover {"
"  background: #e0e0e0;"
"}"
".grid-label-z {"
"  grid-column: 1;"
"  grid-row: 1;"
"}"
".grid-label-col {"
"  grid-row: 1;"
"}"
".grid-label-row {"
"  grid-column: 1;"
"}"
".grid-square {"
"  aspect-ratio: 1;"
"  border: 1px solid #ddd;"
"  cursor: pointer;"
"  transition: background-color 0.2s, border 0.2s;"
"  min-width: 44px;"
"  min-height: 44px;"
"}"
".grid-square:hover {"
"  border: 2px solid #667eea;"
"}"
".grid-square:active {"
"  transform: scale(0.95);"
"}"
".grid-square.current {"
"  border: 3px solid #ff6b6b;"
"  box-shadow: 0 0 8px rgba(255, 107, 107, 0.6);"
"  z-index: 10;"
"  transition: all 0.1s ease;"
"}"
".row-count-control {"
"  margin-bottom: 20px;"
"  text-align: center;"
"}"
".row-count-control label {"
"  display: block;"
"  font-size: 14px;"
"  color: #666;"
"  margin-bottom: 8px;"
"}"
".row-count-control select {"
"  padding: 8px 12px;"
"  border: 2px solid #ddd;"
"  border-radius: 8px;"
"  font-size: 16px;"
"  background: white;"
"  cursor: pointer;"
"  min-width: 120px;"
"}"
".row-count-control select:hover {"
"  border-color: #667eea;"
"}"
".row-count-control select:focus {"
"  outline: none;"
"  border-color: #667eea;"
"  box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);"
"}"
".rhythm-control {"
"  margin-top: 20px;"
"  text-align: center;"
"}"
".rhythm-label {"
"  font-size: 14px;"
"  color: #666;"
"  margin-bottom: 8px;"
"}"
".rhythm-input-container {"
"  display: flex;"
"  align-items: center;"
"  justify-content: center;"
"  gap: 10px;"
"  margin-bottom: 10px;"
"}"
"#rhythmInput {"
"  width: 100%;"
"  max-width: 300px;"
"  height: 40px;"
"  border: 2px solid #ddd;"
"  border-radius: 8px;"
"  padding: 0 12px;"
"  font-size: 16px;"
"}"
"#rhythmDisplay {"
"  font-size: 18px;"
"  color: #667eea;"
"  font-weight: bold;"
"  min-width: 80px;"
"}"
".sync-button-container {"
"  text-align: center;"
"  margin-top: 20px;"
"}"
".export-import-container {"
"  display: flex;"
"  gap: 10px;"
"  justify-content: center;"
"  margin-top: 10px;"
"}"
"#exportButton, #importButton {"
"  background: #667eea;"
"  color: white;"
"  border: none;"
"  padding: 12px 24px;"
"  border-radius: 8px;"
"  font-size: 16px;"
"  font-weight: bold;"
"  cursor: pointer;"
"  transition: background-color 0.2s;"
"  min-height: 44px;"
"}"
"#exportButton:hover, #importButton:hover {"
"  background: #5568d3;"
"}"
"#exportButton:active, #importButton:active {"
"  background: #4457b8;"
"}"
"#exportContainer, #importContainer {"
"  margin-top: 20px;"
"  text-align: center;"
"}"
"#exportTextarea, #importTextarea {"
"  width: 100%;"
"  max-width: 600px;"
"  padding: 12px;"
"  border: 2px solid #ddd;"
"  border-radius: 8px;"
"  font-family: 'Courier New', monospace;"
"  font-size: 14px;"
"  resize: vertical;"
"  margin-bottom: 10px;"
"}"
"#importTextarea {"
"  min-height: 120px;"
"}"
".export-import-actions {"
"  display: flex;"
"  gap: 10px;"
"  justify-content: center;"
"  margin-top: 10px;"
"}"
"#copyExportButton, #confirmImportButton, #cancelImportButton {"
"  background: #27ae60;"
"  color: white;"
"  border: none;"
"  padding: 8px 16px;"
"  border-radius: 8px;"
"  font-size: 14px;"
"  font-weight: bold;"
"  cursor: pointer;"
"  transition: background-color 0.2s;"
"}"
"#copyExportButton:hover, #confirmImportButton:hover {"
"  background: #229954;"
"}"
"#cancelImportButton {"
"  background: #95a5a6;"
"}"
"#cancelImportButton:hover {"
"  background: #7f8c8d;"
"}"
"#exportFeedback, #importFeedback {"
"  margin-top: 10px;"
"  font-size: 14px;"
"  text-align: center;"
"}"
".export-feedback-success, .import-feedback-success {"
"  color: #27ae60;"
"}"
".export-feedback-error, .import-feedback-error {"
"  color: #e74c3c;"
"}"
"#syncButton {"
"  background: #667eea;"
"  color: white;"
"  border: none;"
"  padding: 12px 24px;"
"  border-radius: 8px;"
"  font-size: 16px;"
"  font-weight: bold;"
"  cursor: pointer;"
"  transition: background-color 0.2s;"
"  width: 100%;"
"  max-width: 250px;"
"  min-height: 44px;"
"}"
"#syncButton:hover {"
"  background: #5568d3;"
"}"
"#syncButton:active {"
"  background: #4457b8;"
"}"
"#syncButton:disabled {"
"  opacity: 0.5;"
"  cursor: not-allowed;"
"}"
"#syncFeedback {"
"  margin-top: 10px;"
"  font-size: 14px;"
"  text-align: center;"
"}"
".sync-feedback-success {"
"  color: #27ae60;"
"}"
".sync-feedback-error {"
"  color: #e74c3c;"
"}"
"#colorPicker {"
"  position: absolute;"
"  opacity: 0;"
"  width: 0;"
"  height: 0;"
"  pointer-events: none;"
"}"
"@media (min-width: 768px) {"
"  .container { padding: 30px; }"
"  .grid-label { font-size: 14px; }"
"  .grid-square { min-width: 50px; min-height: 50px; }"
"  .rhythm-input-container { max-width: 400px; margin: 0 auto; }"
"  #syncButton { max-width: 200px; }"
"}"
"@media (min-width: 1024px) {"
"  .container { padding: 40px; }"
"  .grid-label { font-size: 16px; }"
"  .grid-square { min-width: 60px; min-height: 60px; }"
"  .rhythm-input-container { max-width: 500px; }"
"  #syncButton { max-width: 250px; }"
"}"
"@media (max-width: 600px) {"
"  .container { padding: 20px; }"
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
"<div class=\"row-count-control\">"
"<label for=\"rowCountSelect\">Rows:</label>"
"<select id=\"rowCountSelect\">"
"<option value=\"1\">1 row</option>"
"<option value=\"2\">2 rows</option>"
"<option value=\"3\">3 rows</option>"
"<option value=\"4\" selected>4 rows</option>"
"<option value=\"5\">5 rows</option>"
"<option value=\"6\">6 rows</option>"
"<option value=\"7\">7 rows</option>"
"<option value=\"8\">8 rows</option>"
"<option value=\"9\">9 rows</option>"
"<option value=\"10\">10 rows</option>"
"<option value=\"11\">11 rows</option>"
"<option value=\"12\">12 rows</option>"
"<option value=\"13\">13 rows</option>"
"<option value=\"14\">14 rows</option>"
"<option value=\"15\">15 rows</option>"
"<option value=\"16\">16 rows</option>"
"</select>"
"</div>"
"<div class=\"grid-section\">"
"<div class=\"grid-container\" id=\"gridContainer\">"
"<div class=\"grid-label grid-label-z\" data-action=\"all\">Z</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"0\">0</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"1\">1</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"2\">2</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"3\">3</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"4\">4</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"5\">5</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"6\">6</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"7\">7</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"8\">8</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"9\">9</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"10\">A</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"11\">B</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"12\">C</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"13\">D</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"14\">E</div>"
"<div class=\"grid-label grid-label-col\" data-col=\"15\">F</div>"
"<div class=\"grid-label grid-label-row\" data-row=\"0\">0</div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"0\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"1\">1</div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"1\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"2\">2</div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"2\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"3\">3</div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"3\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"4\">4</div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"4\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"5\">5</div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"5\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"6\">6</div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"6\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"7\">7</div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"7\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"8\">8</div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"8\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"9\">9</div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"9\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"10\">A</div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"10\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"11\">B</div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"11\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"12\">C</div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"12\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"13\">D</div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"13\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"14\">E</div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"14\" data-col=\"15\"></div>"
"<div class=\"grid-label grid-label-row\" data-row=\"15\">F</div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"0\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"1\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"2\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"3\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"4\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"5\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"6\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"7\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"8\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"9\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"10\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"11\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"12\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"13\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"14\"></div>"
"<div class=\"grid-square\" data-row=\"15\" data-col=\"15\"></div>"
"</div>"
"<div class=\"rhythm-control\">"
"<div class=\"rhythm-label\">Tempo (ms)</div>"
"<div class=\"rhythm-input-container\">"
"<input type=\"number\" id=\"rhythmInput\" min=\"10\" max=\"2550\" step=\"10\" value=\"250\">"
"<div id=\"rhythmDisplay\">250ms</div>"
"</div>"
"</div>"
"<div class=\"sync-button-container\">"
"<button id=\"syncButton\">Sync</button>"
"<div id=\"syncFeedback\"></div>"
"</div>"
"<div class=\"export-import-container\">"
"<button id=\"exportButton\">Export Sequence</button>"
"<button id=\"importButton\">Import Sequence</button>"
"</div>"
"<div id=\"exportContainer\" style=\"display: none;\">"
"<textarea id=\"exportTextarea\" readonly rows=\"5\" cols=\"80\"></textarea>"
"<div class=\"export-import-actions\">"
"<button id=\"copyExportButton\">Copy</button>"
"</div>"
"<div id=\"exportFeedback\"></div>"
"</div>"
"<div id=\"importContainer\" style=\"display: none;\">"
"<textarea id=\"importTextarea\" rows=\"8\" cols=\"80\" placeholder=\"Paste CSV data here (format: index;RED;GREEN;BLUE)...\"></textarea>"
"<div class=\"export-import-actions\">"
"<button id=\"confirmImportButton\">Import</button>"
"<button id=\"cancelImportButton\">Cancel</button>"
"</div>"
"<div id=\"importFeedback\"></div>"
"</div>"
"<input type=\"color\" id=\"colorPicker\">"
"</div>"
"<script>"
"let gridData = new Uint8Array(768);"
"let tempo = 250;"
"let numRows = 4;"
"let selectedRow = null;"
"let selectedCol = null;"
"let selectedAction = null;"
"let updateInterval;"
"let importHideTimeout = null;"
"let sequenceIndicatorInterval = null;"
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
"function getChannelIndex(row, col, channel) {"
"  return (row * 16 + col) * 3 + channel;"
"}"
""
"function getSquareColor(row, col) {"
"  const idx = getChannelIndex(row, col, 0);"
"  return {"
"    r: gridData[idx],"
"    g: gridData[idx + 1],"
"    b: gridData[idx + 2]"
"  };"
"}"
""
"function setSquareColor(row, col, r, g, b) {"
"  const idx = getChannelIndex(row, col, 0);"
"  gridData[idx] = r;"
"  gridData[idx + 1] = g;"
"  gridData[idx + 2] = b;"
"}"
""
"function setRowColor(row, r, g, b) {"
"  for (let col = 0; col < 16; col++) {"
"    setSquareColor(row, col, r, g, b);"
"  }"
"}"
""
"function setColumnColor(col, r, g, b) {"
"  for (let row = 0; row < 16; row++) {"
"    setSquareColor(row, col, r, g, b);"
"  }"
"}"
""
"function setAllColor(r, g, b) {"
"  for (let row = 0; row < 16; row++) {"
"    for (let col = 0; col < 16; col++) {"
"      setSquareColor(row, col, r, g, b);"
"    }"
"  }"
"}"
""
"function quantizeColor(r, g, b) {"
"  return {"
"    r: Math.max(0, Math.min(15, Math.floor(r / 16))),"
"    g: Math.max(0, Math.min(15, Math.floor(g / 16))),"
"    b: Math.max(0, Math.min(15, Math.floor(b / 16)))"
"  };"
"}"
""
"function initializeDefaultPattern() {"
"  for (let row = 0; row < 16; row++) {"
"    if (row % 2 === 0) {"
"      setRowColor(row, 15, 15, 15);"
"    } else {"
"      setRowColor(row, 0, 0, 15);"
"    }"
"  }"
"}"
""
"function updateGridRows() {"
"  for (let row = 0; row < 16; row++) {"
"    const rowElements = document.querySelectorAll(`[data-row=\"${row}\"]`);"
"    if (row < numRows) {"
"      rowElements.forEach(el => el.style.display = '');"
"    } else {"
"      rowElements.forEach(el => el.style.display = 'none');"
"    }"
"  }"
"}"
""
"function renderGrid() {"
"  const squares = document.querySelectorAll('.grid-square');"
"  squares.forEach(function(square) {"
"    const row = parseInt(square.dataset.row);"
"    const col = parseInt(square.dataset.col);"
"    const color = getSquareColor(row, col);"
"    const r8 = color.r * 17;"
"    const g8 = color.g * 17;"
"    const b8 = color.b * 17;"
"    square.style.backgroundColor = 'rgb(' + r8 + ', ' + g8 + ', ' + b8 + ')';"
"  });"
"}"
""
"function showColorPicker(row, col, action) {"
"  selectedRow = row;"
"  selectedCol = col;"
"  selectedAction = action;"
"  let currentColor = {r: 0, g: 0, b: 0};"
"  if (action === 'square') {"
"    currentColor = getSquareColor(row, col);"
"  } else if (action === 'row') {"
"    currentColor = getSquareColor(row, 0);"
"  } else if (action === 'col') {"
"    currentColor = getSquareColor(0, col);"
"  } else if (action === 'all') {"
"    currentColor = getSquareColor(0, 0);"
"  }"
"  const r8 = currentColor.r * 17;"
"  const g8 = currentColor.g * 17;"
"  const b8 = currentColor.b * 17;"
"  const colorPicker = document.getElementById('colorPicker');"
"  colorPicker.value = rgbToHex(r8, g8, b8);"
"  colorPicker.click();"
"}"
""
"function packGridData(numRows) {"
"  const numSquares = numRows * 16;"
"  const packedSize = (numSquares / 2) * 3;"
"  const packed = new Uint8Array(packedSize);"
"  for (let i = 0; i < numSquares; i += 2) {"
"    const row0 = Math.floor(i / 16);"
"    const col0 = i % 16;"
"    const row1 = Math.floor((i + 1) / 16);"
"    const col1 = (i + 1) % 16;"
"    const color0 = getSquareColor(row0, col0);"
"    const color1 = getSquareColor(row1, col1);"
"    const byteIdx = Math.floor(i / 2) * 3;"
"    packed[byteIdx] = (color0.r << 4) | color0.g;"
"    packed[byteIdx + 1] = (color0.b << 4) | color1.r;"
"    packed[byteIdx + 2] = (color1.g << 4) | color1.b;"
"  }"
"  return packed;"
"}"
""
"function generateCSVLine(index, r, g, b) {"
"  return index + ';' + r + ';' + g + ';' + b;"
"}"
""
"function exportGridToCSV() {"
"  const csvLines = [];"
"  const numSquares = numRows * 16;"
"  for (let i = 0; i < numSquares; i++) {"
"    const row = Math.floor(i / 16);"
"    const col = i % 16;"
"    const index = i + 1;"
"    const color = getSquareColor(row, col);"
"    csvLines.push(generateCSVLine(index, color.r, color.g, color.b));"
"  }"
"  return csvLines.join('\\n');"
"}"
""
"function parseCSVLine(line) {"
"  const trimmed = line.trim();"
"  if (!trimmed) {"
"    return null;"
"  }"
"  const parts = trimmed.split(';');"
"  if (parts.length !== 4) {"
"    throw new Error('Invalid CSV line: expected 4 columns, got ' + parts.length);"
"  }"
"  const index = parseInt(parts[0], 10);"
"  const r = parseInt(parts[1], 10);"
"  const g = parseInt(parts[2], 10);"
"  const b = parseInt(parts[3], 10);"
"  if (isNaN(index) || index < 1 || index > 256) {"
"    throw new Error('Invalid index: must be 1-256, got ' + parts[0]);"
"  }"
"  if (isNaN(r) || r < 0 || r > 15) {"
"    throw new Error('Invalid RED value: must be 0-15, got ' + parts[1]);"
"  }"
"  if (isNaN(g) || g < 0 || g > 15) {"
"    throw new Error('Invalid GREEN value: must be 0-15, got ' + parts[2]);"
"  }"
"  if (isNaN(b) || b < 0 || b > 15) {"
"    throw new Error('Invalid BLUE value: must be 0-15, got ' + parts[3]);"
"  }"
"  return { index: index, r: r, g: g, b: b };"
"}"
""
"function parseCSVText(csvText) {"
"  const lines = csvText.split(/\\r?\\n/);"
"  const parsedData = [];"
"  for (let i = 0; i < lines.length; i++) {"
"    try {"
"      const parsed = parseCSVLine(lines[i]);"
"      if (parsed !== null) {"
"        parsedData.push(parsed);"
"      }"
"    } catch (err) {"
"      throw new Error('Line ' + (i + 1) + ': ' + err.message);"
"    }"
"  }"
"  return parsedData;"
"}"
""
"function indexToGridPosition(index) {"
"  const row = Math.floor((index - 1) / 16);"
"  const col = (index - 1) % 16;"
"  if (row < 0 || row > 15 || col < 0 || col > 15) {"
"    throw new Error('Index ' + index + ' maps to invalid grid position (row: ' + row + ', col: ' + col + ')');"
"  }"
"  return { row: row, col: col };"
"}"
""
"function populateGridFromCSV(parsedData) {"
"  gridData = new Uint8Array(768);"
"  let maxIndex = 0;"
"  for (let i = 0; i < parsedData.length; i++) {"
"    const entry = parsedData[i];"
"    if (entry.index > maxIndex) {"
"      maxIndex = entry.index;"
"    }"
"    const pos = indexToGridPosition(entry.index);"
"    setSquareColor(pos.row, pos.col, entry.r, entry.g, entry.b);"
"  }"
"  if (maxIndex === 0) {"
"    throw new Error('No valid data found in CSV');"
"  }"
"  const calculatedRows = Math.ceil(maxIndex / 16);"
"  if (calculatedRows < 1 || calculatedRows > 16) {"
"    throw new Error('Calculated row count out of range: ' + calculatedRows + ' (must be 1-16)');"
"  }"
"  return calculatedRows;"
"}"
""
"function unpackGridData(packed) {"
"  gridData = new Uint8Array(768);"
"  const numPairs = Math.floor(packed.length / 3);"
"  const numSquares = numPairs * 2;"
"  for (let i = 0; i < numSquares; i += 2) {"
"    const byteIdx = Math.floor(i / 2) * 3;"
"    if (byteIdx + 2 >= packed.length) break;"
"    const byte0 = packed[byteIdx];"
"    const byte1 = packed[byteIdx + 1];"
"    const byte2 = packed[byteIdx + 2];"
"    const row0 = Math.floor(i / 16);"
"    const col0 = i % 16;"
"    const idx0 = (row0 * 16 + col0) * 3;"
"    gridData[idx0] = (byte0 >> 4) & 0x0F;"
"    gridData[idx0 + 1] = byte0 & 0x0F;"
"    gridData[idx0 + 2] = (byte1 >> 4) & 0x0F;"
"    const row1 = Math.floor((i + 1) / 16);"
"    const col1 = (i + 1) % 16;"
"    const idx1 = (row1 * 16 + col1) * 3;"
"    gridData[idx1] = byte1 & 0x0F;"
"    gridData[idx1 + 1] = (byte2 >> 4) & 0x0F;"
"    gridData[idx1 + 2] = byte2 & 0x0F;"
"  }"
"}"
""
"function updateRhythmDisplay() {"
"  const display = document.getElementById('rhythmDisplay');"
"  if (display) {"
"    display.textContent = tempo + 'ms';"
"  }"
"}"
""
"function showImportUI() {"
"  if (importHideTimeout) {"
"    clearTimeout(importHideTimeout);"
"    importHideTimeout = null;"
"  }"
"  const importContainer = document.getElementById('importContainer');"
"  const exportContainer = document.getElementById('exportContainer');"
"  const importTextarea = document.getElementById('importTextarea');"
"  const importFeedback = document.getElementById('importFeedback');"
"  exportContainer.style.display = 'none';"
"  importContainer.style.display = 'block';"
"  importTextarea.value = '';"
"  importTextarea.focus();"
"  importFeedback.textContent = '';"
"  importFeedback.className = '';"
"}"
""
"function hideImportUI() {"
"  const importContainer = document.getElementById('importContainer');"
"  const importTextarea = document.getElementById('importTextarea');"
"  const importFeedback = document.getElementById('importFeedback');"
"  importContainer.style.display = 'none';"
"  importTextarea.value = '';"
"  importFeedback.textContent = '';"
"  importFeedback.className = '';"
"}"
""
"function importSequence() {"
"  const importTextarea = document.getElementById('importTextarea');"
"  const importFeedback = document.getElementById('importFeedback');"
"  const csvText = importTextarea.value.trim();"
"  importFeedback.textContent = '';"
"  importFeedback.className = '';"
"  if (!csvText) {"
"    importFeedback.textContent = 'Please paste CSV data';"
"    importFeedback.className = 'import-feedback-error';"
"    return;"
"  }"
"  try {"
"    const parsedData = parseCSVText(csvText);"
"    if (parsedData.length === 0) {"
"      importFeedback.textContent = 'No valid CSV data found';"
"      importFeedback.className = 'import-feedback-error';"
"      return;"
"    }"
"    const importedRows = populateGridFromCSV(parsedData);"
"    numRows = importedRows;"
"    const rowCountSelect = document.getElementById('rowCountSelect');"
"    rowCountSelect.value = numRows;"
"    updateGridRows();"
"    renderGrid();"
"    stopSequenceIndicator();"
"    importFeedback.textContent = 'Import successful!';"
"    importFeedback.className = 'import-feedback-success';"
"    if (importHideTimeout) {"
"      clearTimeout(importHideTimeout);"
"    }"
"    importHideTimeout = setTimeout(function() {"
"      hideImportUI();"
"      importHideTimeout = null;"
"    }, 2000);"
"  } catch (err) {"
"    importFeedback.textContent = 'Import error: ' + (err.message || 'Unknown error');"
"    importFeedback.className = 'import-feedback-error';"
"    console.error('Import error:', err);"
"  }"
"}"
""
"function exportSequence() {"
"  try {"
"    if (numRows < 1 || numRows > 16) {"
"      throw new Error('Row count out of range (1-16)');"
"    }"
"    const csvText = exportGridToCSV();"
"    const exportTextarea = document.getElementById('exportTextarea');"
"    const exportContainer = document.getElementById('exportContainer');"
"    const importContainer = document.getElementById('importContainer');"
"    const exportFeedback = document.getElementById('exportFeedback');"
"    importContainer.style.display = 'none';"
"    exportTextarea.value = csvText;"
"    exportContainer.style.display = 'block';"
"    exportTextarea.select();"
"    exportFeedback.textContent = '';"
"    exportFeedback.className = '';"
"  } catch (err) {"
"    const exportFeedback = document.getElementById('exportFeedback');"
"    exportFeedback.textContent = 'Error: ' + err.message;"
"    exportFeedback.className = 'export-feedback-error';"
"    console.error('Export error:', err);"
"  }"
"}"
""
"function updateSequenceIndicator() {"
"  fetch('/api/sequence/pointer')"
"    .then(response => {"
"      if (!response.ok) {"
"        return Promise.reject(new Error('HTTP error: ' + response.status));"
"      }"
"      return response.text();"
"    })"
"    .then(pointerText => {"
"      const pointer = parseInt(pointerText, 10);"
"      if (isNaN(pointer) || pointer < 0 || pointer > 255) {"
"        return;"
"      }"
"      const row = Math.floor(pointer / 16);"
"      const col = pointer % 16;"
"      document.querySelectorAll('.grid-square.current').forEach(sq => sq.classList.remove('current'));"
"      const currentSquare = document.querySelector(`[data-row=\"${row}\"][data-col=\"${col}\"]`);"
"      if (currentSquare) {"
"        currentSquare.classList.add('current');"
"      }"
"    })"
"    .catch(err => {"
"      console.error('Failed to fetch pointer:', err);"
"    });"
"}"
""
"function startSequenceIndicator() {"
"  if (sequenceIndicatorInterval) {"
"    clearInterval(sequenceIndicatorInterval);"
"  }"
"  const updateInterval = tempo * 16;"
"  sequenceIndicatorInterval = setInterval(updateSequenceIndicator, updateInterval);"
"  updateSequenceIndicator();"
"}"
""
"function stopSequenceIndicator() {"
"  if (sequenceIndicatorInterval) {"
"    clearInterval(sequenceIndicatorInterval);"
"    sequenceIndicatorInterval = null;"
"  }"
"  document.querySelectorAll('.grid-square.current').forEach(sq => sq.classList.remove('current'));"
"}"
""
"function syncGridData() {"
"  const syncButton = document.getElementById('syncButton');"
"  const syncFeedback = document.getElementById('syncFeedback');"
"  syncButton.disabled = true;"
"  syncButton.textContent = 'Syncing...';"
"  syncFeedback.textContent = '';"
"  syncFeedback.className = '';"
"  try {"
"    const packedData = packGridData(numRows);"
"    const payloadSize = 2 + packedData.length;"
"    const payload = new Uint8Array(payloadSize);"
"    const backendValue = Math.floor(tempo / 10);"
"    if (backendValue < 1 || backendValue > 255) {"
"      throw new Error('Tempo value out of range (10-2550ms)');"
"    }"
"    if (numRows < 1 || numRows > 16) {"
"      throw new Error('Row count out of range (1-16)');"
"    }"
"    payload[0] = backendValue;"
"    payload[1] = numRows;"
"    payload.set(packedData, 2);"
"    fetch('/api/sequence', {"
"      method: 'POST',"
"      body: payload"
"    })"
"    .then(response => response.json())"
"    .then(result => {"
"      if (result.success) {"
"        syncFeedback.textContent = 'Synced successfully!';"
"        syncFeedback.className = 'sync-feedback-success';"
"        stopSequenceIndicator();"
"        startSequenceIndicator();"
"      } else {"
"        syncFeedback.textContent = 'Error: ' + (result.error || 'Failed to sync');"
"        syncFeedback.className = 'sync-feedback-error';"
"      }"
"      syncButton.disabled = false;"
"      syncButton.textContent = 'Sync';"
"    })"
"    .catch(err => {"
"      syncFeedback.textContent = 'Network error: ' + err.message;"
"      syncFeedback.className = 'sync-feedback-error';"
"      syncButton.disabled = false;"
"      syncButton.textContent = 'Sync';"
"      console.error('Sync error:', err);"
"    });"
"  } catch (err) {"
"    syncFeedback.textContent = 'Error: ' + err.message;"
"    syncFeedback.className = 'sync-feedback-error';"
"    syncButton.disabled = false;"
"    syncButton.textContent = 'Sync';"
"    console.error('Sync error:', err);"
"  }"
"}"
""
"document.addEventListener('DOMContentLoaded', function() {"
"  initializeDefaultPattern();"
"  renderGrid();"
"  updateNodeCount();"
"  updateRhythmDisplay();"
"  updateGridRows();"
"  updateInterval = setInterval(function() {"
"    updateNodeCount();"
"  }, 5000);"
""
"  const gridContainer = document.getElementById('gridContainer');"
"  gridContainer.addEventListener('click', function(e) {"
"    const target = e.target;"
"    if (target.classList.contains('grid-square')) {"
"      const row = parseInt(target.dataset.row);"
"      const col = parseInt(target.dataset.col);"
"      showColorPicker(row, col, 'square');"
"    } else if (target.classList.contains('grid-label-row')) {"
"      const row = parseInt(target.dataset.row);"
"      showColorPicker(row, null, 'row');"
"    } else if (target.classList.contains('grid-label-col')) {"
"      const col = parseInt(target.dataset.col);"
"      showColorPicker(null, col, 'col');"
"    } else if (target.classList.contains('grid-label-z')) {"
"      showColorPicker(null, null, 'all');"
"    }"
"  });"
""
"  const colorPicker = document.getElementById('colorPicker');"
"  colorPicker.addEventListener('change', function(e) {"
"    const rgb = hexToRgb(e.target.value);"
"    if (rgb) {"
"      const quantized = quantizeColor(rgb.r, rgb.g, rgb.b);"
"      if (selectedAction === 'square') {"
"        setSquareColor(selectedRow, selectedCol, quantized.r, quantized.g, quantized.b);"
"      } else if (selectedAction === 'row') {"
"        setRowColor(selectedRow, quantized.r, quantized.g, quantized.b);"
"      } else if (selectedAction === 'col') {"
"        setColumnColor(selectedCol, quantized.r, quantized.g, quantized.b);"
"      } else if (selectedAction === 'all') {"
"        setAllColor(quantized.r, quantized.g, quantized.b);"
"      }"
"      renderGrid();"
"    }"
"  });"
""
"  const rhythmInput = document.getElementById('rhythmInput');"
"  rhythmInput.addEventListener('input', function(e) {"
"    const rawValue = parseInt(e.target.value) || 250;"
"    const clamped = Math.max(10, Math.min(2550, rawValue));"
"    tempo = Math.round(clamped / 10) * 10;"
"    e.target.value = tempo;"
"    updateRhythmDisplay();"
"    if (sequenceIndicatorInterval) {"
"      startSequenceIndicator();"
"    }"
"  });"
""
"  const rowCountSelect = document.getElementById('rowCountSelect');"
"  rowCountSelect.addEventListener('change', function(e) {"
"    numRows = parseInt(e.target.value);"
"    updateGridRows();"
"  });"
""
"  const syncButton = document.getElementById('syncButton');"
"  syncButton.addEventListener('click', syncGridData);"
""
"  const exportButton = document.getElementById('exportButton');"
"  exportButton.addEventListener('click', exportSequence);"
""
"  const importButton = document.getElementById('importButton');"
"  importButton.addEventListener('click', showImportUI);"
""
"  const confirmImportButton = document.getElementById('confirmImportButton');"
"  confirmImportButton.addEventListener('click', importSequence);"
""
"  const cancelImportButton = document.getElementById('cancelImportButton');"
"  cancelImportButton.addEventListener('click', hideImportUI);"
""
"  const importTextarea = document.getElementById('importTextarea');"
"  importTextarea.addEventListener('keydown', function(e) {"
"    if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {"
"      e.preventDefault();"
"      importSequence();"
"    }"
"  });"
""
"  const copyExportButton = document.getElementById('copyExportButton');"
"  copyExportButton.addEventListener('click', function() {"
"    const exportTextarea = document.getElementById('exportTextarea');"
"    const exportFeedback = document.getElementById('exportFeedback');"
"    exportTextarea.select();"
"    exportTextarea.setSelectionRange(0, exportTextarea.value.length);"
"    try {"
"      if (navigator.clipboard && navigator.clipboard.writeText) {"
"        navigator.clipboard.writeText(exportTextarea.value).then(function() {"
"          exportFeedback.textContent = 'Copied to clipboard!';"
"          exportFeedback.className = 'export-feedback-success';"
"        }).catch(function(err) {"
"          exportTextarea.focus();"
"          exportTextarea.select();"
"          if (document.execCommand('copy')) {"
"            exportFeedback.textContent = 'Copied to clipboard!';"
"            exportFeedback.className = 'export-feedback-success';"
"          } else {"
"            exportFeedback.textContent = 'Failed to copy';"
"            exportFeedback.className = 'export-feedback-error';"
"          }"
"        });"
"      } else {"
"        exportTextarea.focus();"
"        if (document.execCommand('copy')) {"
"          exportFeedback.textContent = 'Copied to clipboard!';"
"          exportFeedback.className = 'export-feedback-success';"
"        } else {"
"          exportFeedback.textContent = 'Copy not supported';"
"          exportFeedback.className = 'export-feedback-error';"
"        }"
"      }"
"    } catch (err) {"
"      exportFeedback.textContent = 'Copy failed: ' + err.message;"
"      exportFeedback.className = 'export-feedback-error';"
"    }"
"  });"
"});"
"</script>"
"</body>"
"</html>";

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
    esp_err_t err = mode_sequence_root_store_and_broadcast(rhythm, num_rows, color_data, color_data_len);

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

    uint16_t pointer = mode_sequence_root_get_pointer();
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
    config.max_uri_handlers = 8;  /* Updated: nodes, color_get, color_post, sequence_post, index = 5 handlers */
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

