# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

#idf_component_register(SRCS ${app_sources})

idf_component_register(
    SRCS "mesh.c" "mesh_common.c" "mesh_root.c" "mesh_child.c" "light_neopixel.c" "mesh_web.c" "light_common_cathode.c" "mesh_gpio.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip esp_wifi esp_http_server
)