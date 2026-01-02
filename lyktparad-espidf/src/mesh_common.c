/* Mesh Common Module Implementation
 *
 * This module contains code shared by both root and child nodes in the mesh network.
 * It provides common initialization, event handling framework, and shared state management.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

/* Default Kconfig CONFIG_ defines for standalone compilation.
 * These provide sensible defaults and are guarded so build-system
 * provided values (via -D or generated headers) still take precedence.
 */
// CONFIG_MESH_TOPOLOGY
// 0 if MESH_TOPO_TREE
// 1 if MESH_TOPO_CHAIN
#ifndef CONFIG_MESH_TOPOLOGY
#define CONFIG_MESH_TOPOLOGY 0
#endif
#ifndef CONFIG_MESH_ENABLE_PS
#define CONFIG_MESH_ENABLE_PS 1
#endif
#ifndef CONFIG_MESH_PS_DEV_DUTY_TYPE
#define CONFIG_MESH_PS_DEV_DUTY_TYPE 1
#endif
#ifndef CONFIG_MESH_PS_DEV_DUTY
#define CONFIG_MESH_PS_DEV_DUTY 10
#endif
#ifndef CONFIG_MESH_PS_NWK_DUTY
#define CONFIG_MESH_PS_NWK_DUTY 10
#endif
#ifndef CONFIG_MESH_PS_NWK_DUTY_DURATION
#define CONFIG_MESH_PS_NWK_DUTY_DURATION -1
#endif
#ifndef CONFIG_MESH_PS_NWK_DUTY_RULE
#define CONFIG_MESH_PS_NWK_DUTY_RULE 0
#endif
#ifndef CONFIG_MESH_MAX_LAYER
#define CONFIG_MESH_MAX_LAYER 6
#endif
#ifndef CONFIG_MESH_CHANNEL
#define CONFIG_MESH_CHANNEL 0
#endif
/* Site-specific configuration is now in mesh_config.h */
#ifndef CONFIG_MESH_AP_CONNECTIONS
#define CONFIG_MESH_AP_CONNECTIONS 6
#endif
#ifndef CONFIG_MESH_NON_MESH_AP_CONNECTIONS
#define CONFIG_MESH_NON_MESH_AP_CONNECTIONS 0
#endif
#ifndef CONFIG_MESH_ROUTE_TABLE_SIZE
#define CONFIG_MESH_ROUTE_TABLE_SIZE 50
#endif

#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "mesh_config.h"
#include "mesh_common.h"
#include "mesh_gpio.h"
#include "driver/gpio.h"
#include "mesh_device_config.h"
#include "mesh_commands.h"
#include "light_neopixel.h"
#include "light_common_cathode.h"
#include "mesh_ota.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#define RX_SIZE          (1500)
#define TX_SIZE          (1460)

static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = MESH_CONFIG_MESH_ID;
static uint8_t tx_buf[TX_SIZE] = { 0, };
static uint8_t rx_buf[RX_SIZE] = { 0, };
static bool is_running = true;
static bool is_mesh_connected = false;
static bool is_router_connected = false; /* Track router connection for root node */
static bool is_mesh_node_forced = false; /* Track if GPIO forcing is active for mesh node (non-root) */
static bool is_root_node_forced = false; /* Track if GPIO forcing is active for root node */
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_netif_t *netif_sta = NULL;
static TaskHandle_t led_restore_task_handle = NULL; /* Track LED restore task to prevent duplicates */
static esp_timer_handle_t scan_fail_toggle_timer = NULL; /* Timer for toggling LED on scan failures */
static uint8_t scan_toggle_r = 155, scan_toggle_g = 0, scan_toggle_b = 0; /* Stored RGB color for toggle (default RED) */
static bool scan_toggle_state = false; /* Current toggle state (false = off, true = on) */

/* Task function to restore red LED after 250ms delay */
static void led_restore_red_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(250));
    /* Only restore red if still non-root and not connected to mesh */
    if (!esp_mesh_is_root() && !is_mesh_connected) {
        mesh_light_set_colour(MESH_LIGHT_RED);
    }
    led_restore_task_handle = NULL; /* Clear handle before deleting */
    vTaskDelete(NULL);
}

/* Timer callback to toggle LED on each failed connection attempt during scanning */
static void scan_fail_toggle_timer_cb(void *arg)
{
    /* Only toggle if we're still a non-root node and not connected */
    if (esp_mesh_is_root() || is_mesh_connected) {
        /* Stop timer if we became root or connected */
        if (scan_fail_toggle_timer != NULL) {
            esp_timer_stop(scan_fail_toggle_timer);
            esp_timer_delete(scan_fail_toggle_timer);
            scan_fail_toggle_timer = NULL;
        }
        return;
    }

    /* Toggle between stored color and off */
    scan_toggle_state = !scan_toggle_state;
    if (scan_toggle_state) {
        /* Turn on with stored color */
        mesh_light_set_rgb(scan_toggle_r, scan_toggle_g, scan_toggle_b);
    } else {
        /* Turn off */
        mesh_light_set_colour(0);
    }
}

/* Light control structures */
mesh_light_ctl_t light_on = {
    .cmd = MESH_CMD_LIGHT_ON_OFF,
    .on = 1,
    .token_id = MESH_CONFIG_TOKEN_ID,
    .token_value = MESH_CONFIG_TOKEN_VALUE,
};

mesh_light_ctl_t light_off = {
    .cmd = MESH_CMD_LIGHT_ON_OFF,
    .on = 0,
    .token_id = MESH_CONFIG_TOKEN_ID,
    .token_value = MESH_CONFIG_TOKEN_VALUE,
};

/* Event handler callbacks */
static mesh_event_callback_t root_event_callback = NULL;
static mesh_event_callback_t child_event_callback = NULL;
static ip_event_callback_t root_ip_callback = NULL;

/*******************************************************
 *                Common State Accessors
 *******************************************************/

int mesh_common_get_layer(void)
{
    return mesh_layer;
}

void mesh_common_set_layer(int layer)
{
    mesh_layer = layer;
}

bool mesh_common_is_connected(void)
{
    return is_mesh_connected;
}

bool mesh_common_is_router_connected(void)
{
    return is_router_connected;
}

void mesh_common_set_connected(bool connected)
{
    is_mesh_connected = connected;
}

void mesh_common_get_parent_addr(mesh_addr_t *parent_addr)
{
    if (parent_addr) {
        memcpy(parent_addr, &mesh_parent_addr, sizeof(mesh_addr_t));
    }
}

void mesh_common_set_parent_addr(const mesh_addr_t *parent_addr)
{
    if (parent_addr) {
        memcpy(&mesh_parent_addr, parent_addr, sizeof(mesh_addr_t));
    }
}

esp_netif_t* mesh_common_get_netif_sta(void)
{
    return netif_sta;
}

uint8_t* mesh_common_get_tx_buf(void)
{
    return tx_buf;
}

uint8_t* mesh_common_get_rx_buf(void)
{
    return rx_buf;
}

bool mesh_common_is_running(void)
{
    return is_running;
}

void mesh_common_set_running(bool running)
{
    is_running = running;
}

const char* mesh_common_get_tag(void)
{
    return MESH_TAG;
}

const uint8_t* mesh_common_get_mesh_id(void)
{
    return MESH_ID;
}

/*******************************************************
 *                Event Handler Registration
 *******************************************************/

void mesh_common_register_root_event_callback(mesh_event_callback_t callback)
{
    root_event_callback = callback;
}

void mesh_common_register_child_event_callback(mesh_event_callback_t callback)
{
    child_event_callback = callback;
}

void mesh_common_register_root_ip_callback(ip_event_callback_t callback)
{
    root_ip_callback = callback;
}

/*******************************************************
 *                Common Event Handler
 *******************************************************/

void mesh_common_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint16_t last_layer = 0;
    bool is_root = esp_mesh_is_root();

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        ESP_LOGI(MESH_TAG, "[STARTUP] Mesh network started - Node Status: %s",
                 is_root ? "ROOT NODE" : "NON-ROOT NODE");

        /* Log mesh AP status for root node */
        if (is_root) {
            uint8_t primary_channel = 0;
            wifi_second_chan_t second_channel = WIFI_SECOND_CHAN_NONE;
            esp_wifi_get_channel(&primary_channel, &second_channel);
            bool self_org_enabled = esp_mesh_get_self_organized();
            ESP_LOGI(MESH_TAG, "[ROOT NODE] Mesh AP active - Channel: %d, Layer: %d, Fixed Root: %s, Self-org: %s",
                     primary_channel, mesh_layer, esp_mesh_is_root_fixed() ? "YES" : "NO", self_org_enabled ? "ENABLED" : "DISABLED");
            if (!self_org_enabled) {
                ESP_LOGW(MESH_TAG, "[ROOT NODE] WARNING: Self-organization is DISABLED - root node may not accept child connections!");
            }
        } else {
            bool self_org_enabled = esp_mesh_get_self_organized();
            ESP_LOGI(MESH_TAG, "[NON-ROOT NODE] Searching for parent - Layer: %d, Self-org: %s",
                     mesh_layer, self_org_enabled ? "ENABLED" : "DISABLED");
        }

        /* Start rollback timeout monitoring if rollback flag is set (for root nodes) */
        /* This monitors mesh connection for 5 minutes after rollback */
        /* Leaf nodes will start timeout in MESH_EVENT_PARENT_CONNECTED handler */
        if (is_root) {
            bool rollback_needed = false;
            esp_err_t rollback_check_err = mesh_ota_get_rollback_flag(&rollback_needed);
            if (rollback_check_err == ESP_OK && rollback_needed) {
                /* Rollback flag is set - start timeout task to monitor mesh connection */
                esp_err_t timeout_err = mesh_ota_start_rollback_timeout();
                if (timeout_err != ESP_OK) {
                    ESP_LOGW(MESH_TAG, "Failed to start rollback timeout task: %s", esp_err_to_name(timeout_err));
                } else {
                    ESP_LOGI(MESH_TAG, "Rollback timeout monitoring started (root node, mesh started)");
                }
            }
        }

        /* Check if forced root node has become non-root (violation of GPIO forcing) */
        if (is_root_node_forced && !is_root) {
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] ERROR: Forced root node (GPIO %d=LOW) has become NON-ROOT NODE - this violates GPIO forcing configuration!", MESH_GPIO_FORCE_ROOT);
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] Rebooting device to enforce root node behavior...");
            vTaskDelay(pdMS_TO_TICKS(1000)); /* Give time for log message to be sent */
            esp_restart();
        }

        /* Ensure LED shows unconnected state - check router connection for root */
        if (is_root) {
            /* Root node: LED base color indicates router connection status */
            if (is_router_connected) {
                mesh_light_set_colour(MESH_LIGHT_GREEN);
            } else {
                mesh_light_set_colour(MESH_LIGHT_ORANGE);
            }
        } else {
            mesh_light_set_colour(MESH_LIGHT_RED);
        }
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        /* Turn LED back to unconnected state - check router connection for root */
        if (is_root) {
            /* Root node: LED base color indicates router connection status */
            if (is_router_connected) {
                mesh_light_set_colour(MESH_LIGHT_GREEN);
            } else {
                mesh_light_set_colour(MESH_LIGHT_ORANGE);
            }
        } else {
            mesh_light_set_colour(MESH_LIGHT_RED);
        }
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        int routing_table_size = esp_mesh_get_routing_table_size();
        int child_count_after_connect = (routing_table_size > 0) ? (routing_table_size - 1) : 0;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR", routing table size: %d",
                 child_connected->aid,
                 MAC2STR(child_connected->mac), routing_table_size);
        ESP_LOGI(MESH_TAG, "[ROOT ACTION] Child node connected - Total nodes in mesh: %d", routing_table_size);

        // #region agent log
        if (is_root) {
            ESP_LOGI(MESH_TAG, "[DEBUG HYP-D] CHILD_CONNECTED event - route_size:%d child_count:%d",
                     routing_table_size, child_count_after_connect);
            /* Log root state when child connects */
            bool is_root_fixed_now = esp_mesh_is_root_fixed();
            bool self_org_now = esp_mesh_get_self_organized();
            ESP_LOGI(MESH_TAG, "[DEBUG CHILD-CONN] Root state on child connect - is_root_fixed:%d, self_org:%d",
                     is_root_fixed_now ? 1 : 0, self_org_now ? 1 : 0);
        }
        // #endregion
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
        if (is_root && root_event_callback) {
            root_event_callback(arg, event_base, event_id, event_data);
        }
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
        if (is_root && root_event_callback) {
            root_event_callback(arg, event_base, event_id, event_data);
        }
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
        if (is_root && root_event_callback) {
            root_event_callback(arg, event_base, event_id, event_data);
        }
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND: {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);

        /* Log diagnostic information for non-root nodes */
        if (!esp_mesh_is_root()) {
            uint8_t primary_channel = 0;
            wifi_second_chan_t second_channel = WIFI_SECOND_CHAN_NONE;
            esp_wifi_get_channel(&primary_channel, &second_channel);
            ESP_LOGW(MESH_TAG, "[NON-ROOT DIAGNOSTIC] No parent found after %d scans - Current channel: %d, Mesh ID: %02x:%02x:%02x:%02x:%02x:%02x",
                     no_parent->scan_times, primary_channel,
                     MESH_ID[0], MESH_ID[1], MESH_ID[2], MESH_ID[3], MESH_ID[4], MESH_ID[5]);
        }

        /* For non-root nodes: turn off LED for 250ms when searching for root */
        if (!esp_mesh_is_root()) {
            /* Cancel any existing restore task to prevent multiple tasks */
            if (led_restore_task_handle != NULL) {
                vTaskDelete(led_restore_task_handle);
                led_restore_task_handle = NULL;
            }
            mesh_light_set_colour(0); /* Turn off LED */
            /* Create a task to restore red LED after 250ms */
            xTaskCreate(led_restore_red_task, "led_restore", 2048, NULL, 1, &led_restore_task_handle);
        }
    }
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR", duty:%d",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 is_root ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr), connected->duty);
        last_layer = mesh_layer;
        is_mesh_connected = true;

        /* Start rollback timeout monitoring if rollback flag is set (for leaf nodes) */
        /* This monitors mesh connection for 5 minutes after rollback */
        /* Root nodes start timeout in MESH_EVENT_STARTED handler */
        if (!is_root) {
            bool rollback_needed = false;
            esp_err_t rollback_check_err = mesh_ota_get_rollback_flag(&rollback_needed);
            if (rollback_check_err == ESP_OK && rollback_needed) {
                /* Rollback flag is set - start timeout task to monitor mesh connection */
                esp_err_t timeout_err = mesh_ota_start_rollback_timeout();
                if (timeout_err != ESP_OK) {
                    ESP_LOGW(MESH_TAG, "Failed to start rollback timeout task: %s", esp_err_to_name(timeout_err));
                } else {
                    ESP_LOGI(MESH_TAG, "Rollback timeout monitoring started (leaf node, connected to mesh)");
                }
            }
        }

        // #region agent log
        if (is_root) {
            esp_netif_t *netif_sta = mesh_common_get_netif_sta();
            ESP_LOGI(MESH_TAG, "[DEBUG HYP-1] MESH_EVENT_PARENT_CONNECTED (root) - netif_sta:%p, will call root_event_callback", netif_sta);
            if (netif_sta != NULL) {
                esp_netif_ip_info_t ip_info;
                esp_err_t get_ip_err = esp_netif_get_ip_info(netif_sta, &ip_info);
                ESP_LOGI(MESH_TAG, "[DEBUG HYP-1] Current IP info before callback - get_ip_info:0x%x, ip:" IPSTR,
                         get_ip_err, IP2STR(&ip_info.ip));
            }
        }
        // #endregion
        /* Cancel any pending LED restore task since we're now connected */
        if (led_restore_task_handle != NULL) {
            vTaskDelete(led_restore_task_handle);
            led_restore_task_handle = NULL;
        }
        /* Stop scan failure toggle timer since we're now connected */
        if (scan_fail_toggle_timer != NULL) {
            esp_timer_stop(scan_fail_toggle_timer);
            esp_timer_delete(scan_fail_toggle_timer);
            scan_fail_toggle_timer = NULL;
        }
        /* Set LED state when connected - heartbeat will take over from here */
        if (is_root) {
            /* Root node: LED base color is controlled by router connection status (IP events)
             * Don't change LED here - router connection status (GREEN/ORANGE) is the base color
             * Heartbeat will add white blink when mesh nodes are present */
        } else {
            /* Non-root node: turn off LED (heartbeat will control it) */
            mesh_light_set_colour(0);
        }
        if (is_root && root_event_callback) {
            root_event_callback(arg, event_base, event_id, event_data);
        }
        mesh_common_comm_p2p_start();
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        /* Cleanup OTA reception if in progress */
        mesh_ota_cleanup_on_disconnect();
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        /* Cancel any pending LED restore task */
        if (led_restore_task_handle != NULL) {
            vTaskDelete(led_restore_task_handle);
            led_restore_task_handle = NULL;
        }
        /* Stop scan failure toggle timer if running (non-root node disconnected) */
        if (scan_fail_toggle_timer != NULL) {
            esp_timer_stop(scan_fail_toggle_timer);
            esp_timer_delete(scan_fail_toggle_timer);
            scan_fail_toggle_timer = NULL;
        }
        /* Turn LED back to unconnected state - check router connection for root */
        if (is_root) {
            /* For forced root nodes: If router was connected, ensure root stays fixed
             * and router connection is maintained. Parent disconnection can occur due to
             * network issues, but router connection should persist (self-organization
             * is enabled with select_parent=false to prevent router disconnection). */
            if (is_root_node_forced && is_router_connected) {
                /* Verify root is still fixed */
                bool is_root_fixed = esp_mesh_is_root_fixed();
                if (!is_root_fixed) {
                    ESP_LOGW(MESH_TAG, "[ROOT ACTION] Parent disconnected - root became unfixed, re-fixing...");
                    esp_err_t fix_err = esp_mesh_fix_root(true);
                    if (fix_err != ESP_OK) {
                        ESP_LOGE(MESH_TAG, "[ROOT ACTION] ERROR: Failed to re-fix root: 0x%x", fix_err);
                    } else {
                        ESP_LOGI(MESH_TAG, "[ROOT ACTION] Root re-fixed successfully");
                    }
                }
                /* Router connection should be maintained - LED stays GREEN if router still connected */
                mesh_light_set_colour(MESH_LIGHT_GREEN);
            } else if (is_router_connected) {
                mesh_light_set_colour(MESH_LIGHT_GREEN);
            } else {
                mesh_light_set_colour(MESH_LIGHT_ORANGE);
            }
        } else {
            mesh_light_set_colour(MESH_LIGHT_RED);
        }
    }
    break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        mesh_layer = layer_change->new_layer;
        bool is_root_now = esp_mesh_is_root();
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 is_root_now ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        ESP_LOGI(MESH_TAG, "[STATUS CHANGE] Layer: %d -> %d | Node Type: %s",
                 last_layer, mesh_layer, is_root_now ? "ROOT NODE" : "NON-ROOT NODE");

        /* Check if forced mesh node has become root (violation of GPIO forcing) */
        if (is_mesh_node_forced && is_root_now) {
            ESP_LOGE(MESH_TAG, "[GPIO NODE FORCING] ERROR: Forced mesh node (GPIO %d=LOW) has become ROOT NODE - this violates GPIO forcing configuration!", MESH_GPIO_FORCE_MESH);
            ESP_LOGE(MESH_TAG, "[GPIO NODE FORCING] Rebooting device to enforce mesh node behavior...");
            vTaskDelay(pdMS_TO_TICKS(1000)); /* Give time for log message to be sent */
            esp_restart();
        }

        /* Check if forced root node has become non-root (violation of GPIO forcing) */
        if (is_root_node_forced && !is_root_now) {
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] ERROR: Forced root node (GPIO %d=LOW) has become NON-ROOT NODE - this violates GPIO forcing configuration!", MESH_GPIO_FORCE_ROOT);
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] Rebooting device to enforce root node behavior...");
            vTaskDelay(pdMS_TO_TICKS(1000)); /* Give time for log message to be sent */
            esp_restart();
        }

        last_layer = mesh_layer;
        if (is_root_now && root_event_callback) {
            root_event_callback(arg, event_base, event_id, event_data);
        }
    }
    break;
    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(root_addr->addr));
    }
    break;
    case MESH_EVENT_VOTE_STARTED: {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                 switch_req->reason,
                 MAC2STR( switch_req->rc_addr.addr));
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        bool is_root_now = esp_mesh_is_root();
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
        ESP_LOGI(MESH_TAG, "[STATUS CHANGE] Root switch acknowledged - Node Type: %s",
                 is_root_now ? "ROOT NODE" : "NON-ROOT NODE");

        /* Check if forced root node has become non-root (violation of GPIO forcing) */
        if (is_root_node_forced && !is_root_now) {
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] ERROR: Forced root node (GPIO %d=LOW) has become NON-ROOT NODE after root switch - this violates GPIO forcing configuration!", MESH_GPIO_FORCE_ROOT);
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] Rebooting device to enforce root node behavior...");
            vTaskDelay(pdMS_TO_TICKS(1000)); /* Give time for log message to be sent */
            esp_restart();
        }

        if (is_root_now && root_event_callback) {
            root_event_callback(arg, event_base, event_id, event_data);
        }
    }
    break;
    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    case MESH_EVENT_ROOT_FIXED: {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    case MESH_EVENT_ROOT_ASKED_YIELD: {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    case MESH_EVENT_CHANNEL_SWITCH: {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE: {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE: {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    case MESH_EVENT_STOP_RECONNECTION: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    case MESH_EVENT_FIND_NETWORK: {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                 find_network->channel, MAC2STR(find_network->router_bssid));

        /* For non-root nodes: Start LED toggle timer on each scan attempt (failed connection) */
        if (!esp_mesh_is_root() && !is_mesh_connected) {
            /* Stop existing timer if any */
            if (scan_fail_toggle_timer != NULL) {
                esp_timer_stop(scan_fail_toggle_timer);
                esp_timer_delete(scan_fail_toggle_timer);
                scan_fail_toggle_timer = NULL;
            }

            /* Store current color (RED for non-root nodes scanning) */
            scan_toggle_r = 155;
            scan_toggle_g = 0;
            scan_toggle_b = 0;
            scan_toggle_state = false; /* Start with LED off */

            /* Create and start periodic timer to toggle LED on each failed attempt (~300ms based on logs) */
            esp_timer_create_args_t timer_args = {
                .callback = scan_fail_toggle_timer_cb,
                .name = "scan_fail_toggle"
            };
            esp_err_t timer_err = esp_timer_create(&timer_args, &scan_fail_toggle_timer);
            if (timer_err == ESP_OK) {
                esp_err_t start_err = esp_timer_start_periodic(scan_fail_toggle_timer, 300 * 1000); /* Toggle every 300ms (300000 microseconds) */
                if (start_err != ESP_OK) {
                    ESP_LOGE(MESH_TAG, "[SCAN FAIL TOGGLE] Failed to start timer: 0x%x", start_err);
                    esp_timer_delete(scan_fail_toggle_timer);
                    scan_fail_toggle_timer = NULL;
                }
            } else {
                ESP_LOGE(MESH_TAG, "[SCAN FAIL TOGGLE] Failed to create timer: 0x%x", timer_err);
            }
        }
    }
    break;
    case MESH_EVENT_ROUTER_SWITCH: {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
    case MESH_EVENT_PS_PARENT_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
    }
    break;
    case MESH_EVENT_PS_CHILD_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, "MACSTR", duty:%d", ps_duty->child_connected.aid-1,
                MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
    }
    break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%" PRId32 "", event_id);
        break;
    }
}

/*******************************************************
 *                IP Event Handler
 *******************************************************/

void mesh_common_ip_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    bool is_root = esp_mesh_is_root();

    // #region agent log
    ESP_LOGI(MESH_TAG, "[DEBUG HYP-5] IP event handler called - event_id:%ld, is_root:%d",
             (long)event_id, is_root ? 1 : 0);
    // #endregion

    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(MESH_TAG, "[STARTUP] IP address obtained - Node Type: %s",
                 is_root ? "ROOT NODE" : "NON-ROOT NODE");

        ESP_LOGD(MESH_TAG, "[DEBUG] IP_EVENT_STA_GOT_IP received - is_root:%d, is_mesh_node_forced:%d, ip:" IPSTR,
                 is_root ? 1 : 0, is_mesh_node_forced ? 1 : 0, IP2STR(&event->ip_info.ip));

        /* Check if forced mesh node has become root (violation of GPIO forcing) */
        if (is_mesh_node_forced && is_root) {
            ESP_LOGE(MESH_TAG, "[GPIO NODE FORCING] ERROR: Forced mesh node (GPIO %d=LOW) has become ROOT NODE - this violates GPIO forcing configuration!", MESH_GPIO_FORCE_MESH);
            ESP_LOGE(MESH_TAG, "[GPIO NODE FORCING] Rebooting device to enforce mesh node behavior...");
            vTaskDelay(pdMS_TO_TICKS(1000)); /* Give time for log message to be sent */
            esp_restart();
        }

        // #region agent log
        ESP_LOGI(MESH_TAG, "[DEBUG HYP-5] IP_EVENT_STA_GOT_IP received - is_root:%d, is_root_node_forced:%d",
                 is_root ? 1 : 0, is_root_node_forced ? 1 : 0);
        // #endregion

        if (is_root) {
            /* Root node: router connected - set GREEN base color */
            is_router_connected = true;
            mesh_light_set_colour(MESH_LIGHT_GREEN);

            /* Enable self-organization AFTER router connection is established
             * This allows the root node to accept child connections.
             * For fixed root nodes, we ensure the root stays fixed to prevent router disconnection. */
            if (is_root_node_forced) {
                // #region agent log
                ESP_LOGI(MESH_TAG, "[DEBUG HYP-3] About to enable self-organization - is_root_fixed check");
                // #endregion

                /* Verify root is still fixed before enabling self-organization */
                bool is_root_fixed = esp_mesh_is_root_fixed();
                if (!is_root_fixed) {
                    ESP_LOGE(MESH_TAG, "[ROOT ACTION] ERROR: Root is not fixed! Re-fixing root before enabling self-organization...");
                    esp_err_t fix_err = esp_mesh_fix_root(true);
                    if (fix_err != ESP_OK) {
                        ESP_LOGE(MESH_TAG, "[ROOT ACTION] ERROR: Failed to re-fix root: 0x%x", fix_err);
                    }
                }

                /* Small delay to ensure router connection is stable before enabling self-organization */
                vTaskDelay(pdMS_TO_TICKS(100));

                ESP_LOGI(MESH_TAG, "[ROOT ACTION] Router connected - enabling self-organization for child connections...");
                /* For fixed root nodes: Use select_parent=false to enable self-organization
                 * without causing the root to disconnect from router and search for a parent.
                 * This allows child nodes to connect while maintaining router connection. */
                esp_err_t self_org_err = esp_mesh_set_self_organized(true, false);

                // #region agent log
                ESP_LOGI(MESH_TAG, "[DEBUG HYP-3] esp_mesh_set_self_organized(true, false) result:0x%x", self_org_err);
                // #endregion

                if (self_org_err != ESP_OK) {
                    ESP_LOGE(MESH_TAG, "[ROOT ACTION] ERROR: Failed to enable self-organization: 0x%x", self_org_err);
                } else {
                    /* Verify root is still fixed after enabling self-organization */
                    bool is_root_fixed_after = esp_mesh_is_root_fixed();
                    if (!is_root_fixed_after) {
                        ESP_LOGE(MESH_TAG, "[ROOT ACTION] ERROR: Root became unfixed after enabling self-organization! Re-fixing...");
                        esp_err_t fix_err = esp_mesh_fix_root(true);
                        if (fix_err != ESP_OK) {
                            ESP_LOGE(MESH_TAG, "[ROOT ACTION] ERROR: Failed to re-fix root: 0x%x", fix_err);
                        }
                    }

                    bool self_org_enabled = esp_mesh_get_self_organized();

                    // #region agent log
                    ESP_LOGI(MESH_TAG, "[DEBUG HYP-3] After self-org enable - is_root_fixed:%d, self_org_enabled:%d",
                             is_root_fixed_after ? 1 : 0, self_org_enabled ? 1 : 0);
                    // #endregion

                    if (self_org_enabled) {
                        ESP_LOGI(MESH_TAG, "[ROOT ACTION] Self-organization enabled - root node can now accept child connections");

                        // #region agent log
                        /* Log root node state for child connection debugging */
                        uint8_t primary_channel = 0;
                        wifi_second_chan_t second_channel = WIFI_SECOND_CHAN_NONE;
                        esp_wifi_get_channel(&primary_channel, &second_channel);
                        int routing_table_size = esp_mesh_get_routing_table_size();
                        ESP_LOGI(MESH_TAG, "[DEBUG CHILD-CONN] Root ready for children - is_root_fixed:%d, self_org:%d, channel:%d, route_size:%d",
                                 is_root_fixed_after ? 1 : 0, self_org_enabled ? 1 : 0, primary_channel, routing_table_size);
                        // #endregion
                    } else {
                        ESP_LOGE(MESH_TAG, "[ROOT ACTION] ERROR: Self-organization enable failed verification");
                    }
                }
            }

            if (root_ip_callback) {
                root_ip_callback(arg, event_base, event_id, event_data);
            }
        }
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_LOST_IP>");

        if (is_root) {
            /* Root node: router disconnected - set ORANGE */
            is_router_connected = false;
            mesh_light_set_colour(MESH_LIGHT_ORANGE);

            /* Disable self-organization when router disconnects to prevent
             * root from searching for a parent instead of reconnecting to router */
            if (is_root_node_forced) {
                ESP_LOGI(MESH_TAG, "[ROOT ACTION] Router disconnected - disabling self-organization to allow router reconnection...");
                ESP_ERROR_CHECK(esp_mesh_set_self_organized(false, false));
            }
        }
    }
}

/*******************************************************
 *                P2P Communication
 *******************************************************/

/* Forward declaration - implemented in mesh_child.c */
extern void esp_mesh_p2p_rx_main(void *arg);

esp_err_t mesh_common_comm_p2p_start(void)
{
    static bool is_comm_p2p_started = false;
    if (!is_comm_p2p_started) {
        is_comm_p2p_started = true;
        xTaskCreate(esp_mesh_p2p_rx_main, "MPRX", 3072, NULL, 5, NULL);
    }
    return ESP_OK;
}

/*******************************************************
 *                Common Initialization
 *******************************************************/

esp_err_t mesh_common_init(void)
{
    ESP_LOGI(MESH_TAG, "========================================");
    ESP_LOGI(MESH_TAG, "Mesh Node Starting Up");
    ESP_LOGI(MESH_TAG, "========================================");

    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  create network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));

    // #region agent log
    ESP_LOGI(MESH_TAG, "[DEBUG HYP-2] Netif created - netif_sta:%p", netif_sta);
    if (netif_sta != NULL) {
        esp_netif_ip_info_t ip_info;
        esp_err_t get_ip_err = esp_netif_get_ip_info(netif_sta, &ip_info);
        ESP_LOGI(MESH_TAG, "[DEBUG HYP-2] Initial netif IP info - get_ip_info result:0x%x, ip:" IPSTR,
                 get_ip_err, IP2STR(&ip_info.ip));

        /* Ensure DHCP client is enabled for STA interface (needed for root node to get IP) */
        esp_err_t dhcp_start_err = esp_netif_dhcpc_start(netif_sta);
        ESP_LOGI(MESH_TAG, "[DEBUG HYP-2] DHCP client start (initial) - result:0x%x", dhcp_start_err);
    }
    // #endregion
    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &mesh_common_ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &mesh_common_ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_common_event_handler, NULL));
    /*  set mesh topology */
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    /*  set mesh max layer according to the topology */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
#ifdef CONFIG_MESH_ENABLE_PS
    /* Enable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_enable_ps());
    /* better to increase the associate expired time, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
    /* better to increase the announce interval to avoid too much management traffic, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    /* Reduced from 10 to 3 seconds for faster disconnection detection */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(3));
#endif
    /* Apply GPIO-based root forcing before mesh starts */
    /* Read GPIO pins directly to determine the desired behavior */
    bool force_root = mesh_gpio_read_root_force();
    int level_root = 1; /* Default to HIGH if GPIO not initialized */
    int level_mesh = 1; /* Default to HIGH if GPIO not initialized */
    if (mesh_gpio_is_initialized()) {
        level_root = gpio_get_level(MESH_GPIO_FORCE_ROOT);
        level_mesh = gpio_get_level(MESH_GPIO_FORCE_MESH);
    }

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = MESH_CONFIG_MESH_CHANNEL;

    /* For root nodes: Override channel to 0 (auto-adapt) to match router channel immediately
     * This prevents long delays when router channel differs from configured mesh channel */
    if (force_root && cfg.channel != 0) {
        ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] Overriding mesh channel from %d to 0 (auto-adapt) for root node to match router channel", cfg.channel);
        cfg.channel = 0;
    }

    /* Configure router for all nodes (required by esp_mesh_set_config)
     * For forced mesh nodes, router connection will be prevented by disabling self-organization below */
    cfg.router.ssid_len = strlen(MESH_CONFIG_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, MESH_CONFIG_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, MESH_CONFIG_ROUTER_PASSWORD,
           strlen(MESH_CONFIG_ROUTER_PASSWORD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(MESH_CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, MESH_CONFIG_MESH_AP_PASSWORD, strlen(MESH_CONFIG_MESH_AP_PASSWORD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

    /* Log mesh configuration for debugging */
    ESP_LOGI(MESH_TAG, "[MESH CONFIG] Mesh ID: %02x:%02x:%02x:%02x:%02x:%02x, Channel: %d, AP Password length: %d, AP Auth Mode: %d, Max Connections: %d",
             MESH_ID[0], MESH_ID[1], MESH_ID[2], MESH_ID[3], MESH_ID[4], MESH_ID[5],
             cfg.channel, (int)strlen(MESH_CONFIG_MESH_AP_PASSWORD), MESH_CONFIG_MESH_AP_AUTHMODE, cfg.mesh_ap.max_connection);

    /* Note: Channel switching is enabled by default in ESP-MESH. No explicit call needed. */

    if (force_root) {
        /* GPIO 5 LOW (and GPIO 4 HIGH): Force root node
         * According to ESP-IDF documentation, when forcing root node:
         * 1. Disable self-organized networking (allows manual configuration)
         * 2. Enable fixed root setting (prevents automatic root election)
         */
        ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] Configuring for root node (GPIO %d=LOW, GPIO %d=HIGH/floating)",
                 MESH_GPIO_FORCE_ROOT, MESH_GPIO_FORCE_MESH);
        /* For forcing root node, according to ESP-IDF documentation:
         * 1. Enable self-organized networking (required for root to accept child connections)
         *    - Even with fixed root, self-organization must be enabled for mesh AP to accept connections
         * 2. Set device type to MESH_ROOT (explicitly declares this device as root)
         * 3. Enable fixed root setting (prevents automatic root election)
         */
        ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] Setting device type to MESH_ROOT...");
        ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
        ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] Enabling fixed root...");
        esp_err_t fix_root_err = esp_mesh_fix_root(true);
        if (fix_root_err != ESP_OK) {
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] esp_mesh_fix_root(true) FAILED: %s (0x%x)", esp_err_to_name(fix_root_err), fix_root_err);
        } else {
            ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] esp_mesh_fix_root(true) returned ESP_OK");
            // Verify that root fixing actually took effect
            bool is_root_fixed_after_call = esp_mesh_is_root_fixed();
            if (!is_root_fixed_after_call) {
                ESP_LOGW(MESH_TAG, "[GPIO ROOT FORCING] WARNING: esp_mesh_fix_root(true) returned ESP_OK, but esp_mesh_is_root_fixed() returns FALSE!");
            } else {
                ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] Verified: esp_mesh_is_root_fixed() = TRUE after esp_mesh_fix_root(true)");
            }
        }
        ESP_ERROR_CHECK(fix_root_err);
        /* For fixed root nodes: Do NOT enable self-organization
         * - Fixed root nodes can accept child connections without self-organization
         * - Enabling self-organization causes the root to search for a parent, which interferes with router connection
         * - The mesh AP will accept child connections automatically when the node is fixed as root */
        ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] Root node behavior forced via GPIO (GPIO %d=LOW) - type=MESH_ROOT, fixed root enabled (self-organization disabled for router connection)", MESH_GPIO_FORCE_ROOT);
        is_root_node_forced = true; /* Mark that root node forcing is active */
    } else if (mesh_gpio_is_initialized() && level_mesh == 0 && level_root != 0) {
        /* GPIO 4 LOW (and GPIO 5 HIGH): Force mesh/child node - prevent root election
         * To allow connection to parent mesh nodes, we:
         * 1. Enable self-organization with parent selection (esp_mesh_set_self_organized(true, true))
         *    - This allows the node to search for and connect to parent mesh nodes
         *    - select_parent=true enables parent selection/search functionality
         * 2. Set device type to MESH_LEAF to prevent root election
         * 3. Release root fixing (ensures node can connect as child)
         * Note: Router config must be set in mesh_cfg_t (required by esp_mesh_set_config),
         * but the node will connect to a parent mesh node, not directly to the router.
         */
        ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Configuring for mesh node (GPIO %d=LOW, GPIO %d=HIGH/floating)",
                 MESH_GPIO_FORCE_MESH, MESH_GPIO_FORCE_ROOT);

        ESP_LOGD(MESH_TAG, "[DEBUG] Before mesh node forcing config - router_ssid:%.*s, router_ssid_len:%d",
                 cfg.router.ssid_len, cfg.router.ssid, cfg.router.ssid_len);

        ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Enabling self-organization with parent selection for mesh connection...");
        esp_err_t self_org_err = esp_mesh_set_self_organized(true, true);

        ESP_LOGD(MESH_TAG, "[DEBUG] After esp_mesh_set_self_organized(true, true) - err:%s (0x%x)",
                 esp_err_to_name(self_org_err), self_org_err);

        ESP_ERROR_CHECK(self_org_err);

        ESP_LOGD(MESH_TAG, "[DEBUG] Before setting MESH_LEAF type");

        ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Setting device type to MESH_LEAF to prevent root election...");
        esp_err_t set_type_err = esp_mesh_set_type(MESH_LEAF);

        ESP_LOGD(MESH_TAG, "[DEBUG] After esp_mesh_set_type(MESH_LEAF) - err:%s (0x%x)",
                 esp_err_to_name(set_type_err), set_type_err);

        if (set_type_err != ESP_OK) {
            ESP_LOGW(MESH_TAG, "[GPIO NODE FORCING] Warning: esp_mesh_set_type(MESH_LEAF) returned %s (0x%x)", esp_err_to_name(set_type_err), set_type_err);
        }

        /* Note: esp_mesh_set_disallow_level() does not exist in ESP-IDF API.
         * Root election prevention is already handled by:
         * - esp_mesh_set_type(MESH_LEAF) - sets device type to leaf (prevents root election)
         * - esp_mesh_set_self_organized(true, true) - enables self-organization with parent selection
         * - esp_mesh_fix_root(false) - releases root fixing
         */

        ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Releasing root fixing to ensure non-root behavior...");
        esp_err_t fix_root_err = esp_mesh_fix_root(false);
        if (fix_root_err != ESP_OK && fix_root_err != ESP_ERR_MESH_NOT_INIT) {
            ESP_LOGW(MESH_TAG, "[GPIO NODE FORCING] Warning: esp_mesh_fix_root(false) returned %s (0x%x) - this is usually fine", esp_err_to_name(fix_root_err), fix_root_err);
        }
        ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Mesh node behavior forced via GPIO (GPIO %d=LOW) - self-organization enabled with parent selection for mesh connection", MESH_GPIO_FORCE_MESH);
        is_mesh_node_forced = true; /* Mark that mesh node forcing is active */
    } else {
        /* Both HIGH, both LOW (conflict), or GPIO not initialized: Normal root election - ensure self-organization is enabled */
        ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, true));
        /* Explicitly release root forcing to allow normal root election */
        esp_err_t err = esp_mesh_fix_root(false);
        if (err == ESP_OK) {
            if (mesh_gpio_is_initialized()) {
                if (level_root == 0 && level_mesh == 0) {
                    ESP_LOGW(MESH_TAG, "[GPIO NODE FORCING] GPIO conflict detected (both pins LOW) - normal root election enabled");
                } else {
                    ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Normal root election enabled (both GPIO pins HIGH)");
                }
            } else {
                ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Normal root election enabled (GPIO not initialized, using default behavior)");
            }
        }
        /* If root was not previously fixed, this may return ESP_ERR_MESH_NOT_INIT or similar,
         * which is fine - we just want to ensure root forcing is disabled */
    }

    // disable wifi power saving...
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* Check root fixing status before starting mesh (for diagnostics) */
    if (force_root) {
        bool is_root_fixed_before_start = esp_mesh_is_root_fixed();
        ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING] Before esp_mesh_start(): is_root_fixed=%d", is_root_fixed_before_start ? 1 : 0);
        if (!is_root_fixed_before_start) {
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING] ERROR: is_root_fixed is FALSE before mesh start! esp_mesh_fix_root() may not have worked.");
        }
    }

    /* mesh start */
    ESP_LOGI(MESH_TAG, "[GPIO NODE FORCING] Starting mesh network...");

    bool is_root_before = esp_mesh_is_root();
    bool is_root_fixed_before = esp_mesh_is_root_fixed();
    ESP_LOGD(MESH_TAG, "[DEBUG] Before esp_mesh_start() - is_root:%d, is_root_fixed:%d, is_mesh_node_forced:%d",
             is_root_before ? 1 : 0, is_root_fixed_before ? 1 : 0, is_mesh_node_forced ? 1 : 0);

    ESP_ERROR_CHECK(esp_mesh_start());

    bool is_root_after = esp_mesh_is_root();
    bool is_root_fixed_after = esp_mesh_is_root_fixed();
    ESP_LOGD(MESH_TAG, "[DEBUG] After esp_mesh_start() - is_root:%d, is_root_fixed:%d, is_mesh_node_forced:%d",
             is_root_after ? 1 : 0, is_root_fixed_after ? 1 : 0, is_mesh_node_forced ? 1 : 0);
#ifdef CONFIG_MESH_ENABLE_PS
    /* set the device active duty cycle. (default:10, MESH_PS_DEVICE_DUTY_REQUEST) */
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    /* set the network active duty cycle. (default:10, -1, MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
    ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION, CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif

    init_rgb_led();

    bool is_root = esp_mesh_is_root();
    bool is_root_fixed = esp_mesh_is_root_fixed();
    /* Reuse GPIO forcing state from earlier read (GPIO state doesn't change during operation) */
    bool gpio_forced = force_root;

    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%" PRId32 ", %s<%d>%s, ps:%d",  esp_get_minimum_free_heap_size(),
             is_root_fixed ? "root fixed" : "root not fixed",
             esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());

    /* Diagnostic logging for root forcing */
    if (gpio_forced) {
        bool self_org_after_start = esp_mesh_get_self_organized();
        ESP_LOGI(MESH_TAG, "[GPIO ROOT FORCING DIAGNOSTIC] GPIO forced root requested, after mesh_start: is_root=%d, is_root_fixed=%d, self_org=%d",
                 is_root ? 1 : 0, is_root_fixed ? 1 : 0, self_org_after_start ? 1 : 0);
        if (!is_root) {
            ESP_LOGW(MESH_TAG, "[GPIO ROOT FORCING DIAGNOSTIC] WARNING: GPIO forced root, but device is NOT root! is_root_fixed=%d", is_root_fixed ? 1 : 0);
        }
        if (!is_root_fixed) {
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING DIAGNOSTIC] ERROR: GPIO forced root, but is_root_fixed is FALSE! This indicates esp_mesh_fix_root() did not take effect.");
        }
        if (!self_org_after_start) {
            ESP_LOGE(MESH_TAG, "[GPIO ROOT FORCING DIAGNOSTIC] ERROR: Self-organization is DISABLED after mesh_start! Root node will not accept child connections.");
        }
    }

    if (gpio_forced) {
        if (is_root) {
            ESP_LOGI(MESH_TAG, "[STARTUP] Mesh started - Node Type: ROOT NODE (GPIO-FORCED) | Heap: %" PRId32 " bytes", esp_get_minimum_free_heap_size());
        } else {
            ESP_LOGW(MESH_TAG, "[STARTUP] Mesh started - Node Type: NON-ROOT NODE (GPIO-FORCED ROOT FAILED - is_root_fixed=%d) | Heap: %" PRId32 " bytes",
                     is_root_fixed ? 1 : 0, esp_get_minimum_free_heap_size());
        }
    } else {
        ESP_LOGI(MESH_TAG, "[STARTUP] Mesh started - Node Type: %s | Heap: %" PRId32 " bytes",
                 is_root ? "ROOT NODE" : "NON-ROOT NODE", esp_get_minimum_free_heap_size());
    }
    ESP_LOGI(MESH_TAG, "========================================");

    return ESP_OK;
}
