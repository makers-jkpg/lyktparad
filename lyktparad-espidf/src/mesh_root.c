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
#include "node_effects.h"
#include "light_common_cathode.h"
#include "config/mesh_config.h"
#include "mesh_ota.h"
#include "mesh_udp_bridge.h"
#ifdef ROOT_STATUS_LED_GPIO
#include "root_status_led.h"
#endif
#include "node_sequence.h"
#include "mesh_udp_bridge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"

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

    /* Send strobe effect command to all child nodes - this call will be moved to wherever it belongs in the future */
//    mesh_send_strobe_effect();
    mesh_send_fade_effect();

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
        err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
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

    /* Unified LED behavior for all nodes (root and child):
     * - Even heartbeat: LED OFF
     * - Odd heartbeat: LED ON (BLUE default or custom RGB)
     * - Skip LED changes if sequence mode is active (sequence controls LED)
     */
    if (mode_sequence_root_is_active()) {
        ESP_LOGD(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu - skipping LED change (sequence active)", (unsigned long)cnt);
    } else if (!(cnt % 2)) {
        /* even heartbeat: turn off light */
        mesh_light_set_colour(0);
        set_rgb_led(0, 0, 0);
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu (even) - LED OFF", (unsigned long)cnt);
    } else {
        /* odd heartbeat: turn on light using last RGB color or default to MESH_LIGHT_BLUE */
        if (root_rgb_has_been_set) {
            /* Use the color from the latest MESH_CMD_SET_RGB command */
            mesh_light_set_rgb(root_rgb_r, root_rgb_g, root_rgb_b);
            set_rgb_led(root_rgb_r, root_rgb_g, root_rgb_b);
            ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu (odd) - LED RGB(%d,%d,%d)",
                     (unsigned long)cnt, root_rgb_r, root_rgb_g, root_rgb_b);
        } else {
            /* Default to MESH_LIGHT_BLUE if no RGB command has been received */
            mesh_light_set_colour(MESH_LIGHT_BLUE);
            set_rgb_led(0, 0, 155);  /* Match MESH_LIGHT_BLUE RGB values */
            ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%lu (odd) - LED BLUE (default)", (unsigned long)cnt);
        }
    }
}

/*******************************************************
 *                Root-Specific Functions
 *******************************************************/

esp_err_t mesh_send_fade_effect()
{
    /* only root should send the heartbeat */
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    int i;

    esp_err_t err;
    mesh_data_t data;
    uint8_t *tx_buf = mesh_common_get_tx_buf();

    struct effect_params_fade_t *fade_params = (struct effect_params_fade_t *)tx_buf;
    fade_params->base.command = MESH_CMD_EFFECT;
    fade_params->base.effect_id = EFFECT_FADE;
    fade_params->base.start_delay_ms = 0;  /* will be set per-node below */
    fade_params->r_on = 0;
    fade_params->g_on = 0;
    fade_params->b_on = 0;
    fade_params->r_off = 255;
    fade_params->g_off = 0;
    fade_params->b_off = 0;
    fade_params->fade_in_ms = 100;
    fade_params->fade_out_ms = 100;
    fade_params->duration_ms = 100;
    fade_params->repeat_count = 1;

    data.data = tx_buf;
    data.size = sizeof(struct effect_params_fade_t);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    for (i = 0; i < route_table_size; i++) {
        fade_params->base.start_delay_ms = i * 100;  /* stagger start delay by 50ms per node */
        err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err) {
            ESP_LOGD(MESH_TAG, "heartbeat broadcast err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }
    ESP_LOGI(mesh_common_get_tag(), "Fade effect sent");
    return ESP_OK;
}
esp_err_t mesh_send_strobe_effect()
{
    /* only root should send the heartbeat */
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    int i;

    esp_err_t err;
    mesh_data_t data;
    uint8_t *tx_buf = mesh_common_get_tx_buf();

    struct effect_params_strobe_t *strobe_params = (struct effect_params_strobe_t *)tx_buf;
    strobe_params->base.command = MESH_CMD_EFFECT;
    strobe_params->base.effect_id = EFFECT_STROBE;
    strobe_params->base.start_delay_ms = 0;  /* will be set per-node below */
    strobe_params->r_on = 255;
    strobe_params->g_on = 255;
    strobe_params->b_on = 255;
    strobe_params->r_off = 0;
    strobe_params->g_off = 0;
    strobe_params->b_off = 0;
    strobe_params->duration_on = 10;
    strobe_params->duration_off = 100;
    strobe_params->repeat_count = 1;

    data.data = tx_buf;
    data.size = sizeof(struct effect_params_strobe_t);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    for (i = 0; i < route_table_size; i++) {
        strobe_params->base.start_delay_ms = i * 100;  /* stagger start delay by 50ms per node */
        err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err) {
            ESP_LOGD(MESH_TAG, "heartbeat broadcast err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }
    ESP_LOGI(mesh_common_get_tag(), "Strobe effect sent");
    return ESP_OK;
}

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
        err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
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

/**
 * @brief Handle RGB command received via mesh network (for unified behavior)
 *
 * This function allows the root node to receive RGB commands via the mesh network,
 * enabling unified behavior where the root node can respond to color commands
 * just like child nodes.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void mesh_root_handle_rgb_command(uint8_t r, uint8_t g, uint8_t b)
{
    if (!esp_mesh_is_root()) {
        return;
    }

    /* Store RGB values for use in heartbeat handler */
    root_rgb_r = r;
    root_rgb_g = g;
    root_rgb_b = b;
    root_rgb_has_been_set = true;

    /* Update root node's LED immediately */
    esp_err_t err = mesh_light_set_rgb(r, g, b);
    if (err != ESP_OK) {
        ESP_LOGE(mesh_common_get_tag(), "[RGB] failed to set root node LED: 0x%x", err);
    }
    set_rgb_led(r, g, b);

    /* Stop sequence playback if active */
    mode_sequence_root_stop();

    ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] RGB command received via mesh: R:%d G:%d B:%d", r, g, b);
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

/**
 * @brief Discovery task function for non-blocking mDNS discovery.
 *
 * This task performs mDNS discovery after the web server has started.
 * mDNS is the primary discovery method and is required at build time.
 * UDP broadcast listener runs in background as a runtime fallback.
 * Discovery does not affect web server operation (embedded server always works).
 */
static void discovery_task(void *pvParameters)
{
    char server_ip[16] = {0};
    uint16_t server_port = 0;

    /* First, try to use cached IP if available (optimization) */
    if (mesh_udp_bridge_use_cached_ip()) {
        ESP_LOGI(mesh_common_get_tag(), "[DISCOVERY] Using cached server IP (skipping mDNS)");
        /* Register with external server after discovery (non-blocking - already in task) */
        if (mesh_udp_bridge_is_server_discovered() && esp_mesh_is_root()) {
            esp_err_t reg_err = mesh_udp_bridge_register();
            if (reg_err != ESP_OK && reg_err != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(mesh_common_get_tag(), "[REGISTRATION] Registration failed: %s", esp_err_to_name(reg_err));
            }
        }
        vTaskDelete(NULL);
        return;
    }

    /* Initialize mDNS if not already initialized */
    esp_err_t err = mesh_udp_bridge_mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(mesh_common_get_tag(), "[DISCOVERY] mDNS initialization failed, trying cached address");
        /* Try to use cached address */
        err = mesh_udp_bridge_get_cached_server(server_ip, &server_port);
        if (err == ESP_OK) {
            ESP_LOGI(mesh_common_get_tag(), "[DISCOVERY] Using cached server address: %s:%d", server_ip, server_port);
            /* Convert IP string to network byte order for registration */
            struct in_addr addr;
            if (inet_aton(server_ip, &addr) != 0) {
                uint8_t ip_bytes[4];
                memcpy(ip_bytes, &addr.s_addr, 4);
                mesh_udp_bridge_set_registration(true, ip_bytes, server_port);
                /* Register with external server after discovery (non-blocking - already in task) */
                if (esp_mesh_is_root()) {
                    esp_err_t reg_err = mesh_udp_bridge_register();
                    if (reg_err != ESP_OK && reg_err != ESP_ERR_NOT_FOUND) {
                        ESP_LOGW(mesh_common_get_tag(), "[REGISTRATION] Registration failed: %s", esp_err_to_name(reg_err));
                    }
                }
            }
        }
        vTaskDelete(NULL);
        return;
    }

    /* Perform discovery with 20 second timeout */
    err = mesh_udp_bridge_discover_server(20000, server_ip, &server_port);
    if (err == ESP_OK) {
        /* Discovery succeeded - cache the address */
        ESP_LOGI(mesh_common_get_tag(), "[DISCOVERY] External web server discovered: %s:%d", server_ip, server_port);
        mesh_udp_bridge_cache_server(server_ip, server_port);

        /* Stop retry task if it's running (discovery succeeded) */
        mesh_udp_bridge_stop_retry_task();

        /* Convert IP string to network byte order for registration */
        struct in_addr addr;
        if (inet_aton(server_ip, &addr) != 0) {
            uint8_t ip_bytes[4];
            memcpy(ip_bytes, &addr.s_addr, 4);
            mesh_udp_bridge_set_registration(true, ip_bytes, server_port);
            /* Stop UDP broadcast listener since mDNS discovery succeeded (optional optimization) */
            mesh_udp_bridge_broadcast_listener_stop();
            /* Register with external server after discovery (non-blocking - already in task) */
            if (esp_mesh_is_root()) {
                esp_err_t reg_err = mesh_udp_bridge_register();
                if (reg_err != ESP_OK && reg_err != ESP_ERR_NOT_FOUND) {
                    ESP_LOGW(mesh_common_get_tag(), "[REGISTRATION] Registration failed: %s", esp_err_to_name(reg_err));
                }
            }
        } else {
            ESP_LOGW(mesh_common_get_tag(), "[DISCOVERY] Failed to convert IP address: %s", server_ip);
        }

        /* Broadcast discovered IP to all child nodes (optimization) */
        mesh_udp_bridge_broadcast_server_ip(server_ip, server_port);
    } else {
        /* Discovery failed - try to use cached address */
        ESP_LOGI(mesh_common_get_tag(), "[DISCOVERY] Discovery failed, trying cached address");
        err = mesh_udp_bridge_get_cached_server(server_ip, &server_port);
        if (err == ESP_OK) {
            ESP_LOGI(mesh_common_get_tag(), "[DISCOVERY] Using cached server address: %s:%d", server_ip, server_port);
            /* Convert IP string to network byte order for registration */
            struct in_addr addr;
            if (inet_aton(server_ip, &addr) != 0) {
                uint8_t ip_bytes[4];
                memcpy(ip_bytes, &addr.s_addr, 4);
                mesh_udp_bridge_set_registration(true, ip_bytes, server_port);
                /* Register with external server after discovery (non-blocking - already in task) */
                if (esp_mesh_is_root()) {
                    esp_err_t reg_err = mesh_udp_bridge_register();
                    if (reg_err != ESP_OK && reg_err != ESP_ERR_NOT_FOUND) {
                        ESP_LOGW(mesh_common_get_tag(), "[REGISTRATION] Registration failed: %s", esp_err_to_name(reg_err));
                    }
                }
            }
        } else {
            ESP_LOGI(mesh_common_get_tag(), "[DISCOVERY] No cached address available, starting retry task");
            /* Start background retry task if no cached address available */
            mesh_udp_bridge_start_retry_task();
        }
    }

    vTaskDelete(NULL);
}

static void mesh_root_ip_callback(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Starting web server on port 80");
    esp_err_t err = mesh_web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(mesh_common_get_tag(), "Failed to start web server: 0x%x", err);
    } else {
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Web server started successfully");

        /* Start UDP broadcast listener (runtime fallback discovery mechanism) */
        /* mDNS discovery is the primary method and is tried first via discovery_task. */
        /* UDP broadcast listener runs in the background and is used as a runtime fallback */
        /* when mDNS discovery fails (server not found), not when mDNS component is unavailable. */
        mesh_udp_bridge_broadcast_listener_start();

        /* Start UDP API command listener (for external server API proxy) */
        mesh_udp_bridge_api_listener_start();

        /* Start discovery task AFTER web server has started (non-blocking) */
        /* mDNS discovery is the primary method and is required at build time */
        /* Discovery does not affect embedded web server operation (embedded server always works) */
        BaseType_t task_err = xTaskCreate(discovery_task, "discovery", 4096, NULL, 1, NULL);
        if (task_err != pdPASS) {
            ESP_LOGW(mesh_common_get_tag(), "[DISCOVERY] Failed to create discovery task");
        } else {
            ESP_LOGI(mesh_common_get_tag(), "[DISCOVERY] Discovery task started");
        }
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
            err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
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
