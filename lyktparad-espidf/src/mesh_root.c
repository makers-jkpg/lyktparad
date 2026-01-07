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
#include "plugin_system.h"
#include "light_common_cathode.h"
#include "plugins/sequence/sequence_plugin.h"
#include "plugins/rgb_effect/rgb_effect_plugin.h"
#include "config/mesh_config.h"
#include "config/mesh_device_config.h"
#include "mesh_ota.h"
#include "mesh_udp_bridge.h"
#ifdef ROOT_STATUS_LED_GPIO
#include "root_status_led.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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
static bool root_setup_in_progress = false;
static bool root_has_been_root_before = false;  /* Track if this node has been root before (not a newly booted root) */

/* State response collection structure */
typedef struct {
    uint8_t counter;
    char plugin_name[32];  /* Max plugin name length */
    bool valid;
} mesh_state_response_t;

static mesh_state_response_t state_responses[10];  /* Max 10 responses */
static int state_response_count = 0;
static bool state_query_sent = false;
static SemaphoreHandle_t state_response_mutex = NULL;  /* Mutex for thread-safe response collection */

/*******************************************************
 *                Forward Declarations
 *******************************************************/

static esp_err_t mesh_root_adopt_mesh_state(void);

/*******************************************************
 *                Heartbeat Timer Management
 *******************************************************/

static void heartbeat_timer_start(void)
{
    if (heartbeat_timer != NULL) {
        /* If this is a newly booted root and setup hasn't been done, perform state adoption first */
        if (!root_has_been_root_before && !root_setup_in_progress) {
            root_setup_in_progress = true;
            root_has_been_root_before = true;
            esp_err_t adopt_err = mesh_root_adopt_mesh_state();
            if (adopt_err != ESP_OK) {
                ESP_LOGW(MESH_TAG, "[HEARTBEAT] State adoption failed: %s, continuing anyway", esp_err_to_name(adopt_err));
                root_setup_in_progress = false;  /* Allow heartbeats even if adoption failed */
            }
        }

        /* Only start timer if setup is complete */
        if (!root_setup_in_progress) {
            esp_err_t err = esp_timer_start_periodic(heartbeat_timer, (uint64_t)MESH_CONFIG_HEARTBEAT_INTERVAL * 1000ULL);
            if (err == ESP_OK) {
                ESP_LOGI(MESH_TAG, "[HEARTBEAT] Timer started with interval %dms", MESH_CONFIG_HEARTBEAT_INTERVAL);
            } else {
                ESP_LOGE(MESH_TAG, "[HEARTBEAT] Failed to start timer: 0x%x", err);
            }
        } else {
            ESP_LOGI(MESH_TAG, "[HEARTBEAT] Timer start deferred - setup in progress");
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
 *                Root State Adoption
 *******************************************************/

/**
 * @brief Calculate median value from array of uint8_t counters
 *
 * Handles the circular nature of uint8_t (0-255 wraps).
 * For odd count: returns middle value
 * For even count: returns average of two middle values
 *
 * @param counters Array of counter values
 * @param count Number of values in array
 * @return Median counter value
 */
static uint8_t calculate_median_counter(uint8_t *counters, int count)
{
    if (count == 0) {
        return 0;
    }
    if (count == 1) {
        return counters[0];
    }

    /* Sort counters (simple bubble sort for small arrays) */
    uint8_t sorted[10];  /* Max 10 responses */
    if (count > 10) {
        count = 10;  /* Limit to 10 */
    }
    for (int i = 0; i < count; i++) {
        sorted[i] = counters[i];
    }

    /* Bubble sort */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (sorted[j] > sorted[j + 1]) {
                uint8_t temp = sorted[j];
                sorted[j] = sorted[j + 1];
                sorted[j + 1] = temp;
            }
        }
    }

    /* Calculate median */
    if (count % 2 == 1) {
        /* Odd count: return middle value */
        return sorted[count / 2];
    } else {
        /* Even count: return average of two middle values */
        uint16_t sum = (uint16_t)sorted[count / 2 - 1] + (uint16_t)sorted[count / 2];
        return (uint8_t)(sum / 2);
    }
}

/**
 * @brief Determine most common plugin name from responses
 *
 * @param plugin_names Array of plugin name strings (can be NULL for inactive)
 * @param count Number of responses
 * @return Most common plugin name, or NULL if no clear majority
 */
static const char* determine_active_plugin(const char **plugin_names, int count)
{
    if (count == 0) {
        return NULL;
    }

    /* Count occurrences of each plugin name */
    int max_count = 0;
    const char *most_common = NULL;
    int null_count = 0;

    for (int i = 0; i < count; i++) {
        if (plugin_names[i] == NULL || plugin_names[i][0] == '\0') {
            null_count++;
            continue;
        }

        int plugin_count = 1;
        for (int j = i + 1; j < count; j++) {
            if (plugin_names[j] != NULL && plugin_names[j][0] != '\0' &&
                strcmp(plugin_names[i], plugin_names[j]) == 0) {
                plugin_count++;
            }
        }

        if (plugin_count > max_count) {
            max_count = plugin_count;
            most_common = plugin_names[i];
        }
    }

    /* If most common plugin has more votes than NULL/inactive, return it */
    if (most_common != NULL && max_count > null_count) {
        return most_common;
    }

    return NULL;  /* No clear majority */
}

/**
 * @brief Check if command should be blocked during setup
 *
 * @param cmd Command ID to check
 * @return true if command should be blocked, false otherwise
 */
static bool is_command_blocked_during_setup(uint8_t cmd)
{
    if (root_setup_in_progress && cmd != MESH_CMD_QUERY_MESH_STATE) {
        return true;
    }
    return false;
}

/**
 * @brief Perform root state adoption from mesh
 *
 * Queries all child nodes for their mesh state (plugin and heartbeat counter),
 * collects responses, calculates median counter and most common plugin,
 * and adopts the state before starting heartbeats.
 *
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t mesh_root_adopt_mesh_state(void)
{
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(MESH_TAG, "[ROOT SETUP] Starting mesh state adoption...");

    /* Get routing table */
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate child node count (excluding root) */
    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

    if (child_node_count == 0) {
        ESP_LOGI(MESH_TAG, "[ROOT SETUP] No child nodes, starting with default state (counter=0, no plugin)");
        root_setup_in_progress = false;
        return ESP_OK;
    }

    /* Prepare state query command */
    uint8_t *tx_buf = mesh_common_get_tx_buf();
    tx_buf[0] = MESH_CMD_QUERY_MESH_STATE;

    mesh_data_t query_data;
    query_data.data = tx_buf;
    query_data.size = 1;  /* Command byte only */
    query_data.proto = MESH_PROTO_BIN;
    query_data.tos = MESH_TOS_P2P;

    /* Send query to all child nodes */
    for (int i = 0; i < route_table_size; i++) {
        /* Skip root node */
        mesh_addr_t root_addr;
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            memcpy(root_addr.addr, mac, 6);
            bool is_root = true;
            for (int j = 0; j < 6; j++) {
                if (route_table[i].addr[j] != root_addr.addr[j]) {
                    is_root = false;
                    break;
                }
            }
            if (is_root) {
                continue;
            }
        }

        esp_err_t err = mesh_send_with_bridge(&route_table[i], &query_data, MESH_DATA_P2P, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW(MESH_TAG, "[ROOT SETUP] Failed to send state query to "MACSTR": %s",
                     MAC2STR(route_table[i].addr), esp_err_to_name(err));
        }
    }

    ESP_LOGI(MESH_TAG, "[ROOT SETUP] State query sent to %d child nodes, waiting for responses...", child_node_count);

    /* Create mutex if not exists */
    if (state_response_mutex == NULL) {
        state_response_mutex = xSemaphoreCreateMutex();
        if (state_response_mutex == NULL) {
            ESP_LOGE(MESH_TAG, "[ROOT SETUP] Failed to create response mutex");
            root_setup_in_progress = false;
            return ESP_ERR_NO_MEM;
        }
    }

    /* Reset response collection (with mutex protection) */
    if (xSemaphoreTake(state_response_mutex, portMAX_DELAY) == pdTRUE) {
        state_response_count = 0;
        state_query_sent = true;
        memset(state_responses, 0, sizeof(state_responses));
        xSemaphoreGive(state_response_mutex);
    }

    /* Wait for responses (max 5 seconds, max 10 responses) */
    int64_t start_time = esp_timer_get_time() / 1000;  /* Convert to milliseconds */
    const int timeout_ms = 5000;  /* 5 seconds */
    const int max_responses = 10;

    /* Wait for responses with timeout */
    while ((esp_timer_get_time() / 1000 - start_time) < timeout_ms && state_response_count < max_responses) {
        vTaskDelay(pdMS_TO_TICKS(100));  /* Check every 100ms */
    }

    /* Stop accepting responses */
    if (xSemaphoreTake(state_response_mutex, portMAX_DELAY) == pdTRUE) {
        state_query_sent = false;
        xSemaphoreGive(state_response_mutex);
    }

    /* Extract counters and plugin names from responses (with mutex protection) */
    uint8_t counters[10];
    const char *plugin_names[10];
    int response_count = 0;
    int current_response_count = 0;

    if (xSemaphoreTake(state_response_mutex, portMAX_DELAY) == pdTRUE) {
        current_response_count = state_response_count;
        for (int i = 0; i < current_response_count && i < max_responses; i++) {
            if (state_responses[i].valid) {
                counters[response_count] = state_responses[i].counter;
                plugin_names[response_count] = state_responses[i].plugin_name[0] != '\0' ? state_responses[i].plugin_name : NULL;
                response_count++;
            }
        }
        xSemaphoreGive(state_response_mutex);
    }

    /* If no responses collected, use default state */
    if (response_count == 0) {
        ESP_LOGW(MESH_TAG, "[ROOT SETUP] No responses received, using default state");
        root_setup_in_progress = false;
        return ESP_OK;
    }

    /* Calculate median counter */
    uint8_t median_counter = calculate_median_counter(counters, response_count);
    ESP_LOGI(MESH_TAG, "[ROOT SETUP] Collected %d responses, median counter: %u", response_count, median_counter);

    /* Determine active plugin */
    const char *adopted_plugin = determine_active_plugin(plugin_names, response_count);
    if (adopted_plugin != NULL) {
        ESP_LOGI(MESH_TAG, "[ROOT SETUP] Adopted plugin: '%s'", adopted_plugin);
    } else {
        ESP_LOGI(MESH_TAG, "[ROOT SETUP] No plugin adopted (no clear majority)");
    }

    /* Set initial state */
    heartbeat_count = median_counter;
    mesh_common_set_local_heartbeat_counter(median_counter);

    /* Activate adopted plugin if one was determined */
    if (adopted_plugin != NULL) {
        esp_err_t plugin_err = plugin_activate(adopted_plugin);
        if (plugin_err != ESP_OK) {
            ESP_LOGW(MESH_TAG, "[ROOT SETUP] Failed to activate plugin '%s': %s",
                     adopted_plugin, esp_err_to_name(plugin_err));
        } else {
            ESP_LOGI(MESH_TAG, "[ROOT SETUP] Plugin '%s' activated", adopted_plugin);
        }
    }

    /* Send plugin START command to activate nodes that joined during setup */
    /* Note: Do this before marking setup complete, but after state is set */
    /* This ensures nodes that joined during setup receive the command */
    /* Refresh routing table to include nodes that joined during setup */
    if (adopted_plugin != NULL) {
        /* Refresh routing table to get current state (nodes might have joined during wait) */
        esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
        int current_child_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

        /* Get plugin ID */
        uint8_t plugin_id = 0;
        esp_err_t id_err = plugin_get_id_by_name(adopted_plugin, &plugin_id);
        if (id_err == ESP_OK && plugin_id != 0) {
            /* Broadcast PLUGIN_CMD_START */
            uint8_t *cmd_buf = mesh_common_get_tx_buf();
            cmd_buf[0] = plugin_id;
            cmd_buf[1] = PLUGIN_CMD_START;

            mesh_data_t start_data;
            start_data.data = cmd_buf;
            start_data.size = 2;  /* Plugin ID + Command */
            start_data.proto = MESH_PROTO_BIN;
            start_data.tos = MESH_TOS_P2P;

            for (int i = 0; i < route_table_size; i++) {
                /* Skip root node */
                mesh_addr_t root_addr;
                uint8_t mac[6];
                if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
                    memcpy(root_addr.addr, mac, 6);
                    bool is_root = true;
                    for (int j = 0; j < 6; j++) {
                        if (route_table[i].addr[j] != root_addr.addr[j]) {
                            is_root = false;
                            break;
                        }
                    }
                    if (is_root) {
                        continue;
                    }
                }

                esp_err_t err = mesh_send_with_bridge(&route_table[i], &start_data, MESH_DATA_P2P, NULL, 0);
                if (err != ESP_OK) {
                    ESP_LOGW(MESH_TAG, "[ROOT SETUP] Failed to send plugin START to "MACSTR": %s",
                             MAC2STR(route_table[i].addr), esp_err_to_name(err));
                }
            }
            ESP_LOGI(MESH_TAG, "[ROOT SETUP] Plugin START command sent for '%s' to %d child nodes", adopted_plugin, current_child_count);
        }
    }

    /* Mark setup as complete (after sending plugin START) */
    root_setup_in_progress = false;

    ESP_LOGI(MESH_TAG, "[ROOT SETUP] Mesh state adoption complete - counter: %u, plugin: %s",
             median_counter, adopted_plugin != NULL ? adopted_plugin : "none");

    return ESP_OK;
}

/**
 * @brief Handle mesh state response from child node
 *
 * Called when a child node responds to MESH_CMD_QUERY_MESH_STATE.
 * Collects the response for processing during state adoption.
 *
 * @param plugin_name Active plugin name (can be NULL or empty if no plugin active)
 * @param counter Current heartbeat counter value
 */
void mesh_root_handle_state_response(const char *plugin_name, uint8_t counter)
{
    if (state_response_mutex == NULL) {
        return;  /* Mutex not initialized yet */
    }

    /* Check if we should accept responses (with mutex protection) */
    bool should_accept = false;
    int current_count = 0;
    if (xSemaphoreTake(state_response_mutex, portMAX_DELAY) == pdTRUE) {
        should_accept = (state_query_sent && state_response_count < 10);
        current_count = state_response_count;
        xSemaphoreGive(state_response_mutex);
    }

    if (!should_accept) {
        return;  /* Not waiting for responses or already have max responses */
    }

    /* Store response (with mutex protection) */
    if (xSemaphoreTake(state_response_mutex, portMAX_DELAY) == pdTRUE) {
        /* Double-check after acquiring mutex (state might have changed) */
        if (state_query_sent && state_response_count < 10) {
            state_responses[state_response_count].counter = counter;
            state_responses[state_response_count].valid = true;
            if (plugin_name != NULL && plugin_name[0] != '\0') {
                strncpy(state_responses[state_response_count].plugin_name, plugin_name, sizeof(state_responses[state_response_count].plugin_name) - 1);
                state_responses[state_response_count].plugin_name[sizeof(state_responses[state_response_count].plugin_name) - 1] = '\0';
            } else {
                state_responses[state_response_count].plugin_name[0] = '\0';
            }
            state_response_count++;
            current_count = state_response_count;
        }
        xSemaphoreGive(state_response_mutex);
    }

    ESP_LOGD(MESH_TAG, "[ROOT SETUP] State response received: plugin='%s', counter=%u (total: %d)",
             plugin_name != NULL ? plugin_name : "none", counter, current_count);
}

/**
 * @brief Check if root setup is in progress
 *
 * Used by other modules to check if commands should be blocked during setup.
 *
 * @return true if setup is in progress, false otherwise
 */
bool mesh_root_is_setup_in_progress(void)
{
    return root_setup_in_progress;
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

    /* Get sequence pointer if sequence plugin is active, otherwise use 0 */
    /* sequence_plugin_get_pointer_for_heartbeat() handles the inactive case internally */
    uint16_t seq_pointer = sequence_plugin_get_pointer_for_heartbeat();
    uint8_t pointer = (uint8_t)(seq_pointer & 0xFF);  /* Convert to 1-byte (0-255) */

    /* Check if commands are blocked during setup */
    if (is_command_blocked_during_setup(MESH_CMD_HEARTBEAT)) {
        ESP_LOGD(MESH_TAG, "[HEARTBEAT] Heartbeat blocked during setup");
        return;
    }

    /* payload: command prefix (0x01) + pointer (1 byte) + counter (1 byte) */
    /* Use core local heartbeat counter (core timer handles incrementing) */
    /* Note: Core timer increments counter every MESH_CONFIG_HEARTBEAT_INTERVAL */
    /* Root heartbeat timer fires at same interval, so counters should be synchronized */
    uint8_t counter = mesh_common_get_local_heartbeat_counter();
    heartbeat_count = counter;  /* Keep root-specific counter for compatibility */
    tx_buf[0] = MESH_CMD_HEARTBEAT;  /* Command prefix */
    tx_buf[1] = pointer;  /* Sequence pointer (0-255, 0 when sequence inactive) */
    tx_buf[2] = counter;  /* Counter (0-255, wraps) */

    data.data = tx_buf;
    data.size = 3;  /* New format: CMD(1) + pointer(1) + counter(1) */
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;

    esp_mesh_get_routing_table((mesh_addr_t *) &route_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

    /* Calculate actual child node count (excluding root node) */
    int child_node_count = (route_table_size > 0) ? (route_table_size - 1) : 0;

    /* Log routing table size changes for debugging */
    static int last_route_table_size = -1;
    if (route_table_size != last_route_table_size) {
        ESP_LOGI(mesh_common_get_tag(), "[ROUTING TABLE CHANGE] Size changed: %d -> %d", last_route_table_size, route_table_size);
        last_route_table_size = route_table_size;
    }

    for (i = 0; i < route_table_size; i++) {
        err = mesh_send_with_bridge(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
        if (err) {
            ESP_LOGD(MESH_TAG, "heartbeat broadcast err:0x%x to "MACSTR, err, MAC2STR(route_table[i].addr));
        }
    }
    ESP_LOGI(mesh_common_get_tag(), "[ROOT HEARTBEAT] sent - routing table size: %d (child nodes: %d)", route_table_size, child_node_count);

    /* Process heartbeat for rgb_effect plugin if active (root node processes its own heartbeat) */
    if (plugin_is_active("rgb_effect")) {
        esp_err_t heartbeat_err = rgb_effect_plugin_handle_heartbeat(pointer, counter);
        if (heartbeat_err != ESP_OK) {
            ESP_LOGW(mesh_common_get_tag(), "[HEARTBEAT] RGB effect plugin heartbeat handler error: 0x%x", heartbeat_err);
        }
    }

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

    /* Skip LED control if plugin is active (plugin controls LED exclusively)
     * This check ensures that if LED control is ever added back to the heartbeat handler,
     * it will not override plugin control. Heartbeat counting and mesh command sending
     * continue normally regardless of plugin state.
     */
    const char *active_plugin = plugin_get_active();
    if (active_plugin != NULL) {
        ESP_LOGD(mesh_common_get_tag(), "[ROOT ACTION] Heartbeat #%u - skipping LED change (plugin '%s' active)", counter, active_plugin);
    }

    /* Unified LED behavior for all nodes (root and child):
     * - Even heartbeat: LED OFF
     * - Odd heartbeat: LED ON (BLUE default or custom RGB)
     * - Skip LED changes if plugin is active (plugin controls LED)
     */
    /* Heartbeat counting and mesh command sending continue, but RGB LED control is removed
     * RGB LEDs are now exclusive to plugins via plugin_light_set_rgb() and plugin_set_rgb_led()
     * Status indication uses root status LED (ROOT_STATUS_LED_GPIO) instead
     */
    ESP_LOGI(mesh_common_get_tag(), "[ROOT HEARTBEAT] sent - pointer:%u, counter:%u", pointer, counter);
}

/*******************************************************
 *                Root-Specific Functions
 *******************************************************/


esp_err_t mesh_send_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!esp_mesh_is_root()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if commands are blocked during setup */
    if (is_command_blocked_during_setup(MESH_CMD_SET_RGB)) {
        ESP_LOGW(MESH_TAG, "[RGB] Command blocked during setup");
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

    /* RGB LED control removed - LEDs are now exclusive to plugins
     * State is still stored for web UI queries via mesh_get_current_rgb()
     */

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

    /* Store RGB values for web interface queries */
    root_rgb_r = r;
    root_rgb_g = g;
    root_rgb_b = b;
    root_rgb_has_been_set = true;

    /* If plugin is active, don't override plugin control
     * This check ensures that if LED control is ever added back to this function,
     * it will not override plugin control. State storage for web UI continues regardless.
     */
    const char *active_plugin = plugin_get_active();
    if (active_plugin != NULL) {
        ESP_LOGD(mesh_common_get_tag(), "[ROOT ACTION] RGB command ignored - plugin '%s' active", active_plugin);
        return;
    }

    /* RGB LED control removed - LEDs are now exclusive to plugins
     * State is still stored for web UI queries via mesh_get_current_rgb()
     * RGB commands should be routed to plugin system instead
     * Note: Plugin pause logic removed - plugins control RGB LEDs exclusively
     */

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
__attribute__((unused)) static void discovery_task(void *pvParameters)
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

#ifndef ONLY_ONBOARD_HTTP
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
#else
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] ONLY_ONBOARD_HTTP enabled - external webserver functionality disabled");
#endif /* ONLY_ONBOARD_HTTP */
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
