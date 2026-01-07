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
#include "config/mesh_config.h"
#include "mesh_common.h"
#include "config/mesh_device_config.h"
#include "mesh_commands.h"
#include "light_neopixel.h"
#include "light_common_cathode.h"
#include "mesh_ota.h"
#include "mesh_udp_bridge.h"
#include "plugin_system.h"
#ifdef ROOT_STATUS_LED_GPIO
#include "root_status_led.h"
#endif
#include "mesh_udp_bridge.h"
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
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_netif_t *netif_sta = NULL;

/* Core local heartbeat counter - runs on all nodes (root and child) */
static uint8_t local_heartbeat_counter = 0;
static esp_timer_handle_t local_heartbeat_timer = NULL;

/* Task function for non-blocking registration on role change */
static void registration_task(void *pvParameters)
{
    esp_err_t reg_err = mesh_udp_bridge_register();
    if (reg_err != ESP_OK && reg_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(MESH_TAG, "[REGISTRATION] Registration failed on role change: %s", esp_err_to_name(reg_err));
    }
    vTaskDelete(NULL);
}

/* Scan failure toggle timer removed - RGB LEDs are now exclusive to plugins
 * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
 */

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
    static bool was_root_before = false; /* Track previous root status for heartbeat management */
    bool is_root = esp_mesh_is_root();

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        ESP_LOGI(MESH_TAG, "[STARTUP] Mesh network started - Node Status: %s",
                 is_root ? "ROOT NODE" : "NON-ROOT NODE");
        /* Initialize previous root status tracking */
        was_root_before = is_root;

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

        /* RGB LED control removed - LEDs are now exclusive to plugins
         * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
         */

#ifdef ROOT_STATUS_LED_GPIO
        /* Update status LED based on current mesh role (mesh role is now known) */
        root_status_led_update();
#endif
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        /* RGB LED control removed - LEDs are now exclusive to plugins
         * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
         */
#ifdef ROOT_STATUS_LED_GPIO
        /* Update status LED - mesh stopped, node is no longer root */
        root_status_led_update();
#endif
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
#ifdef ROOT_STATUS_LED_GPIO
        /* Update status LED pattern when node count changes */
        if (is_root) {
            root_status_led_update_status();
        }
#endif
        /* If root node, send active plugin state to newly joined child node */
        if (is_root) {
            /* Add small delay to allow node to fully initialize before receiving commands */
            vTaskDelay(pdMS_TO_TICKS(200));
            /* Convert child MAC address to mesh_addr_t */
            mesh_addr_t child_addr = {0};
            memcpy(child_addr.addr, child_connected->mac, 6);
            /* Send active plugin START command to newly joined node */
            plugin_send_start_to_node(&child_addr);
        }
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
#ifdef ROOT_STATUS_LED_GPIO
        /* Update status LED pattern when node count changes */
        if (is_root) {
            root_status_led_update_status();
        }
#endif
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
#ifdef ROOT_STATUS_LED_GPIO
        /* Update status LED pattern when node count changes */
        if (is_root) {
            root_status_led_update_status();
        }
#endif
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
#ifdef ROOT_STATUS_LED_GPIO
        /* Update status LED pattern when node count changes */
        if (is_root) {
            root_status_led_update_status();
        }
#endif
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

        /* RGB LED control removed - LEDs are now exclusive to plugins
         * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
         */
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
        /* RGB LED control removed - LEDs are now exclusive to plugins
         * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
         */
        /* RGB LED control removed - LEDs are now exclusive to plugins
         * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
         * Heartbeat no longer controls RGB LEDs
         */
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
        /* RGB LED control removed - LEDs are now exclusive to plugins
         * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
         */
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

        /* Handle heartbeat, state updates, broadcast listener, and API listener on role change */
        if (was_root_before && !is_root_now) {
            /* Node lost root status - stop heartbeat, state updates, broadcast listener, and API listener */
            mesh_udp_bridge_stop_heartbeat();
            mesh_udp_bridge_stop_state_updates();
            mesh_udp_bridge_broadcast_listener_stop();
            mesh_udp_bridge_api_listener_stop();
            /* Update status LED to OFF */
#ifdef ROOT_STATUS_LED_GPIO
            root_status_led_set_root(false);
#endif
        } else if (!was_root_before && is_root_now) {
            /* Node became root - register with external server if discovered */
            if (mesh_udp_bridge_is_server_discovered()) {
                /* Create task for non-blocking registration */
                BaseType_t task_err = xTaskCreate(registration_task, "reg_role_chg", 4096, NULL, 1, NULL);
                if (task_err != pdPASS) {
                    ESP_LOGW(MESH_TAG, "[REGISTRATION] Failed to create registration task on role change");
                }
            }
            /* Node became root - start heartbeat and state updates if registered */
            if (mesh_udp_bridge_is_registered()) {
                mesh_udp_bridge_start_heartbeat();
                mesh_udp_bridge_start_state_updates();
            }
            /* Broadcast listener will be started in mesh_root_ip_callback when IP is obtained */
#ifdef ROOT_STATUS_LED_GPIO
            /* Update status LED pattern (node became root) */
            root_status_led_update_status();
#endif
        } else {
#ifdef ROOT_STATUS_LED_GPIO
            /* Update status LED based on current role */
            root_status_led_update();
#endif
        }

        /* Update previous root status */
        was_root_before = is_root_now;
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

        /* Handle heartbeat, state updates, broadcast listener, and API listener on root switch */
        if (!is_root_now) {
            /* Node is no longer root - stop heartbeat, state updates, broadcast listener, and API listener */
            mesh_udp_bridge_stop_heartbeat();
            mesh_udp_bridge_stop_state_updates();
            mesh_udp_bridge_broadcast_listener_stop();
            mesh_udp_bridge_api_listener_stop();
#ifdef ROOT_STATUS_LED_GPIO
            /* Update status LED to OFF */
            root_status_led_set_root(false);
#endif
        } else if (is_root_now) {
            /* Node is root - register with external server if discovered */
            if (mesh_udp_bridge_is_server_discovered()) {
                /* Create task for non-blocking registration */
                BaseType_t task_err = xTaskCreate(registration_task, "reg_switch", 4096, NULL, 1, NULL);
                if (task_err != pdPASS) {
                    ESP_LOGW(MESH_TAG, "[REGISTRATION] Failed to create registration task on root switch");
                }
            }
            /* Node is root and registered - start heartbeat and state updates */
            if (mesh_udp_bridge_is_registered()) {
                mesh_udp_bridge_start_heartbeat();
                mesh_udp_bridge_start_state_updates();
            }
#ifdef ROOT_STATUS_LED_GPIO
            /* Update status LED pattern (node is root) */
            root_status_led_update_status();
#endif
        } else {
#ifdef ROOT_STATUS_LED_GPIO
            /* Update status LED based on current role */
            root_status_led_update();
#endif
        }
        /* Broadcast listener will be started in mesh_root_ip_callback when IP is obtained */

        /* Update previous root status */
        was_root_before = is_root_now;
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

        /* RGB LED control removed - LEDs are now exclusive to plugins
         * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
         * Scan failure toggle timer no longer needed (was only for RGB LED indication)
         */
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

        if (is_root) {
            /* Root node: router connected */
            /* Root node: router connected */
            is_router_connected = true;

#ifdef ROOT_STATUS_LED_GPIO
            /* Update status LED pattern (router connected) */
            root_status_led_update_status();
#endif

            if (root_ip_callback) {
                root_ip_callback(arg, event_base, event_id, event_data);
            }
        }
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_LOST_IP>");

        if (is_root) {
            /* Root node: router disconnected */
            /* Root node: router disconnected */
            is_router_connected = false;

#ifdef ROOT_STATUS_LED_GPIO
            /* Update status LED pattern (router disconnected) */
            root_status_led_update_status();
#endif
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
 *                Core Local Heartbeat Counter
 *******************************************************/

/**
 * @brief Timer callback to increment local heartbeat counter
 *
 * This callback increments the local heartbeat counter every
 * MESH_CONFIG_HEARTBEAT_INTERVAL milliseconds. The counter runs
 * continuously on all nodes (root and child), even when root is lost.
 */
static void local_heartbeat_timer_callback(void *arg)
{
    (void)arg;
    local_heartbeat_counter++;
    /* Counter wraps at 255 automatically (uint8_t) */
}

/**
 * @brief Initialize core local heartbeat counter timer
 *
 * Creates and starts a periodic timer that increments the counter
 * every MESH_CONFIG_HEARTBEAT_INTERVAL milliseconds. This timer
 * runs on all nodes (root and child) and continues even when
 * root is lost.
 *
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t mesh_common_init_local_heartbeat(void)
{
    if (local_heartbeat_timer != NULL) {
        ESP_LOGD(MESH_TAG, "Local heartbeat timer already created");
        return ESP_OK;
    }

    esp_timer_create_args_t args = {
        .callback = &local_heartbeat_timer_callback,
        .arg = NULL,
        .name = "local_heartbeat_timer",
    };

    esp_err_t err = esp_timer_create(&args, &local_heartbeat_timer);
    if (err != ESP_OK) {
        ESP_LOGE(MESH_TAG, "Failed to create local heartbeat timer: %s", esp_err_to_name(err));
        local_heartbeat_timer = NULL;
        return err;
    }

    err = esp_timer_start_periodic(local_heartbeat_timer, (uint64_t)MESH_CONFIG_HEARTBEAT_INTERVAL * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(MESH_TAG, "Failed to start local heartbeat timer: %s", esp_err_to_name(err));
        esp_timer_delete(local_heartbeat_timer);
        local_heartbeat_timer = NULL;
        return err;
    }

    ESP_LOGI(MESH_TAG, "Local heartbeat timer started with interval %dms", MESH_CONFIG_HEARTBEAT_INTERVAL);
    return ESP_OK;
}

/**
 * @brief Get current local heartbeat counter value
 *
 * @return Current counter value (0-255, wraps)
 */
uint8_t mesh_common_get_local_heartbeat_counter(void)
{
    return local_heartbeat_counter;
}

/**
 * @brief Set local heartbeat counter value
 *
 * Used for root state adoption when a new root adopts the mesh state.
 * This synchronizes the local counter with the mesh state.
 *
 * @param counter Counter value to set (0-255)
 */
void mesh_common_set_local_heartbeat_counter(uint8_t counter)
{
    local_heartbeat_counter = counter;
    ESP_LOGD(MESH_TAG, "Local heartbeat counter set to %u", counter);
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
#ifdef ROOT_STATUS_LED_GPIO
    /* Initialize root status LED */
    esp_err_t status_led_err = root_status_led_init();
    if (status_led_err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "Failed to initialize root status LED: %s", esp_err_to_name(status_led_err));
    }
#endif
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
    /* Configure mesh settings - always use automatic root election */
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = MESH_CONFIG_MESH_CHANNEL;

    /* Configure router for all nodes (required by esp_mesh_set_config) */
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

    /* Always use automatic root election */
    ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, true));
    esp_err_t err = esp_mesh_fix_root(false);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "Failed to disable root fixing: %s", esp_err_to_name(err));
        /* Continue anyway - non-critical */
    }

    // disable wifi power saving...
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* mesh start */
    ESP_LOGI(MESH_TAG, "Starting mesh network...");

    ESP_ERROR_CHECK(esp_mesh_start());
#ifdef CONFIG_MESH_ENABLE_PS
    /* set the device active duty cycle. (default:10, MESH_PS_DEVICE_DUTY_REQUEST) */
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    /* set the network active duty cycle. (default:10, -1, MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
    ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION, CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif

    init_rgb_led();

    /* Initialize core local heartbeat counter timer (runs on all nodes) */
    esp_err_t heartbeat_init_err = mesh_common_init_local_heartbeat();
    if (heartbeat_init_err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "Failed to initialize local heartbeat timer: %s", esp_err_to_name(heartbeat_init_err));
        /* Continue anyway - non-critical */
    }

    bool is_root = esp_mesh_is_root();
    bool is_root_fixed = esp_mesh_is_root_fixed();

    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%" PRId32 ", %s<%d>%s, ps:%d",  esp_get_minimum_free_heap_size(),
             is_root_fixed ? "root fixed" : "root not fixed",
             esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());

    ESP_LOGI(MESH_TAG, "[STARTUP] Mesh started - Node Type: %s | Heap: %" PRId32 " bytes",
             is_root ? "ROOT NODE" : "NON-ROOT NODE", esp_get_minimum_free_heap_size());
    ESP_LOGI(MESH_TAG, "========================================");

    return ESP_OK;
}

/*******************************************************
 *                Mesh Send Bridge Wrapper
 *******************************************************/

/**
 * @brief Wrapper function to send mesh data and optionally forward to UDP bridge.
 *
 * This function wraps esp_mesh_send() and adds optional forwarding of mesh commands
 * to the external web server via UDP. The forwarding is completely optional and non-blocking.
 * If the external server is not registered or forwarding fails, mesh operations continue normally.
 *
 * Execution order:
 * 1. Call original esp_mesh_send() first (mesh operation)
 * 2. Check if external server registered
 * 3. If registered, forward command via UDP (non-blocking)
 * 4. Return mesh send result (ignore UDP forward result)
 *
 * @param to Destination mesh address (NULL for broadcast)
 * @param data Mesh data to send
 * @param flag Mesh data flag (e.g., MESH_DATA_P2P)
 * @param opt Optional mesh options (can be NULL)
 * @param opt_count Number of optional mesh options
 * @return Result from esp_mesh_send() call
 */
esp_err_t mesh_send_with_bridge(const mesh_addr_t *to, const mesh_data_t *data,
                                 int flag, const mesh_opt_t opt[], int opt_count)
{
    /* Call original esp_mesh_send() first (mesh operation) */
    esp_err_t mesh_result = esp_mesh_send(to, data, flag, opt, opt_count);

    /* Only forward if this is the root node (plan requirement: root node forwards commands) */
    if (!esp_mesh_is_root()) {
        /* Not root node - don't forward, but return mesh result */
        return mesh_result;
    }

    /* Check if we should forward to external server */
    if (data == NULL || data->data == NULL) {
        /* Invalid data - don't forward, but return mesh result */
        ESP_LOGW(MESH_TAG, "mesh_send_with_bridge: NULL data pointer, skipping forward");
        return mesh_result;
    }
    if (data->size == 0) {
        /* Empty data - don't forward, but return mesh result */
        ESP_LOGW(MESH_TAG, "mesh_send_with_bridge: empty data (size=0), skipping forward");
        return mesh_result;
    }

    /* Extract command ID from mesh data (first byte) */
    uint8_t mesh_cmd = data->data[0];

    /* Extract payload from mesh data (data after command ID) */
    const void *mesh_payload = NULL;
    size_t mesh_payload_len = 0;
    if (data->size > 1) {
        mesh_payload = &data->data[1];
        mesh_payload_len = data->size - 1;
    }

    /* Forward command via UDP (non-blocking, fire-and-forget) */
    mesh_udp_bridge_forward_mesh_command_async(mesh_cmd, mesh_payload, mesh_payload_len);

    /* Return mesh send result (ignore UDP forward result) */
    return mesh_result;
}
