# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

#idf_component_register(SRCS ${app_sources})

idf_component_register(
    SRCS "mesh.c" "mesh_light.c" "mesh_web.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip esp_wifi esp_http_server
)