/* Mesh Root Node Module Implementation
 *
 * This module contains code specific to root nodes in the mesh network.
 * Root nodes are responsible for sending heartbeats, managing the routing table,
 * hosting the web server, and sending commands to child nodes.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include <string.h>
#include <inttypes.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "mesh_common.h"
#include "mesh_root.h"
#include "mesh_web.h"
#include "mesh_commands.h"
#include "light_neopixel.h"
#include "light_common_cathode.h"
#include "config/mesh_config.h"
#include "mesh_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef CONFIG_MESH_ROUTE_TABLE_SIZE
#define CONFIG_MESH_ROUTE_TABLE_SIZE 50
#endif

/* Forward declaration - light_on and light_off are in mesh_common.c */
extern mesh_light_ctl_t light_on;
extern mesh_light_ctl_t light_off;

/* Tag for logging - compile-time constant for MACSTR concatenation */
#define MESH_TAG "mesh_main"

/* Ensure MACSTR is defined - it should be in esp_mac.h */
#ifndef MACSTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

/* Root-specific static variables */
static esp_timer_handle_t heartbeat_timer = NULL;
static uint32_t heartbeat_count = 0;
static uint8_t root_rgb_r = 0;
static uint8_t root_rgb_g = 0;
static uint8_t root_rgb_b = 0;
static bool root_rgb_has_been_set = false;

/*******************************************************
 *                Heartbeat Timer Management
 *******************************************************/

static void heartbeat_timer_start(void)
{
    if (heartbeat_timer != NULL) {
        esp_err_t err = esp_timer_start_periodic(heartbeat_timer, 500000); /* 500ms */
        if (err == ESP_OK) {
            ESP_LOGI(MESH_TAG, "[HEARTBEAT] Timer started");
        } else {
            ESP_LOGE(MESH_TAG, "[HEARTBEAT] Failed to start timer: 0x%x", err);
        }
    }
}

static void heartbeat_timer_stop(void)
{
    if (heartbeat_timer != NULL) {
        esp_err_t err = esp_timer_stop(heartbeat_timer);
        if (err == ESP_OK) {
            ESP_LOGI(MESH_TAG, "[HEARTBEAT] Timer stopped");
        } else if (err != ESP_ERR_INVALID_STATE) { /* ESP_ERR_INVALID_STATE means already stopped */
            ESP_LOGE(MESH_TAG, "[HEARTBEAT] Failed to stop timer: 0x%x", err);
        }
    }
}

/*******************************************************
 *                Heartbeat Timer Callback
 *******************************************************/

static void heartbeat_timer_cb(void *arg)
{
    /* only root should send the heartbeat */
    if (!esp_mesh_is_root()) {
        return;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    int i;

    esp_err_t err;
    mesh_data_t data;
    uint8_t *tx_buf = mesh_common_get_tx_buf();

    /* payload: command prefix (0x01) + 4-byte big-endian counter */
    heartbeat_count++;
    uint32_t cnt = heartbeat_count;
    tx_buf[0] = MESH_CMD_HEARTBEAT;  /* Command prefix */
    tx_buf[1] = (cnt >> 24) & 0xff;  /* Counter MSB */
    tx_buf[2] = (cnt >> 16) & 0xff;
    tx_buf[3] = (cnt >> 8) & 0xff;
    tx_buf[4] = (cnt >> 0) & 0xff;    /* Counter LSB */

    data.data = tx_buf;
    data.size = 5;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate actual child node count (excluding root node) */
    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

    // #region agent log
    ESP_LOGI(MESH_TAG, "[DEBUG HYP-A] heartbeat_timer_cb entry - route_table_size:%d child_node_count:%d heartbeat_count:%lu",
             route_table_size, child_node_count, (unsigned long)cnt);
    // #endregion

    /* Log routing table size changes for debugging */
    static int last_route_table_size = -1;
    if (route_table_size != last_route_table_size) {
        ESP_LOGI(mesh_common_get_tag(), "[ROUTING TABLE CHANGE] Size changed: %d -> %d", last_route_table_size, route_table_size);
        last_route_table_size = route_table_size;
    }

    // #region agent log
    ESP_LOGI(MESH_TAG, "[DEBUG HYP-B] heartbeat_timer_cb before send loop - route_table_size:%d child_node_count:%d will_send:%s",
             route_table_size, child_node_count, (route_table_size > 0) ? "true" : "false");
    // #endregion

    for (i = 0; i < route_table_size; i++) {
        err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err) {
            ESP_LOGD(MESH_TAG, "heartbeat broadcast err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }
    ESP_LOGI(mesh_common_get_tag(), "[ROOT HEARTBEAT] sent - routing table size: %d (child nodes: %d)", route_table_size, child_node_count);

    // #region agent log
    /* Periodically log root state to detect if it changes (every 10th heartbeat = ~5 seconds) */
    static unsigned long heartbeat_log_counter = 0;
    heartbeat_log_counter++;
    if (heartbeat_log_counter % 10 == 0) {
        bool is_root_fixed = esp_mesh_is_root_fixed();
        bool self_org = esp_mesh_get_self_organized();
        uint8_t primary_channel = 0;
        wifi_second_chan_t second_channel = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_channel(&primary_channel, &second_channel);
        ESP_LOGI(mesh_common_get_tag(), "[DEBUG CHILD-CONN] Root state check - is_root_fixed:%d, self_org:%d, channel:%d, route_size:%d",
                 is_root_fixed ? 1 : 0, self_org ? 1 : 0, primary_channel, route_table_size);
    }
    // #endregion

    /* Root node LED behavior:
     * - Base color indicates router connection: ORANGE (not connected) or GREEN (connected)
     * - When router is connected AND mesh nodes are present: blink WHITE for 100ms on heartbeat
     * - Base color (GREEN) is maintained to always show router connection status
     */
    bool router_connected = mesh_common_is_router_connected();

    if (!router_connected) {
        /* Router not connected: maintain ORANGE base */
        mesh_light_set_colour(MESH_LIGHT_ORANGE);
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu - LED ORANGE (router not connected)", (unsigned long)cnt);
    } else if (child_node_count == 0) {
        /* Router connected, no mesh nodes: steady GREEN */
        mesh_light_set_colour(MESH_LIGHT_GREEN);
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu - LED GREEN (router connected, no mesh nodes)", (unsigned long)cnt);
    } else {
        /* Router connected with mesh nodes: GREEN base with WHITE blink on heartbeat */
        /* Ensure GREEN base is set first */
        mesh_light_set_colour(MESH_LIGHT_GREEN);
        /* Blink white for 100ms to indicate heartbeat activity */
        mesh_light_set_colour(MESH_LIGHT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(100)); /* 100ms delay */
        /* Return to GREEN base to maintain router connection indication */
        mesh_light_set_colour(MESH_LIGHT_GREEN);
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu - LED GREEN with WHITE blink (router connected, %d mesh nodes)", (unsigned long)cnt, child_node_count);
    }
}

/*******************************************************
 *                Root-Specific Functions
 *******************************************************/

esp_err_t mesh_send_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    int i;
    esp_err_t err;
    mesh_data_t data;
    uint8_t *tx_buf = mesh_common_get_tx_buf();

    /* Prepare RGB message: command prefix (0x03) + 3-byte RGB values */
    tx_buf[0] = MESH_CMD_SET_RGB;
    tx_buf[1] = r;
    tx_buf[2] = g;
    tx_buf[3] = b;

    data.data = tx_buf;
    data.size = 4;
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate actual child node count (excluding root node) */
    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

    /* Update root node RGB state for web interface (even if no other nodes) */
    root_rgb_r = r;
    root_rgb_g = g;
    root_rgb_b = b;
    root_rgb_has_been_set = true;

    /* Update root node's own LED */
    err = mesh_light_set_rgb(r, g, b);
    if (err != ESP_OK) {
        ESP_LOGE(mesh_common_get_tag(), "[RGB] failed to set root node LED: 0x%x", err);
    }
    set_rgb_led(r, g, b);

    if (child_node_count == 0) {
        ESP_LOGD(mesh_common_get_tag(), "[RGB SENT] R:%d G:%d B:%d - no child nodes", r, g, b);
        return ESP_OK;
    }
    for (i = 0; i < route_table_size; i++) {
        err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err) {
            ESP_LOGD(MESH_TAG, "RGB send err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }
    ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] RGB command sent: R:%d G:%d B:%d to %d child nodes", r, g, b, child_node_count);
    return ESP_OK;
}

uint32_t mesh_get_heartbeat_count(void)
{
    return heartbeat_count;
}

void mesh_get_current_rgb(uint8_t *r, uint8_t *g, uint8_t *b, bool *is_set)
{
    if (root_rgb_has_been_set) {
        *r = root_rgb_r;
        *g = root_rgb_g;
        *b = root_rgb_b;
        *is_set = true;
    } else {
        /* Return MESH_LIGHT_BLUE default (RGB: 0, 0, 155) */
        *r = 0;
        *g = 0;
        *b = 155;
        *is_set = false;
    }
}

int mesh_get_node_count(void)
{
    if (!esp_mesh_is_root()) {
        return 0;
    }
    int total_size = esp_mesh_get_routing_table_size();
    /* Subtract 1 to exclude root node from count - routing table size includes root node */
    return (total_size > 0) ? (total_size - 1) : 0;
}

/*******************************************************
 *                Root Event Handler Callback
 *******************************************************/

static void mesh_root_event_callback(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data)
{
    switch (event_id) {
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        int route_size_after_disconnect = esp_mesh_get_routing_table_size();
        int child_count_after_disconnect = (route_size_after_disconnect > 0) ? (route_size_after_disconnect - 1) : 0;
        ESP_LOGI(MESH_TAG, "[CHILD DISCONNECTED] Child "MACSTR" disconnected - Current routing table size: %d",
                 MAC2STR(child_disconnected->mac), route_size_after_disconnect);

        // #region agent log
        ESP_LOGI(MESH_TAG, "[DEBUG HYP-E] CHILD_DISCONNECTED event - route_size:%d child_count:%d",
                 route_size_after_disconnect, child_count_after_disconnect);
        // #endregion

        /* Stop heartbeat timer when last child disconnects (route_size == 1 means only root remains) */
        if (route_size_after_disconnect == 1) {
            heartbeat_timer_stop();
        }
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGI(mesh_common_get_tag(), "[ROUTING TABLE] Node added - Total nodes: %d", routing_table->rt_size_new);

        // #region agent log
        int child_count_after_add = (routing_table->rt_size_new > 0) ? (routing_table->rt_size_new - 1) : 0;
        ESP_LOGI(MESH_TAG, "[DEBUG HYP-D] ROUTING_TABLE_ADD event - rt_size_new:%d child_count:%d",
                 routing_table->rt_size_new, child_count_after_add);
        // #endregion

        /* Start heartbeat timer when first child connects (rt_size_new > 1 means root + at least one child) */
        if (routing_table->rt_size_new > 1) {
            heartbeat_timer_start();
        }
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGI(mesh_common_get_tag(), "[ROUTING TABLE] Node removed - Total nodes: %d", routing_table->rt_size_new);

        // #region agent log
        int child_count_after_remove = (routing_table->rt_size_new > 0) ? (routing_table->rt_size_new - 1) : 0;
        ESP_LOGI(MESH_TAG, "[DEBUG HYP-E] ROUTING_TABLE_REMOVE event - rt_size_new:%d child_count:%d",
                 routing_table->rt_size_new, child_count_after_remove);
        // #endregion

        /* Stop heartbeat timer when last child disconnects (rt_size_new == 1 means only root remains) */
        if (routing_table->rt_size_new == 1) {
            heartbeat_timer_stop();
        }
    }
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        esp_netif_t *netif_sta = mesh_common_get_netif_sta();

        // #region agent log
        ESP_LOGI(MESH_TAG, "[DEBUG HYP-1] MESH_EVENT_PARENT_CONNECTED - netif_sta:%p, is_root:%d",
                 netif_sta, esp_mesh_is_root() ? 1 : 0);
        // #endregion

        if (netif_sta != NULL) {
            // #region agent log
            esp_netif_ip_info_t ip_info_before;
            esp_err_t get_ip_before = esp_netif_get_ip_info(netif_sta, &ip_info_before);
            ESP_LOGI(MESH_TAG, "[DEBUG HYP-1] Before DHCP restart - get_ip_info result:0x%x, ip:" IPSTR,
                     get_ip_before, IP2STR(&ip_info_before.ip));
            // #endregion

            esp_err_t dhcp_stop_err = esp_netif_dhcpc_stop(netif_sta);

            // #region agent log
            ESP_LOGI(MESH_TAG, "[DEBUG HYP-1] DHCP stop result:0x%x", dhcp_stop_err);
            // #endregion

            esp_err_t dhcp_start_err = esp_netif_dhcpc_start(netif_sta);

            // #region agent log
            ESP_LOGI(MESH_TAG, "[DEBUG HYP-1] DHCP start result:0x%x", dhcp_start_err);
            // #endregion
        } else {
            // #region agent log
            ESP_LOGE(MESH_TAG, "[DEBUG HYP-1] ERROR: netif_sta is NULL!");
            // #endregion
        }
    }
    break;
    case MESH_EVENT_LAYER_CHANGE: {
        bool is_root = esp_mesh_is_root();
        if (is_root) {
            /* Check if we have IP address before starting web server */
            esp_netif_t *netif_sta = mesh_common_get_netif_sta();
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif_sta, &ip_info) == ESP_OK &&
                ip_info.ip.addr != 0) {
                mesh_web_server_start();
            }
        } else {
            mesh_web_server_stop();
        }
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        bool is_root = esp_mesh_is_root();
        /* Stop web server if no longer root */
        if (!is_root) {
            mesh_web_server_stop();
        }
    }
    break;
    default:
        break;
    }
}

/*******************************************************
 *                Root IP Event Handler Callback
 *******************************************************/

static void mesh_root_ip_callback(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Starting web server on port 80");
    esp_err_t err = mesh_web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(mesh_common_get_tag(), "Failed to start web server: 0x%x", err);
    } else {
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Web server started successfully");
    }
}

/*******************************************************
 *                Root P2P TX Task (currently disabled)
 *******************************************************/

void esp_mesh_p2p_tx_main(void *arg)
{
    int i;
    esp_err_t err;
    int send_count = 0;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    mesh_data_t data;
    uint8_t *tx_buf = mesh_common_get_tx_buf();
    data.data = tx_buf;
    data.size = TX_SIZE;  /* Defined in mesh_common.h */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    mesh_common_set_running(true);

    while (mesh_common_is_running()) {
        /* non-root do nothing but print */
        if (!esp_mesh_is_root()) {
            ESP_LOGI(mesh_common_get_tag(), "layer:%d, rtableSize:%d, %s", mesh_common_get_layer(),
                     esp_mesh_get_routing_table_size(),
                     mesh_common_is_connected() ? "NODE" : "DISCONNECT");
            vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
            continue;
        }
        esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
        if (send_count && !(send_count % 100)) {
            ESP_LOGI(mesh_common_get_tag(), "size:%d/%d,send_count:%d", route_table_size,
                     esp_mesh_get_routing_table_size(), send_count);
        }
        send_count++;
        tx_buf[25] = (send_count >> 24) & 0xff;
        tx_buf[24] = (send_count >> 16) & 0xff;
        tx_buf[23] = (send_count >> 8) & 0xff;
        tx_buf[22] = (send_count >> 0) & 0xff;
        if (send_count % 2) {
            memcpy(tx_buf, (uint8_t *)&light_on, sizeof(light_on));
        } else {
            memcpy(tx_buf, (uint8_t *)&light_off, sizeof(light_off));
        }

        for (i = 0; i < route_table_size; i++) {
            err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
            if (err) {
                mesh_addr_t parent_addr;
                mesh_common_get_parent_addr(&parent_addr);
                ESP_LOGE(MESH_TAG, "[ROOT-2-UNICAST:%d][L:%d]parent:"MACSTR" to "MACSTR", heap:%" PRId32 "[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_common_get_layer(), MAC2STR(parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                         err, data.proto, data.tos);
            } else if (!(send_count % 100)) {
                mesh_addr_t parent_addr;
                mesh_common_get_parent_addr(&parent_addr);
                ESP_LOGW(MESH_TAG, "[ROOT-2-UNICAST:%d][L:%d][rtableSize:%d]parent:"MACSTR" to "MACSTR", heap:%" PRId32 "[err:0x%x, proto:%d, tos:%d]",
                         send_count, mesh_common_get_layer(),
                         esp_mesh_get_routing_table_size(),
                         MAC2STR(parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                         err, data.proto, data.tos);
            }
        }
        /* if route_table_size is less than 10, add delay to avoid watchdog in this task. */
        if (route_table_size < 10) {
            vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}

/*******************************************************
 *                Root Initialization
 *******************************************************/

esp_err_t mesh_root_init(void)
{
    /* Register root event handler callback */
    mesh_common_register_root_event_callback(mesh_root_event_callback);
    mesh_common_register_root_ip_callback(mesh_root_ip_callback);

    /* Create heartbeat timer (but don't start it yet)
     * Timer will be started when first child node connects */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &heartbeat_timer_cb,
        .arg = NULL,
        .name = "heartbeat"
    };
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &heartbeat_timer));

    // #region agent log
    int initial_route_size = esp_mesh_get_routing_table_size();
    int initial_child_count = (initial_route_size > 0) ? (initial_route_size - 1) : 0;
    ESP_LOGI(MESH_TAG, "[DEBUG HYP-C] mesh_root_init timer created - initial_route_size:%d initial_child_count:%d timer_started:false",
             initial_route_size, initial_child_count);
    // #endregion

    /* Only start timer if there are already child nodes connected */
    if (initial_child_count > 0) {
        heartbeat_timer_start();
    }

    /* Initialize OTA system (safe to call on all nodes, but only root will use it) */
    esp_err_t ota_err = mesh_ota_init();
    if (ota_err != ESP_OK) {
        ESP_LOGW(MESH_TAG, "[STARTUP] OTA initialization failed: %s", esp_err_to_name(ota_err));
    } else {
        ESP_LOGI(MESH_TAG, "[STARTUP] OTA system initialized");
    }

    return ESP_OK;
}
