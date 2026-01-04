# External Web Server Discovery - Development Guide

**Last Updated:** 2025-01-15

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Discovery Methods](#discovery-methods)
4. [mDNS Discovery - Server Side](#mdns-discovery---server-side)
5. [mDNS Discovery - ESP32 Side](#mdns-discovery---esp32-side)
6. [UDP Broadcast Fallback](#udp-broadcast-fallback)
7. [Discovery Priority and Integration](#discovery-priority-and-integration)
8. [Caching and Persistence](#caching-and-persistence)
9. [Error Handling and Graceful Degradation](#error-handling-and-graceful-degradation)
10. [Implementation Details](#implementation-details)
11. [API Reference](#api-reference)
12. [Integration Points](#integration-points)

## Overview

### Purpose

The External Web Server Discovery system enables automatic zero-configuration discovery of optional external web servers by ESP32 mesh root nodes. The system implements two discovery methods: **mDNS (multicast DNS)** as the primary discovery mechanism (required at build time), and **UDP broadcast** as a runtime fallback when mDNS discovery fails (server not found). This discovery is completely optional - ESP32 root nodes always run their embedded web server and discover external servers if available, but function perfectly without them.

### Design Decisions

**Optional Infrastructure**: The external web server is completely optional. ESP32 devices must continue operating normally even if no external server is discovered or if discovery fails completely. The embedded web server (`mesh_web_server_start()`) MUST ALWAYS run regardless of discovery status.

**Primary mDNS, Fallback UDP**: mDNS is the primary discovery method and is required at build time. The mDNS component must be enabled in sdkconfig and is required as a component dependency. UDP broadcast serves as a runtime fallback mechanism when mDNS discovery fails (server not found), not when the mDNS component is unavailable. This ensures mDNS is always the primary discovery mechanism.

**Discovery Priority**: mDNS discovery is actively attempted first via the discovery task. UDP broadcast listener runs in the background and acts as a runtime fallback when mDNS discovery fails. When mDNS discovery succeeds, the UDP broadcast listener is stopped as an optimization. This ensures mDNS is the primary method while providing UDP broadcast as a fallback mechanism.

**Non-Blocking**: All discovery operations are non-blocking and run in background tasks. Discovery must not delay or interfere with embedded web server startup or normal mesh operation.

**Caching**: Discovered server addresses are cached in NVS (Non-Volatile Storage) to allow immediate use on subsequent boots without waiting for discovery. Cached addresses are also used as a fallback if both discovery methods fail.

**Graceful Degradation**: If mDNS discovery fails (server not found), the system falls back to UDP broadcast. If both discovery methods fail, the system continues without external server discovery. Errors are logged but do not affect embedded web server or mesh operation. Note that mDNS is required at build time - the build will fail if the mDNS component is unavailable.

**Discovery Timing**: Discovery happens AFTER the embedded web server starts. This ensures that the root node is fully functional before attempting to discover optional external services.

## Architecture

### Overall Discovery Flow

```
┌─────────────────────────────────────────────────────────────────┐
│  ESP32 Root Node                                                 │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Embedded Web Server (MUST ALWAYS RUN)                   │  │
│  │  (mesh_web_server_start() on port 80)                    │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Discovery Module (OPTIONAL)                              │  │
│  │  - Started AFTER embedded web server                      │  │
│  │  - Runs in background tasks                               │  │
│  │  - Non-blocking                                           │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────┐  ┌──────────────────────────┐   │
│  │  mDNS Discovery Task      │  │  UDP Broadcast Listener  │   │
│  │  (Primary)                │  │  Task (Fallback)         │   │
│  │  - Query: _lyktparad-web  │  │  - Listen on port 5353   │   │
│  │  - Timeout: 20 seconds    │  │  - Parse JSON payload    │   │
│  └──────────┬───────────────┘  └──────────┬───────────────┘   │
│             │                              │                    │
│             │ mDNS Discovery (Primary)     │                    │
│             │ UDP Broadcast (Fallback)     │                    │
│             │ (First Success Wins)         │                    │
│             ▼                              ▼                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Discovery Result Handler                                │  │
│  │  - Cache in NVS                                          │  │
│  │  - Store for registration                                │  │
│  │  - Stop other discovery method (optional)                │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                        │
                        │ Discovered Server Address
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│  UDP Bridge Module                                              │
│  - Registration with external server                            │
│  - Heartbeat and state updates                                  │
│  - API command handling                                         │
└─────────────────────────────────────────────────────────────────┘
```

### Network Discovery Flow

```
┌─────────────────────────┐                    ┌─────────────────────────┐
│  External Web Server     │                    │  ESP32 Root Node        │
│  (lyktparad-server)      │                    │                         │
│                          │                    │                         │
│  ┌───────────────────┐  │                    │  ┌───────────────────┐  │
│  │  HTTP Server      │  │                    │  │  Embedded HTTP    │  │
│  │  (Port 8080)      │  │                    │  │  Server (Port 80) │  │
│  └───────────────────┘  │                    │  └───────────────────┘  │
│                          │                    │                         │
│  ┌───────────────────┐  │                    │  ┌───────────────────┐  │
│  │  mDNS Service     │──┤  mDNS Query        │  │  mDNS Discovery   │  │
│  │  Advertiser       │  │  (Port 5353)       │  │  (Primary)        │  │
│  │  _lyktparad-web   │  │                    │  └───────────────────┘  │
│  └───────────────────┘  │                    │                         │
│                          │                    │  ┌───────────────────┐  │
│  ┌───────────────────┐  │  UDP Broadcast     │  │  UDP Broadcast    │  │
│  │  UDP Broadcast    │──┤  (Port 5353)       │  │  Listener         │  │
│  │  Task             │  │  JSON Payload      │  │  (Fallback)       │  │
│  │  (Every 30s)      │  │                    │  └───────────────────┘  │
│  └───────────────────┘  │                    │                         │
└─────────────────────────┘                    └─────────────────────────┘
```

## Discovery Methods

### Method 1: mDNS Discovery (Primary)

**Overview**: mDNS (multicast DNS) is a standard zero-configuration networking protocol that allows devices to discover services on a local network without a centralized DNS server. It uses multicast UDP packets on port 5353 to advertise and discover services.

**Service Type**: `_lyktparad-web._tcp`

**Service Name**: Configurable (e.g., "Lyktparad Web Server")

**TXT Records**:
- `version`: Server version (e.g., "1.0.0")
- `protocol`: Protocol type (must be "udp")
- `udp_port`: UDP port number for root node communication (if different from HTTP port)

**Port**: 8080 (HTTP server port)

**Advantages**:
- Standard protocol with broad platform support
- Automatic service registration and unregistration
- Efficient multicast-based discovery
- Built-in support in many operating systems

**Limitations**:
- May not work in network configurations that block multicast traffic
- Requires mDNS component/library on both server and client
- Some firewalls block mDNS packets

### Method 2: UDP Broadcast Discovery (Fallback)

**Overview**: UDP broadcast discovery uses broadcast UDP packets to advertise server presence. The server periodically broadcasts a JSON payload on a known port, and ESP32 root nodes listen for these broadcasts.

**Broadcast Address**: `255.255.255.255` (local network broadcast)

**Broadcast Port**: 5353 (configurable, defaults to 5353 to match mDNS port)

**Broadcast Interval**: 30 seconds (configurable)

**Payload Format**: JSON
```json
{
  "service": "lyktparad-web",
  "port": 8080,
  "udp_port": 8081,
  "protocol": "udp",
  "version": "1.0.0"
}
```

**Advantages**:
- Works even when mDNS is unavailable
- Simple implementation with minimal dependencies
- No special network configuration required

**Limitations**:
- Some networks block UDP broadcasts
- Less efficient than mDNS (broadcasts to entire network)
- Requires periodic broadcasting (not event-driven)

## mDNS Discovery - Server Side

### Service Advertisement

The external web server advertises its presence using mDNS (Bonjour on macOS/Windows, Avahi on Linux). The service is advertised as `_lyktparad-web._tcp` with the HTTP port and metadata in TXT records.

### Implementation (Node.js)

**File**: `lyktparad-server/mdns.js`

**Library**: `bonjour` (pure JavaScript, cross-platform)

**Service Registration**:
```javascript
function registerService(port, serviceName, metadata) {
    // Initialize bonjour library
    if (!init()) {
        return null;  // Graceful degradation if library unavailable
    }

    // Create bonjour instance
    const bonjourInstance = bonjour();

    // Build TXT record from metadata
    const txtRecord = {
        version: metadata.version || '1.0.0',
        protocol: metadata.protocol || 'udp',
        udp_port: String(metadata.udp_port || port)
    };

    // Publish service
    service = bonjourInstance.publish({
        name: serviceName,
        type: '_lyktparad-web._tcp',
        port: port,
        txt: txtRecord
    });

    return service;
}
```

**Service Unregistration**:
```javascript
function unregisterService(serviceAdvertisement) {
    if (!serviceAdvertisement) {
        return false;
    }

    serviceAdvertisement.stop();
    return true;
}
```

### Server Integration

**Service Registration Timing**: Service is registered AFTER the HTTP server starts listening.

**Service Unregistration Timing**: Service is unregistered during graceful server shutdown (SIGINT, SIGTERM handlers).

**Error Handling**: If mDNS library is unavailable or registration fails, the server continues without mDNS. Errors are logged at warning level but do not prevent server startup.

**Code Example**:
```javascript
// In server.js - after server starts listening
server.listen(PORT, () => {
    console.log(`Server started on port ${PORT}`);

    // Register mDNS service (optional)
    if (mdns) {
        const serviceName = 'Lyktparad Web Server';
        const metadata = {
            version: SERVER_VERSION,
            protocol: 'udp',
            udp_port: UDP_PORT
        };
        mdns.registerService(PORT, serviceName, metadata);
    }

    // Start UDP broadcast (fallback)
    startBroadcast({
        broadcastPort: BROADCAST_PORT,
        broadcastInterval: BROADCAST_INTERVAL,
        service: 'lyktparad-web',
        port: PORT,
        udpPort: UDP_PORT,
        protocol: 'udp',
        version: SERVER_VERSION
    });
});

// Graceful shutdown handler
process.on('SIGINT', () => {
    // Stop UDP broadcast
    stopBroadcast();

    // Unregister mDNS service
    if (mdns) {
        const service = mdns.getService();
        if (service) {
            mdns.unregisterService(service);
        }
        mdns.cleanup();
    }

    // Close server
    server.close();
});
```

### TXT Record Size Limitations

mDNS TXT records have a maximum size of 255 bytes per record. Each TXT record entry is encoded as:
```
[length byte][key=value string]
```

Total size calculation:
```
sum of (1 + key.length + 1 + value.length) for each entry
```

The implementation verifies TXT record size and truncates if necessary (typically the version string, which is most likely to be large).

## mDNS Discovery - ESP32 Side

### mDNS Initialization

The ESP32 root node initializes the ESP-IDF mDNS component and sets a hostname for itself. **mDNS is required at build time and is the primary discovery method**. The mDNS component must be enabled in the sdkconfig file and is required as a component dependency. UDP broadcast is only used as a runtime fallback when mDNS discovery fails (server not found), not when the mDNS component is unavailable.

**Implementation**:
```c
esp_err_t mesh_udp_bridge_mdns_init(void)
{
#if MDNS_AVAILABLE
    if (s_mdns_initialized) {
        return ESP_OK;  /* Already initialized */
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return err;
    }

    /* Set hostname for mDNS */
    err = mdns_hostname_set("lyktparad-root");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
        /* Continue even if hostname set fails - discovery can still work */
    }

    s_mdns_initialized = true;
    ESP_LOGI(TAG, "mDNS initialized successfully");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "mDNS component not available, mDNS functionality disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
```

**Build-Time Requirement**: The mDNS component is required at build time. The build will fail if the mDNS component is unavailable. The code checks if mDNS is available at compile time by checking for `CONFIG_MDNS_MAX_INTERFACES` or `CONFIG_MDNS_TASK_PRIORITY` configuration options. These defines are always present when mDNS is properly configured in sdkconfig. Stub implementations exist for robustness but should never be used in normal operation.

### Service Discovery

The ESP32 root node queries for `_lyktparad-web._tcp` service using `mdns_query_ptr()`. The query has a timeout (typically 20 seconds) and requests a maximum number of results (typically 20).

**Implementation**:
```c
esp_err_t mesh_udp_bridge_discover_server(uint32_t timeout_ms, char *server_ip, uint16_t *server_port)
{
    if (server_ip == NULL || server_port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if MDNS_AVAILABLE
    /* Ensure mDNS is initialized */
    if (!s_mdns_initialized) {
        esp_err_t err = mesh_udp_bridge_mdns_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to initialize mDNS for discovery");
            return err;
        }
    }

    /* Query for _lyktparad-web._tcp service */
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_lyktparad-web", "_tcp", timeout_ms, 20, &results);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return err;
    }

    if (results == NULL) {
        ESP_LOGI(TAG, "No external web server found via mDNS");
        return ESP_ERR_NOT_FOUND;
    }

    /* Use first result (if multiple services found, use first) */
    mdns_result_t *result = results;

    /* Extract IP address */
    if (result->addr == NULL) {
        ESP_LOGE(TAG, "mDNS result has no IP address");
        mdns_query_results_free(results);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Convert IP address to string */
    if (result->addr->addr.type == IPADDR_TYPE_V4) {
        struct in_addr addr;
        addr.s_addr = result->addr->addr.u_addr.ip4.addr;
        const char *ip_str = inet_ntoa(addr);
        strncpy(server_ip, ip_str, 15);
        server_ip[15] = '\0';
    }

    /* Extract port - start with HTTP port from service record */
    uint16_t discovered_port = result->port;

    /* Parse TXT records for UDP port */
    if (result->txt != NULL) {
        for (int i = 0; i < result->txt->count; i++) {
            mdns_txt_item_t *item = &result->txt->items[i];
            if (item->key != NULL && item->value != NULL) {
                if (strcmp(item->key, "udp_port") == 0) {
                    int udp_port = atoi(item->value);
                    if (udp_port > 0 && udp_port <= 65535) {
                        discovered_port = (uint16_t)udp_port;
                        ESP_LOGI(TAG, "Found UDP port in TXT record: %d", discovered_port);
                    }
                }
            }
        }
    }

    *server_port = discovered_port;

    /* Free query results */
    mdns_query_results_free(results);

    return ESP_OK;
#else
    ESP_LOGW(TAG, "mDNS not available, discovery disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
```

### TXT Record Parsing

The implementation parses TXT records to extract:
1. **protocol**: Should be "udp" (validated but not required)
2. **udp_port**: UDP port number for root node communication (if different from HTTP port)

If `udp_port` is found in TXT records, it is used instead of the HTTP port from the service record. This allows the server to run HTTP on one port (8080) and UDP on another port (8081).

### Discovery Timing and Integration

Discovery is triggered AFTER the embedded web server starts. The discovery runs in a background FreeRTOS task to avoid blocking the main thread or web server startup.

**Integration Point**:
```c
// In mesh_root_ip_callback() - after embedded web server starts
esp_err_t err = mesh_web_server_start();
if (err == ESP_OK) {
    ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Web server started successfully");

    // Start discovery AFTER web server starts (non-blocking)
    mesh_udp_bridge_start_discovery();
}
```

**Discovery Task**:
```c
static void mesh_udp_bridge_discovery_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Discovery task started");

    char server_ip[16] = {0};
    uint16_t server_port = 0;

    // Perform discovery with 20 second timeout
    esp_err_t err = mesh_udp_bridge_discover_server(20000, server_ip, &server_port);
    if (err == ESP_OK) {
        // Discovery succeeded - cache the address
        mesh_udp_bridge_cache_server(server_ip, server_port);

        // Stop retry task if it's running
        mesh_udp_bridge_stop_retry_task();

        // Store address for registration
        struct in_addr addr;
        if (inet_aton(server_ip, &addr) != 0) {
            uint8_t ip_bytes[4];
            memcpy(ip_bytes, &addr.s_addr, 4);
            mesh_udp_bridge_set_registration(true, ip_bytes, server_port);

            // Stop UDP broadcast listener since mDNS succeeded (optional)
            mesh_udp_bridge_broadcast_listener_stop();

            // Register with external server
            if (esp_mesh_is_root()) {
                mesh_udp_bridge_register();
            }
        }
    } else {
        // Discovery failed - try to use cached address
        esp_err_t cache_err = mesh_udp_bridge_get_cached_server(server_ip, &server_port);
        if (cache_err == ESP_OK) {
            // Use cached address
            struct in_addr addr;
            if (inet_aton(server_ip, &addr) != 0) {
                uint8_t ip_bytes[4];
                memcpy(ip_bytes, &addr.s_addr, 4);
                mesh_udp_bridge_set_registration(true, ip_bytes, server_port);
                mesh_udp_bridge_register();
            }
        } else {
            // No cached address - start retry task
            mesh_udp_bridge_start_retry_task();
        }
    }

    vTaskDelete(NULL);
}
```

### Retry Logic

If initial discovery fails and no cached address is available, a background retry task is started. The retry task uses exponential backoff:

- **Initial delay**: 5 seconds
- **Maximum delay**: 60 seconds
- **Backoff multiplier**: 2x

The retry task continues until discovery succeeds or is explicitly stopped.

## UDP Broadcast Fallback (Runtime)

**Note**: UDP broadcast is only used as a runtime fallback when mDNS discovery fails (server not found). mDNS is required at build time and is always the primary discovery method.

### Server-Side Implementation

The external web server periodically broadcasts its presence using UDP broadcast packets. The broadcast contains a JSON payload with service information.

**Implementation**:
```javascript
// In lyktparad-server/lib/udp-broadcast.js

function startBroadcast(config = {}) {
    // Create UDP socket
    broadcastSocket = dgram.createSocket('udp4');
    broadcastSocket.setBroadcast(true);

    const broadcastPort = config.broadcastPort || 5353;
    const broadcastAddress = '255.255.255.255';
    const broadcastIntervalMs = config.broadcastInterval || 30000;

    // Send initial broadcast immediately
    sendBroadcast(config);

    // Set up periodic broadcast
    broadcastInterval = setInterval(() => {
        sendBroadcast(config);
    }, broadcastIntervalMs);
}

function sendBroadcast(config = {}) {
    const payload = {
        service: config.service || 'lyktparad-web',
        port: config.port || 8080,
        udp_port: config.udpPort || 8081,
        protocol: config.protocol || 'udp',
        version: config.version || '1.0.0'
    };

    const payloadBuffer = Buffer.from(JSON.stringify(payload), 'utf8');

    broadcastSocket.send(payloadBuffer, broadcastPort, broadcastAddress, (err) => {
        if (err) {
            console.debug(`UDP broadcast: Send error (non-critical): ${err.message}`);
        }
    });
}
```

**Broadcast Timing**:
- Initial broadcast sent immediately when broadcasting starts
- Subsequent broadcasts sent every 30 seconds (configurable)
- Broadcast continues until server shuts down

### ESP32-Side Implementation

The ESP32 root node listens for UDP broadcast packets on port 5353. When a broadcast is received, it parses the JSON payload, validates the service name, and extracts the server IP and UDP port.

**Broadcast Listener Task**:
```c
static void mesh_udp_bridge_broadcast_listener_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UDP broadcast listener task started");

    // Create UDP socket
    s_broadcast_listener_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // Set socket to allow broadcast reception
    int broadcast = 1;
    setsockopt(s_broadcast_listener_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // Set socket to non-blocking
    int flags = fcntl(s_broadcast_listener_socket, F_GETFL, 0);
    fcntl(s_broadcast_listener_socket, F_SETFL, flags | O_NONBLOCK);

    // Bind to broadcast port (5353)
    struct sockaddr_in listen_addr = {0};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(5353);
    bind(s_broadcast_listener_socket, (struct sockaddr *)&listen_addr, sizeof(listen_addr));

    // Receive loop
    uint8_t recv_buffer[MAX_BROADCAST_PAYLOAD_SIZE];
    while (s_broadcast_listener_running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t received = recvfrom(s_broadcast_listener_socket, recv_buffer, sizeof(recv_buffer), 0,
                                     (struct sockaddr *)&from_addr, &from_len);

        if (received > 0) {
            handle_broadcast_packet(recv_buffer, received, &from_addr);
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay to prevent busy-waiting
    }

    // Cleanup
    close(s_broadcast_listener_socket);
    vTaskDelete(NULL);
}
```

**Broadcast Packet Handler**:
```c
static void handle_broadcast_packet(const uint8_t *buffer, size_t len, const struct sockaddr_in *from_addr)
{
    // Null-terminate JSON string
    char json[MAX_BROADCAST_PAYLOAD_SIZE + 1];
    memcpy(json, buffer, len);
    json[len] = '\0';

    // Parse JSON
    char service[32] = {0};
    uint16_t http_port = 0;
    uint16_t udp_port = 0;
    char protocol[16] = {0};
    char version[16] = {0};

    esp_err_t err = parse_broadcast_json(json, len, service, &http_port, &udp_port, protocol, version);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to parse broadcast JSON: %s", esp_err_to_name(err));
        return;
    }

    // Validate service name
    if (strcmp(service, "lyktparad-web") != 0) {
        ESP_LOGD(TAG, "Broadcast from wrong service: %s (expected lyktparad-web), ignoring", service);
        return;
    }

    // Validate UDP port
    if (udp_port == 0) {
        ESP_LOGD(TAG, "Invalid UDP port in broadcast: %d, ignoring", udp_port);
        return;
    }

    // Extract IP address from source address
    uint8_t server_ip[4];
    memcpy(server_ip, &from_addr->sin_addr.s_addr, 4);

    // Cache discovered address in NVS
    char ip_str[16];
    inet_ntoa_r(from_addr->sin_addr, ip_str, sizeof(ip_str));
    mesh_udp_bridge_cache_server(ip_str, udp_port);

    // Store for registration
    mesh_udp_bridge_set_registration(true, server_ip, udp_port);

    // Stop mDNS discovery since UDP broadcast succeeded (optional optimization)
    // Note: mDNS discovery may already have succeeded, so this is just an optimization

    // Register with external server
    if (esp_mesh_is_root()) {
        mesh_udp_bridge_register();
    }

    ESP_LOGI(TAG, "Discovered external web server via UDP broadcast: %s:%d", ip_str, udp_port);
}
```

**JSON Parsing**: The implementation uses a simple JSON parser (cJSON or similar) to extract fields from the broadcast payload. The parser handles:
- Service name validation
- Port number extraction (HTTP port and UDP port)
- Protocol validation (should be "udp")
- Version extraction (informational)

## Discovery Priority and Integration

### Parallel Execution

mDNS discovery is actively attempted first via the discovery task. UDP broadcast listener starts in the background when the root node gets an IP address and acts as a runtime fallback when mDNS discovery fails. When mDNS discovery succeeds, the UDP broadcast listener is stopped as an optimization. This ensures mDNS is the primary method while providing UDP broadcast as a fallback mechanism.

### Discovery Result Handling

When mDNS discovery succeeds:
1. The discovered address is cached in NVS
2. The address is stored for registration
3. Registration with the external server is initiated
4. The other discovery method can be stopped as an optimization (optional)

### Discovery State Management

The system tracks discovery state:
- **mDNS in progress**: mDNS query is active
- **UDP broadcast listening**: UDP listener task is running
- **Discovery succeeded**: Address discovered and cached
- **Discovery failed**: Both methods failed, using cached address or retry

### Integration with Registration

Once a server address is discovered (via either method), it is used for registration. The registration process is independent of discovery - it uses the discovered address but handles registration failures separately.

## Caching and Persistence

### NVS Cache Storage

Discovered server addresses are stored in NVS under the namespace `"udp_bridge"` with keys:
- `"server_ip"`: Server IP address as a string (e.g., "192.168.1.100")
- `"server_port"`: Server UDP port as a 16-bit unsigned integer

**Cache Storage**:
```c
esp_err_t mesh_udp_bridge_cache_server(const char *server_ip, uint16_t server_port)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("udp_bridge", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_set_str(nvs_handle, "server_ip", server_ip);
    nvs_set_u16(nvs_handle, "server_port", server_port);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    return ESP_OK;
}
```

**Cache Retrieval**:
```c
esp_err_t mesh_udp_bridge_get_cached_server(char *server_ip, uint16_t *server_port)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("udp_bridge", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = 16;
    err = nvs_get_str(nvs_handle, "server_ip", server_ip, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_u16(nvs_handle, "server_port", server_port);
    nvs_close(nvs_handle);

    return err;
}
```

### Cache Usage Strategy

1. **On Boot**: After embedded web server starts, discovery is initiated. If discovery fails or times out, cached address is retrieved and used.

2. **After Discovery**: When a new server address is discovered, it replaces the cached address (even if different from previous).

3. **Fallback**: If both discovery methods fail, cached address is used (if available). This allows immediate registration on subsequent boots without waiting for discovery.

4. **Cache Expiration**: Currently, cached addresses do not expire. If the server IP changes, new discovery will update the cache. Future enhancements could add cache expiration based on discovery success/failure patterns.

## Error Handling and Graceful Degradation

### mDNS Errors

**Initialization Failure**: If mDNS initialization fails (e.g., component not available, initialization error), the system continues with UDP broadcast only. Error is logged at warning level.

**Query Timeout**: If mDNS query times out (no service found), UDP broadcast listener continues. Cached address is used if available.

**Query Errors**: If mDNS query fails (network error, invalid response), UDP broadcast listener continues. Cached address is used if available.

**Result Parsing Errors**: If IP address or port extraction fails, the result is discarded and UDP broadcast listener continues.

### UDP Broadcast Errors

**Socket Creation Failure**: If UDP socket creation fails, the listener task exits. mDNS discovery continues. Cached address is used if available.

**Bind Failure**: If binding to port 5353 fails (port in use, permission denied), the listener task exits. mDNS discovery continues. Cached address is used if available.

**Receive Errors**: If packet receive fails (non-critical errors like EAGAIN/EWOULDBLOCK), the listener continues. Critical errors are logged but don't stop the listener.

**JSON Parsing Errors**: If JSON parsing fails, the packet is discarded. The listener continues waiting for the next broadcast.

**Validation Errors**: If service name doesn't match or port is invalid, the packet is discarded. The listener continues.

### Discovery Failure Handling

**Both Methods Fail**: If both mDNS and UDP broadcast fail:
1. Cached address is retrieved (if available)
2. Registration is attempted with cached address
3. Retry task is started for background discovery

**No Cached Address**: If both methods fail and no cached address is available:
1. Embedded web server continues operating normally
2. Retry task is started for background discovery
3. Registration is skipped until discovery succeeds

### Network Configuration Issues

**Firewall Blocking mDNS**: If mDNS is blocked by firewall, UDP broadcast is used. If both are blocked, cached address is used.

**Firewall Blocking UDP Broadcast**: If UDP broadcast is blocked, mDNS is used. If both are blocked, cached address is used.

**Network Isolation**: If the root node is isolated from the external server (different network segment, no route), discovery will fail. Embedded web server continues operating normally.

### Logging Strategy

- **Info Level**: Discovery start/stop, successful discovery, cache operations
- **Warning Level**: Discovery failures, initialization failures, graceful degradation
- **Debug Level**: Non-critical errors (timeouts, receive errors), packet validation failures

Errors are logged but never cause embedded web server or mesh operation to fail.

## Implementation Details

### Task Priorities

- **Discovery Task**: Low priority (1) - Does not interfere with mesh or web server
- **Retry Task**: Low priority (1) - Background retry does not affect normal operation
- **UDP Broadcast Listener Task**: Low priority (1) - Packet reception is non-blocking

### Stack Sizes

- **Discovery Task**: 4096 bytes - Sufficient for mDNS query and result parsing
- **Retry Task**: 4096 bytes - Simple retry loop with exponential backoff
- **UDP Broadcast Listener Task**: 4096 bytes - Sufficient for socket operations and JSON parsing

### Memory Management

**mDNS Results**: mDNS query results must be freed using `mdns_query_results_free()` after use. Results are freed immediately after parsing to minimize memory usage.

**JSON Parsing**: JSON payload is parsed into stack-allocated buffers (service name, protocol, version). Maximum sizes are enforced to prevent buffer overflows.

**NVS Operations**: NVS handles are opened and closed for each operation to ensure proper resource management.

### Thread Safety

Discovery operations are thread-safe:
- Discovery task and retry task use state flags protected by task scheduling (FreeRTOS tasks)
- NVS operations are atomic (one operation at a time)
- UDP listener uses a single task (no concurrent access)

### Performance Considerations

**Discovery Timeout**: mDNS query timeout of 20 seconds balances discovery success rate with responsiveness. Shorter timeouts may miss servers on slower networks. Longer timeouts delay fallback to UDP broadcast.

**Broadcast Interval**: 30-second broadcast interval balances network efficiency with discovery latency. Shorter intervals increase network traffic. Longer intervals increase discovery latency.

**Retry Backoff**: Exponential backoff (starting at 5 seconds, max 60 seconds) reduces unnecessary discovery attempts while ensuring eventual discovery when the server becomes available.

## API Reference

### ESP32 Side

#### `mesh_udp_bridge_mdns_init()`
Initialize mDNS component for service discovery.

**Returns**: `ESP_OK` on success, error code on failure

**Side Effects**: Initializes ESP-IDF mDNS component, sets hostname

#### `mesh_udp_bridge_discover_server()`
Discover external web server via mDNS.

**Parameters**:
- `timeout_ms`: Query timeout in milliseconds (10000-30000)
- `server_ip`: Output buffer for server IP (must be at least 16 bytes)
- `server_port`: Output pointer for UDP port

**Returns**: `ESP_OK` on success, error code on failure

**Side Effects**: Performs mDNS query, parses results, extracts IP and port

#### `mesh_udp_bridge_cache_server()`
Cache discovered server address in NVS.

**Parameters**:
- `server_ip`: Server IP address string (e.g., "192.168.1.100")
- `server_port`: Server UDP port

**Returns**: `ESP_OK` on success, error code on failure

**Side Effects**: Stores server address in NVS

#### `mesh_udp_bridge_get_cached_server()`
Retrieve cached server address from NVS.

**Parameters**:
- `server_ip`: Output buffer for server IP (must be at least 16 bytes)
- `server_port`: Output pointer for UDP port

**Returns**: `ESP_OK` on success, `ESP_ERR_NOT_FOUND` if cache is empty, error code on failure

**Side Effects**: Reads server address from NVS

#### `mesh_udp_bridge_start_discovery()`
Start discovery process (mDNS and UDP broadcast in parallel).

**Returns**: None

**Side Effects**: Starts discovery task and UDP broadcast listener task

#### `mesh_udp_bridge_stop_discovery()`
Stop discovery process.

**Returns**: None

**Side Effects**: Stops discovery task, UDP broadcast listener task, and retry task

#### `mesh_udp_bridge_broadcast_listener_start()`
Start UDP broadcast listener task.

**Returns**: None

**Side Effects**: Creates and starts UDP broadcast listener task

#### `mesh_udp_bridge_broadcast_listener_stop()`
Stop UDP broadcast listener task.

**Returns**: None

**Side Effects**: Stops and deletes UDP broadcast listener task

### Server Side

#### `registerService(port, serviceName, metadata)`
Register mDNS service advertisement.

**Parameters**:
- `port`: HTTP server port number
- `serviceName`: Service name (e.g., "Lyktparad Web Server")
- `metadata`: Service metadata object
  - `version`: Server version (e.g., "1.0.0")
  - `protocol`: Protocol type (must be "udp")
  - `udp_port`: UDP port number (if different from HTTP port)

**Returns**: Service advertisement object, or `null` if registration failed

**Side Effects**: Registers mDNS service with Bonjour library

#### `unregisterService(serviceAdvertisement)`
Unregister mDNS service advertisement.

**Parameters**:
- `serviceAdvertisement`: Service advertisement object from `registerService()`

**Returns**: `true` if unregistration succeeded, `false` otherwise

**Side Effects**: Unregisters mDNS service

#### `startBroadcast(config)`
Start periodic UDP broadcast.

**Parameters**:
- `config`: Broadcast configuration object
  - `broadcastPort`: Broadcast port (default: 5353)
  - `broadcastInterval`: Broadcast interval in milliseconds (default: 30000)
  - `service`: Service name (default: "lyktparad-web")
  - `port`: HTTP port
  - `udpPort`: UDP port
  - `protocol`: Protocol (default: "udp")
  - `version`: Server version

**Returns**: `true` if started successfully, `false` otherwise

**Side Effects**: Creates UDP socket, starts periodic broadcast

#### `stopBroadcast()`
Stop periodic UDP broadcast.

**Returns**: None

**Side Effects**: Stops broadcast interval, closes UDP socket

## Integration Points

### Root Node Initialization

Discovery is integrated into the root node initialization flow:

```c
// In mesh_root_ip_callback() - when root node gets IP address
void mesh_root_ip_callback(void)
{
    // ... mesh initialization ...

    // Start embedded web server (MUST ALWAYS RUN)
    esp_err_t err = mesh_web_server_start();
    if (err == ESP_OK) {
        ESP_LOGI(mesh_common_get_tag(), "[ROOT ACTION] Web server started successfully");

        // Start discovery AFTER web server starts (non-blocking)
        mesh_udp_bridge_start_discovery();
    }
}
```

### Server Startup

mDNS registration and UDP broadcast are integrated into server startup:

```javascript
// In server.js - after server starts listening
server.listen(PORT, () => {
    console.log(`Server started on port ${PORT}`);

    // Register mDNS service (optional)
    if (mdns) {
        mdns.registerService(PORT, serviceName, metadata);
    }

    // Start UDP broadcast (fallback)
    startBroadcast(config);
});
```

### Server Shutdown

mDNS unregistration and UDP broadcast stop are integrated into graceful shutdown:

```javascript
// Graceful shutdown handler
process.on('SIGINT', () => {
    stopBroadcast();
    if (mdns) {
        const service = mdns.getService();
        if (service) {
            mdns.unregisterService(service);
        }
        mdns.cleanup();
    }
    server.close();
});
```

### Registration Integration

Once discovery succeeds, registration is initiated:

```c
// In discovery task or broadcast handler - after discovery succeeds
if (esp_mesh_is_root()) {
    mesh_udp_bridge_register();
}
```

The registration process is independent of discovery - it handles registration failures separately and can retry registration without re-running discovery.

---

**Note**: This discovery system is designed for local network environments. For wide-area deployments, additional discovery mechanisms (e.g., cloud-based service discovery) may be needed. The current implementation focuses on zero-configuration discovery within a single local network segment.
