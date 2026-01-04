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
set(PLUGINS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
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

    # File embedding: Convert HTML/JS/CSS files to C string headers
    set(CONVERT_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/plugins/convert_file_to_c_string.py")
    set(GENERATED_PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_plugin_headers")

    # Create output directory for generated headers
    file(MAKE_DIRECTORY "${GENERATED_PLUGIN_DIR}")

    # Convert HTML files
    foreach(html_file ${PLUGIN_HTML_FILES})
        # Extract plugin name from file path
        get_filename_component(plugin_dir "${html_file}" DIRECTORY)
        get_filename_component(plugin_name "${plugin_dir}" NAME)
        get_filename_component(html_basename "${html_file}" NAME_WE)

        # Generate header file name
        set(header_file "${GENERATED_PLUGIN_DIR}/plugin_${plugin_name}_html.h")

        # Create custom command to convert HTML to C string
        add_custom_command(
            OUTPUT "${header_file}"
            COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=" "PYTHONIOENCODING=utf-8"
                python3 "${CONVERT_SCRIPT}" "${html_file}" "${header_file}" "html"
            DEPENDS "${html_file}" "${CONVERT_SCRIPT}"
            COMMENT "Generating C string header from ${html_file}"
            VERBATIM
        )
    endforeach()

    # Convert JS files
    foreach(js_file ${PLUGIN_JS_FILES})
        # Extract plugin name from file path
        get_filename_component(js_dir "${js_file}" DIRECTORY)
        get_filename_component(plugin_dir "${js_dir}" DIRECTORY)
        get_filename_component(plugin_name "${plugin_dir}" NAME)
        get_filename_component(js_basename "${js_file}" NAME_WE)

        # Generate header file name
        set(header_file "${GENERATED_PLUGIN_DIR}/plugin_${plugin_name}_js.h")

        # Create custom command to convert JS to C string
        add_custom_command(
            OUTPUT "${header_file}"
            COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=" "PYTHONIOENCODING=utf-8"
                python3 "${CONVERT_SCRIPT}" "${js_file}" "${header_file}" "js"
            DEPENDS "${js_file}" "${CONVERT_SCRIPT}"
            COMMENT "Generating C string header from ${js_file}"
            VERBATIM
        )
    endforeach()

    # Convert CSS files
    foreach(css_file ${PLUGIN_CSS_FILES})
        # Extract plugin name from file path
        get_filename_component(css_dir "${css_file}" DIRECTORY)
        get_filename_component(plugin_dir "${css_dir}" DIRECTORY)
        get_filename_component(plugin_name "${plugin_dir}" NAME)
        get_filename_component(css_basename "${css_file}" NAME_WE)

        # Generate header file name
        set(header_file "${GENERATED_PLUGIN_DIR}/plugin_${plugin_name}_css.h")

        # Create custom command to convert CSS to C string
        add_custom_command(
            OUTPUT "${header_file}"
            COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=" "PYTHONIOENCODING=utf-8"
                python3 "${CONVERT_SCRIPT}" "${css_file}" "${header_file}" "css"
            DEPENDS "${css_file}" "${CONVERT_SCRIPT}"
            COMMENT "Generating C string header from ${css_file}"
            VERBATIM
        )
    endforeach()

    # Add generated headers directory to include directories
    list(APPEND PLUGIN_INCLUDE_DIRS "${GENERATED_PLUGIN_DIR}")

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

# HTML Generation: Extract template and generate HTML with plugin content
set(EXTRACT_TEMPLATE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../tools/extract_html_template.py")
set(EMBEDDED_TEMPLATE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../templates/embedded_template.html")
set(GENERATE_HTML_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_html.py")
set(GENERATED_HTML_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_html.h")
set(EXTERNAL_HTML_FILE "${PROJECT_ROOT}/lyktparad-server/web-ui/index.html")

# Step 1: Extract embedded template from mesh_web.c (if template doesn't exist or mesh_web.c is newer)
if(EXISTS "${EXTRACT_TEMPLATE_SCRIPT}" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mesh_web.c")
    add_custom_command(
        OUTPUT "${EMBEDDED_TEMPLATE_FILE}"
        COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8 PYTHONPATH="${CMAKE_CURRENT_SOURCE_DIR}/../tools"
            python3 "${EXTRACT_TEMPLATE_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}/mesh_web.c" "${EMBEDDED_TEMPLATE_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mesh_web.c" "${EXTRACT_TEMPLATE_SCRIPT}"
        COMMENT "Extracting embedded HTML template from mesh_web.c"
        VERBATIM
    )
    message(STATUS "Template extraction command added")
endif()

# Step 2: Generate embedded HTML with plugin content
# Always generate if script exists (mesh_web.c depends on generated_html.h)
if(EXISTS "${GENERATE_HTML_SCRIPT}")
    # Ensure template file exists (extract if needed, or use placeholder)
    if(NOT EXISTS "${EMBEDDED_TEMPLATE_FILE}")
        # Create minimal placeholder template if extraction didn't run
        file(WRITE "${EMBEDDED_TEMPLATE_FILE}" "<!DOCTYPE html><html><head><title>Lyktparad</title></head><body><h1>Lyktparad</h1>{{PLUGIN_HTML}}</body></html>")
        message(STATUS "Created placeholder template file")
    endif()

    # Ensure output directory exists
    get_filename_component(GENERATED_HTML_DIR "${GENERATED_HTML_HEADER}" DIRECTORY)
    file(MAKE_DIRECTORY "${GENERATED_HTML_DIR}")

    # Collect all plugin HTML/CSS/JS files as dependencies
    set(HTML_GEN_DEPENDS "${EMBEDDED_TEMPLATE_FILE}" "${GENERATE_HTML_SCRIPT}")
    if(EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}")
        list(APPEND HTML_GEN_DEPENDS ${PLUGIN_HTML_FILES})
        list(APPEND HTML_GEN_DEPENDS ${PLUGIN_CSS_FILES})
        list(APPEND HTML_GEN_DEPENDS ${PLUGIN_JS_FILES})
    endif()

    # Generate file at configure time as fallback (ensures it always exists)
    # This is a safety measure - the custom command should handle updates during build
    if(NOT EXISTS "${GENERATED_HTML_HEADER}")
        message(STATUS "Generating ${GENERATED_HTML_HEADER} at configure time")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8 PYTHONPATH="${CMAKE_CURRENT_SOURCE_DIR}/../tools"
                python3 "${GENERATE_HTML_SCRIPT}" embedded "${EMBEDDED_TEMPLATE_FILE}" "${PLUGINS_DIR}" "${GENERATED_HTML_HEADER}"
            RESULT_VARIABLE GEN_RESULT
            OUTPUT_VARIABLE GEN_OUTPUT
            ERROR_VARIABLE GEN_ERROR
        )
        if(NOT GEN_RESULT EQUAL 0)
            message(WARNING "Failed to generate HTML at configure time: ${GEN_ERROR}")
            # Create minimal fallback header
            file(WRITE "${GENERATED_HTML_HEADER}"
                "#ifndef GENERATED_HTML_PAGE_H\n#define GENERATED_HTML_PAGE_H\nstatic const char html_page[] = \"<html><body>Error: HTML generation failed</body></html>\";\n#endif\n")
        endif()
    endif()

    # Add custom command for build-time regeneration (when dependencies change)
    add_custom_command(
        OUTPUT "${GENERATED_HTML_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8 PYTHONPATH="${CMAKE_CURRENT_SOURCE_DIR}/../tools"
            python3 "${GENERATE_HTML_SCRIPT}" embedded "${EMBEDDED_TEMPLATE_FILE}" "${PLUGINS_DIR}" "${GENERATED_HTML_HEADER}"
        DEPENDS ${HTML_GEN_DEPENDS}
        COMMENT "Generating embedded HTML page with plugin content"
        VERBATIM
    )

    # Create a custom target to ensure the file is generated before compilation
    add_custom_target(generate_html_header ALL DEPENDS "${GENERATED_HTML_HEADER}")

    message(STATUS "Embedded HTML generation command added")
endif()

# Step 3: Generate external HTML with plugin content
if(EXISTS "${GENERATE_HTML_SCRIPT}" AND EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}")
    if(EXISTS "${PROJECT_ROOT}/lyktparad-server/web-ui")
        # Use external webserver index.html as template (it has placeholders)
        set(EXTERNAL_TEMPLATE_FILE "${PROJECT_ROOT}/lyktparad-server/web-ui/index.html")

        # Collect all plugin HTML/CSS/JS files as dependencies
        set(EXTERNAL_HTML_GEN_DEPENDS "${EXTERNAL_TEMPLATE_FILE}" "${GENERATE_HTML_SCRIPT}")
        list(APPEND EXTERNAL_HTML_GEN_DEPENDS ${PLUGIN_HTML_FILES})
        list(APPEND EXTERNAL_HTML_GEN_DEPENDS ${PLUGIN_CSS_FILES})
        list(APPEND EXTERNAL_HTML_GEN_DEPENDS ${PLUGIN_JS_FILES})

        add_custom_command(
            OUTPUT "${EXTERNAL_HTML_FILE}"
            COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8 PYTHONPATH="${CMAKE_CURRENT_SOURCE_DIR}/../tools"
                python3 "${GENERATE_HTML_SCRIPT}" external "${EXTERNAL_TEMPLATE_FILE}" "${PLUGINS_DIR}" "${EXTERNAL_HTML_FILE}"
            DEPENDS ${EXTERNAL_HTML_GEN_DEPENDS}
            COMMENT "Generating external webserver index.html with plugin content"
            VERBATIM
        )
        message(STATUS "External HTML generation command added")
    endif()
endif()

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
)

list(APPEND ALL_SOURCES ${PLUGIN_SOURCES})

# Combine existing include directories with plugin include directories
set(ALL_INCLUDE_DIRS "." "../include")
list(APPEND ALL_INCLUDE_DIRS ${PLUGIN_INCLUDE_DIRS})
# Add generated HTML header directory (for generated_html.h)
list(APPEND ALL_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}")

idf_component_register(
    SRCS ${ALL_SOURCES}
    INCLUDE_DIRS ${ALL_INCLUDE_DIRS}
    REQUIRES ${COMPONENT_REQUIRES}
)

# Ensure generated_html.h is built before mesh_web.c compiles
# In ESP-IDF, we use OBJECT_DEPENDS to ensure the generated file exists before compilation
# The ALL keyword in add_custom_target ensures it builds automatically
if(EXISTS "${GENERATE_HTML_SCRIPT}" AND TARGET generate_html_header)
    set_source_files_properties("mesh_web.c" PROPERTIES OBJECT_DEPENDS "${GENERATED_HTML_HEADER}")
endif()