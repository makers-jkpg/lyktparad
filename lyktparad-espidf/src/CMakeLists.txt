# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

#idf_component_register(SRCS ${app_sources})

# Build component requirements list
# mDNS is required at build time and is the primary discovery method for the external web server.
# UDP broadcast is only used as a runtime fallback when mDNS discovery fails (server not found).
# The build will fail if mDNS component is unavailable, ensuring mDNS is always the primary discovery mechanism.
set(COMPONENT_REQUIRES led_strip esp_wifi esp_http_server esp_https_ota esp_http_client app_update mdns)

idf_component_register(
    SRCS "mesh.c" "mesh_common.c" "mesh_root.c" "mesh_child.c" "light_neopixel.c" "mesh_web.c" "light_common_cathode.c" "mode_sequence_root.c" "mode_sequence_node.c" "mode_effects_node.c" "mesh_version.c" "mesh_ota.c" "root_status_led.c" "mesh_udp_bridge.c" "plugin_system.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES ${COMPONENT_REQUIRES}
)