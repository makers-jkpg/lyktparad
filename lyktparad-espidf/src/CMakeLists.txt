# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

#idf_component_register(SRCS ${app_sources})

# Build component requirements list
# mDNS is required at build time and is the primary discovery method for the external web server.
# UDP broadcast is only used as a runtime fallback when mDNS discovery fails (server not found).
# The build will fail if mDNS component is unavailable, ensuring mDNS is always the primary discovery mechanism.
set(COMPONENT_REQUIRES led_strip esp_wifi esp_http_server esp_https_ota esp_http_client app_update mdns)

# Plugin system build integration
# Discover plugins in src/plugins/ directory and automatically include them in the build
# Use CMAKE_SOURCE_DIR/src/plugins for consistent path resolution across CMake configuration passes
set(PLUGINS_DIR "${CMAKE_SOURCE_DIR}/src/plugins")
set(PLUGIN_SOURCES "")
set(PLUGIN_INCLUDE_DIRS "")
set(PLUGIN_HTML_FILES "")
set(PLUGIN_JS_FILES "")
set(PLUGIN_CSS_FILES "")
set(PLUGIN_NAMES "")

if(EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}")
    message(STATUS "Scanning for plugins in ${PLUGINS_DIR}")

    # Find all subdirectories in plugins directory (direct children only)
    file(GLOB plugin_dirs RELATIVE "${PLUGINS_DIR}" "${PLUGINS_DIR}/*")

    foreach(plugin_dir ${plugin_dirs})
        set(plugin_path "${PLUGINS_DIR}/${plugin_dir}")

        # Check if it's a directory (not a file)
        if(IS_DIRECTORY "${plugin_path}")
            # Extract plugin name from directory name
            set(plugin_name "${plugin_dir}")

            # Find plugin source files: <plugin-name>_plugin.c and <plugin-name>_plugin.h
            set(plugin_c_file "${plugin_path}/${plugin_name}_plugin.c")
            set(plugin_h_file "${plugin_path}/${plugin_name}_plugin.h")

            # Check if required files exist
            if(EXISTS "${plugin_c_file}" AND EXISTS "${plugin_h_file}")
                # Check for duplicate plugin directory names
                list(FIND PLUGIN_NAMES "${plugin_name}" plugin_name_index)
                if(NOT plugin_name_index EQUAL -1)
                    message(WARNING "Plugin directory '${plugin_name}' already found - skipping duplicate")
                else()
                    list(APPEND PLUGIN_NAMES "${plugin_name}")
                    message(STATUS "  Found plugin: ${plugin_name}")

                    # Add plugin .c file to sources (relative path from src/)
                    list(APPEND PLUGIN_SOURCES "plugins/${plugin_name}/${plugin_name}_plugin.c")

                    # Add plugin directory to include directories
                    list(APPEND PLUGIN_INCLUDE_DIRS "${plugin_path}")

                    # Find optional web files: HTML, JS, CSS
                    set(plugin_html_file "${plugin_path}/${plugin_name}.html")
                    set(plugin_index_html_file "${plugin_path}/index.html")
                    set(plugin_js_file "${plugin_path}/js/${plugin_name}.js")
                    set(plugin_css_file "${plugin_path}/css/${plugin_name}.css")

                    if(EXISTS "${plugin_html_file}")
                        list(APPEND PLUGIN_HTML_FILES "${plugin_html_file}")
                        message(STATUS "    HTML: ${plugin_name}.html")
                    elseif(EXISTS "${plugin_index_html_file}")
                        list(APPEND PLUGIN_HTML_FILES "${plugin_index_html_file}")
                        message(STATUS "    HTML: index.html")
                    endif()

                    if(EXISTS "${plugin_js_file}")
                        list(APPEND PLUGIN_JS_FILES "${plugin_js_file}")
                        message(STATUS "    JS: js/${plugin_name}.js")
                    endif()

                    if(EXISTS "${plugin_css_file}")
                        list(APPEND PLUGIN_CSS_FILES "${plugin_css_file}")
                        message(STATUS "    CSS: css/${plugin_name}.css")
                    endif()
                endif()
            else()
                if(NOT EXISTS "${plugin_c_file}")
                    message(WARNING "Plugin directory '${plugin_name}' missing required file: ${plugin_name}_plugin.c")
                endif()
                if(NOT EXISTS "${plugin_h_file}")
                    message(WARNING "Plugin directory '${plugin_name}' missing required file: ${plugin_name}_plugin.h")
                endif()
            endif()
        endif()
    endforeach()

    if(PLUGIN_SOURCES)
        message(STATUS "Found ${PLUGIN_SOURCES} plugin source file(s)")
    else()
        message(STATUS "No plugins found (plugins directory is empty or contains no valid plugins)")
    endif()

    # Plugin file conversion to C string headers has been removed - simple static HTML page is now embedded directly in mesh_web.c

    # External webserver file sharing: Copy plugin HTML/JS/CSS files to external webserver
    # Find external webserver directory (should be at ../lyktparad-server/web-ui/plugins/)
    get_filename_component(PROJECT_ROOT "${CMAKE_SOURCE_DIR}" DIRECTORY)
    set(EXTERNAL_WEBUI_PLUGINS_DIR "${PROJECT_ROOT}/lyktparad-server/web-ui/plugins")

    if(EXISTS "${PROJECT_ROOT}/lyktparad-server")
        message(STATUS "Copying plugin files to external webserver: ${EXTERNAL_WEBUI_PLUGINS_DIR}")

        # Create plugins directory in external webserver
        file(MAKE_DIRECTORY "${EXTERNAL_WEBUI_PLUGINS_DIR}")

        # Copy HTML, JS, and CSS files for each plugin
        foreach(plugin_dir ${plugin_dirs})
            set(plugin_path "${PLUGINS_DIR}/${plugin_dir}")

            if(IS_DIRECTORY "${plugin_path}")
                set(plugin_name "${plugin_dir}")
                set(plugin_c_file "${plugin_path}/${plugin_name}_plugin.c")
                set(plugin_h_file "${plugin_path}/${plugin_name}_plugin.h")

                # Only copy if plugin has required files (valid plugin)
                if(EXISTS "${plugin_c_file}" AND EXISTS "${plugin_h_file}")
                    set(dest_plugin_dir "${EXTERNAL_WEBUI_PLUGINS_DIR}/${plugin_name}")
                    file(MAKE_DIRECTORY "${dest_plugin_dir}")

                    # Copy HTML file (either <plugin-name>.html or index.html)
                    set(plugin_html_file "${plugin_path}/${plugin_name}.html")
                    set(plugin_index_html_file "${plugin_path}/index.html")

                    if(EXISTS "${plugin_html_file}")
                        configure_file("${plugin_html_file}" "${dest_plugin_dir}/${plugin_name}.html" COPYONLY)
                    elseif(EXISTS "${plugin_index_html_file}")
                        configure_file("${plugin_index_html_file}" "${dest_plugin_dir}/index.html" COPYONLY)
                    endif()

                    # Copy JS file (if exists)
                    set(plugin_js_file "${plugin_path}/js/${plugin_name}.js")
                    set(plugin_js_dir "${dest_plugin_dir}/js")
                    if(EXISTS "${plugin_js_file}")
                        file(MAKE_DIRECTORY "${plugin_js_dir}")
                        configure_file("${plugin_js_file}" "${plugin_js_dir}/${plugin_name}.js" COPYONLY)
                    endif()

                    # Copy CSS file (if exists)
                    set(plugin_css_file "${plugin_path}/css/${plugin_name}.css")
                    set(plugin_css_dir "${dest_plugin_dir}/css")
                    if(EXISTS "${plugin_css_file}")
                        file(MAKE_DIRECTORY "${plugin_css_dir}")
                        configure_file("${plugin_css_file}" "${plugin_css_dir}/${plugin_name}.css" COPYONLY)
                    endif()
                endif()
            endif()
        endforeach()

        message(STATUS "Plugin files copied to external webserver")
    else()
        message(STATUS "External webserver directory not found: ${PROJECT_ROOT}/lyktparad-server (plugin files not copied)")
    endif()

else()
    message(STATUS "Plugins directory does not exist: ${PLUGINS_DIR} (this is OK, build will continue)")
endif()

# HTML generation has been removed - simple static HTML page is now embedded directly in mesh_web.c

# Combine existing sources with plugin sources
set(ALL_SOURCES
    "mesh.c"
    "mesh_common.c"
    "mesh_root.c"
    "mesh_child.c"
    "light_neopixel.c"
    "mesh_web.c"
    "light_common_cathode.c"
    "mesh_version.c"
    "mesh_ota.c"
    "root_status_led.c"
    "mesh_udp_bridge.c"
    "plugin_system.c"
    "plugin_light.c"
    "plugin_web_ui.c"
)

list(APPEND ALL_SOURCES ${PLUGIN_SOURCES})

# Combine existing include directories with plugin include directories
set(ALL_INCLUDE_DIRS "." "../include")
list(APPEND ALL_INCLUDE_DIRS ${PLUGIN_INCLUDE_DIRS})

idf_component_register(
    SRCS ${ALL_SOURCES}
    INCLUDE_DIRS ${ALL_INCLUDE_DIRS}
    REQUIRES ${COMPONENT_REQUIRES}
)

# Get the component target name after registration
# In ESP-IDF, the component target is typically __idf_<component_name>
# Since we're in src/, the component name is "src"
get_filename_component(COMPONENT_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(COMPONENT_TARGET_NAME "__idf_${COMPONENT_DIR_NAME}")

# Plugin file conversion custom commands have been removed - simple static HTML page is now embedded directly in mesh_web.c

# HTML generation custom commands have been removed - simple static HTML page is now embedded directly in mesh_web.c