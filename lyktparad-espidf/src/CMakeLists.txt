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

    # File embedding: Convert HTML/JS/CSS files to C string headers
    # Use CMAKE_SOURCE_DIR (project root) instead of CMAKE_CURRENT_SOURCE_DIR to ensure
    # paths resolve correctly in both project-level and component-level CMake configuration passes
    set(CONVERT_SCRIPT "${CMAKE_SOURCE_DIR}/src/plugins/convert_file_to_c_string.py")
    message(STATUS "  -> CONVERT_SCRIPT resolved to: ${CONVERT_SCRIPT}")
    if(EXISTS "${CONVERT_SCRIPT}")
        message(STATUS "  -> CONVERT_SCRIPT found: ${CONVERT_SCRIPT}")
    else()
        message(WARNING "  -> CONVERT_SCRIPT not found: ${CONVERT_SCRIPT}")
    endif()
    set(GENERATED_PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_plugin_headers")

    # Create output directory for generated headers
    file(MAKE_DIRECTORY "${GENERATED_PLUGIN_DIR}")

    # NOTE: Custom command registration for plugin file conversion is deferred until after
    # idf_component_register to avoid "command is not scriptable" error during requirements
    # discovery phase. Custom commands will be registered after component registration (see below).
    message(STATUS "Plugin file conversion custom commands will be registered after component registration")

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
# This section generates embedded HTML with plugin content for the embedded webserver
# Use CMAKE_SOURCE_DIR (project root) instead of CMAKE_CURRENT_SOURCE_DIR/../ to ensure
# paths resolve correctly in both project-level and component-level CMake configuration passes
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
# PlatformIO may filter STATUS messages, but WARNING messages should always appear
message(WARNING "=== HTML Generation Configuration ===")
set(EXTRACT_TEMPLATE_SCRIPT "${CMAKE_SOURCE_DIR}/tools/extract_html_template.py")
set(EMBEDDED_TEMPLATE_FILE "${CMAKE_SOURCE_DIR}/templates/embedded_template.html")
set(GENERATE_HTML_SCRIPT "${CMAKE_SOURCE_DIR}/tools/generate_html.py")
set(GENERATED_HTML_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_html.h")
set(EXTERNAL_HTML_FILE "${PROJECT_ROOT}/lyktparad-server/web-ui/index.html")

message(STATUS "Checking for extract template script: ${EXTRACT_TEMPLATE_SCRIPT}")
# Step 1: Extract embedded template from mesh_web.c (if template doesn't exist or mesh_web.c is newer)
# Use CMAKE_SOURCE_DIR/src/mesh_web.c for consistent path resolution
# NOTE: Custom command registration is deferred until after idf_component_register
# to avoid "command is not scriptable" error during requirements discovery phase
set(MESH_WEB_C_FILE "${CMAKE_SOURCE_DIR}/src/mesh_web.c")
set(EXTRACT_TEMPLATE_AVAILABLE FALSE)
if(EXISTS "${EXTRACT_TEMPLATE_SCRIPT}" AND EXISTS "${MESH_WEB_C_FILE}")
    message(STATUS "  -> Extract template script found, will add custom command after component registration")
    set(EXTRACT_TEMPLATE_AVAILABLE TRUE)
else()
    if(NOT EXISTS "${EXTRACT_TEMPLATE_SCRIPT}")
        message(STATUS "  -> Extract template script NOT found: ${EXTRACT_TEMPLATE_SCRIPT}")
    endif()
    if(NOT EXISTS "${MESH_WEB_C_FILE}")
        message(STATUS "  -> mesh_web.c NOT found: ${MESH_WEB_C_FILE}")
    endif()
endif()

# Step 2: Generate embedded HTML with plugin content
# Always generate if script exists (mesh_web.c depends on generated_html.h)
message(STATUS "Checking for generate HTML script: ${GENERATE_HTML_SCRIPT}")
if(EXISTS "${GENERATE_HTML_SCRIPT}")
    message(STATUS "  -> Generate HTML script found, setting up HTML generation")

    # Template file must exist - it should be extracted from mesh_web.c by the template extraction custom command
    # If template doesn't exist, build will fail with clear error during build-time generation
    if(EXISTS "${EMBEDDED_TEMPLATE_FILE}")
        message(STATUS "  -> Template file exists: ${EMBEDDED_TEMPLATE_FILE}")
    else()
        message(WARNING "  -> Template file not found: ${EMBEDDED_TEMPLATE_FILE}")
        message(WARNING "  -> Template should be extracted from mesh_web.c by template extraction custom command")
        message(WARNING "  -> Build will fail during build-time generation if template is still missing")
    endif()

    # Ensure output directory exists
    get_filename_component(GENERATED_HTML_DIR "${GENERATED_HTML_HEADER}" DIRECTORY)
    file(MAKE_DIRECTORY "${GENERATED_HTML_DIR}")
    message(STATUS "  -> Output directory: ${GENERATED_HTML_DIR}")

    # Collect all plugin HTML/CSS/JS files as dependencies
    set(HTML_GEN_DEPENDS "${EMBEDDED_TEMPLATE_FILE}" "${GENERATE_HTML_SCRIPT}")
    if(EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}")
        list(APPEND HTML_GEN_DEPENDS ${PLUGIN_HTML_FILES})
        list(APPEND HTML_GEN_DEPENDS ${PLUGIN_CSS_FILES})
        list(APPEND HTML_GEN_DEPENDS ${PLUGIN_JS_FILES})
        message(STATUS "  -> Added ${PLUGIN_HTML_FILES} plugin files as dependencies")
    else()
        message(STATUS "  -> Plugins directory not found, HTML generation will proceed without plugins")
    endif()

    # NOTE: Configure-time generation is deferred until after idf_component_register
    # to avoid "command is not scriptable" error during requirements discovery phase.
    # The build-time custom commands will handle HTML generation, and configure-time
    # generation (if needed) will run after component registration.
    message(STATUS "  -> Configure-time HTML generation will run after component registration if needed")

    # NOTE: Custom command registration is deferred until after idf_component_register
    # to avoid "command is not scriptable" error during requirements discovery phase
    # Custom commands will be registered after component registration (see below)
    message(STATUS "  -> Embedded HTML generation will be configured after component registration")
else()
    message(WARNING "Generate HTML script NOT found: ${GENERATE_HTML_SCRIPT}")
    message(WARNING "HTML generation will be skipped - embedded webserver may not work correctly!")
endif()
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
message(WARNING "=== End HTML Generation Configuration ===")

# Step 3: Generate external HTML with plugin content
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
message(WARNING "=== External HTML Generation Configuration ===")
if(EXISTS "${GENERATE_HTML_SCRIPT}" AND EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}")
    message(STATUS "Checking for external webserver directory: ${PROJECT_ROOT}/lyktparad-server/web-ui")
    if(EXISTS "${PROJECT_ROOT}/lyktparad-server/web-ui")
        message(STATUS "  -> External webserver directory found")
        # Use external webserver index.html as template (it has placeholders)
        set(EXTERNAL_TEMPLATE_FILE "${PROJECT_ROOT}/lyktparad-server/web-ui/index.html")

        # Collect all plugin HTML/CSS/JS files as dependencies
        set(EXTERNAL_HTML_GEN_DEPENDS "${EXTERNAL_TEMPLATE_FILE}" "${GENERATE_HTML_SCRIPT}")
        list(APPEND EXTERNAL_HTML_GEN_DEPENDS ${PLUGIN_HTML_FILES})
        list(APPEND EXTERNAL_HTML_GEN_DEPENDS ${PLUGIN_CSS_FILES})
        list(APPEND EXTERNAL_HTML_GEN_DEPENDS ${PLUGIN_JS_FILES})

        # NOTE: Custom command registration is deferred until after idf_component_register
        # to avoid "command is not scriptable" error during requirements discovery phase
        # Custom command will be registered after component registration (see below)
        message(STATUS "  -> External HTML generation will be configured after component registration")
    else()
        message(STATUS "  -> External webserver directory not found, skipping external HTML generation")
    endif()
else()
    if(NOT EXISTS "${GENERATE_HTML_SCRIPT}")
        message(STATUS "  -> Generate HTML script not found, skipping external HTML generation")
    endif()
    if(NOT EXISTS "${PLUGINS_DIR}" OR NOT IS_DIRECTORY "${PLUGINS_DIR}")
        message(STATUS "  -> Plugins directory not found, skipping external HTML generation")
    endif()
endif()
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
message(WARNING "=== End External HTML Generation Configuration ===")

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

# Get the component target name after registration
# In ESP-IDF, the component target is typically __idf_<component_name>
# Since we're in src/, the component name is "src"
get_filename_component(COMPONENT_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(COMPONENT_TARGET_NAME "__idf_${COMPONENT_DIR_NAME}")

# Plugin File Conversion Custom Commands Registration
# These commands must be registered AFTER idf_component_register to avoid
# "command is not scriptable" error during ESP-IDF requirements discovery phase
if(EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}" AND PLUGIN_HTML_FILES)
    # Use WARNING level for section headers to ensure visibility in PlatformIO build logs
    message(WARNING "=== Registering Plugin File Conversion Custom Commands (after component registration) ===")

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

    # Use WARNING level for section headers to ensure visibility in PlatformIO build logs
    message(WARNING "=== End Plugin File Conversion Custom Commands Registration ===")
endif()

# HTML Generation Custom Commands Registration
# These commands must be registered AFTER idf_component_register to avoid
# "command is not scriptable" error during ESP-IDF requirements discovery phase
# ESP-IDF has two CMake passes: (1) Requirements discovery - only scriptable commands,
# (2) Full configuration - all commands allowed. Custom commands modify build graph
# and must be registered during full configuration phase.
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
# PlatformIO may filter STATUS messages, but WARNING messages should always appear
message(WARNING "=== Registering HTML Generation Custom Commands (after component registration) ===")

# Configure-time generation is removed to prevent placeholder files from interfering with build-time generation.
# Build-time custom command will always generate the file, ensuring actual HTML content is produced.
# If build-time generation fails, the build will fail with clear error messages (no silent placeholder fallback).

# Step 1: Register template extraction custom command (if available)
if(EXTRACT_TEMPLATE_AVAILABLE)
    # Use WARNING level for critical command registration to ensure visibility in PlatformIO build logs
    message(WARNING "Registering template extraction custom command")
    add_custom_command(
        OUTPUT "${EMBEDDED_TEMPLATE_FILE}"
        COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8 PYTHONPATH="${CMAKE_SOURCE_DIR}/tools"
            python3 "${EXTRACT_TEMPLATE_SCRIPT}" "${MESH_WEB_C_FILE}" "${EMBEDDED_TEMPLATE_FILE}"
        DEPENDS "${MESH_WEB_C_FILE}" "${EXTRACT_TEMPLATE_SCRIPT}"
        COMMENT "Extracting embedded HTML template from mesh_web.c"
        VERBATIM
    )
    message(STATUS "  -> Template extraction command registered")
endif()

# Step 2: Register embedded HTML generation custom commands (if script exists)
if(EXISTS "${GENERATE_HTML_SCRIPT}")
    # Use WARNING level for critical command registration to ensure visibility in PlatformIO build logs
    message(WARNING "Registering embedded HTML generation custom commands")

    # Add custom command for build-time HTML generation
    # This command will always run during build to ensure actual HTML content is generated
    # We remove any existing file first to force regeneration and prevent placeholder issues
    # Content verification ensures placeholder content is not used
    message(WARNING "=== HTML GENERATION: Adding build-time custom command ===")
    message(STATUS "  -> Output file: ${GENERATED_HTML_HEADER}")
    message(STATUS "  -> Dependencies: ${HTML_GEN_DEPENDS}")

    # Create a helper script for content verification
    set(VERIFY_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/verify_html_content.sh")
    file(WRITE "${VERIFY_SCRIPT}" "#!/bin/sh\n"
        "FILE=\"$1\"\n"
        "if [ ! -f \"$FILE\" ]; then\n"
        "  echo \"ERROR: Generated file does not exist: $FILE\"\n"
        "  exit 1\n"
        "fi\n"
        "if [ ! -s \"$FILE\" ]; then\n"
        "  echo \"ERROR: Generated file is empty: $FILE\"\n"
        "  exit 1\n"
        "fi\n"
        "SIZE=$(stat -f%z \"$FILE\" 2>/dev/null || stat -c%s \"$FILE\" 2>/dev/null || echo 0)\n"
        "if [ \"$SIZE\" -lt 1000 ]; then\n"
        "  echo \"ERROR: Generated file is too small ($SIZE bytes), likely contains placeholder content: $FILE\"\n"
        "  exit 1\n"
        "fi\n"
        "if grep -q \"HTML generation pending\" \"$FILE\"; then\n"
        "  echo \"ERROR: Generated file contains placeholder text 'HTML generation pending': $FILE\"\n"
        "  exit 1\n"
        "fi\n"
        "if ! grep -q \"<html\" \"$FILE\"; then\n"
        "  echo \"ERROR: Generated file does not contain valid HTML structure: $FILE\"\n"
        "  exit 1\n"
        "fi\n"
        "echo \"Content verification passed: file size $SIZE bytes, contains valid HTML\"\n"
    )

    # Use add_custom_command with OUTPUT so OBJECT_DEPENDS can properly track it
    # This ensures the command runs when mesh_web.c needs the file
    # Create a force file that changes every build to ensure regeneration
    # This file is touched at configure time to ensure the custom command runs every build
    set(FORCE_REGEN_FILE "${CMAKE_CURRENT_BINARY_DIR}/.force_html_regen")
    file(TOUCH "${FORCE_REGEN_FILE}")

    add_custom_command(
        OUTPUT "${GENERATED_HTML_HEADER}"
        # Pre-execution logging
        COMMAND ${CMAKE_COMMAND} -E echo "=== HTML GENERATION STARTING ==="
        COMMAND ${CMAKE_COMMAND} -E echo "[DEBUG] Custom command executing for OUTPUT: ${GENERATED_HTML_HEADER}"
        # Check if file exists and contains placeholder - if so, remove it
        # Otherwise, keep it for OBJECT_DEPENDS check (will be regenerated anyway)
        COMMAND ${CMAKE_COMMAND} -E echo "  -> Checking existing file..."
        COMMAND sh -c "if [ -f '${GENERATED_HTML_HEADER}' ] && grep -q 'HTML generation pending' '${GENERATED_HTML_HEADER}' 2>/dev/null; then ${CMAKE_COMMAND} -E remove '${GENERATED_HTML_HEADER}' && echo '  -> Removed placeholder file'; else echo '  -> File does not exist or is valid, will regenerate'; fi"
        # Pre-flight checks: Verify Python3 availability
        COMMAND ${CMAKE_COMMAND} -E echo "Checking prerequisites..."
        COMMAND ${CMAKE_COMMAND} -E env python3 --version || (echo "ERROR: Python3 is not available. Please install Python 3.x and ensure it's in PATH." && exit 1)
        # Pre-flight checks: Verify script and template exist
        COMMAND ${CMAKE_COMMAND} -E test -f "${GENERATE_HTML_SCRIPT}" || (echo "ERROR: Generate HTML script not found: ${GENERATE_HTML_SCRIPT}" && exit 1)
        COMMAND ${CMAKE_COMMAND} -E test -f "${EMBEDDED_TEMPLATE_FILE}" || (echo "ERROR: Template file not found: ${EMBEDDED_TEMPLATE_FILE}" && echo "ERROR: Template should be extracted from mesh_web.c by template extraction custom command." && echo "ERROR: Ensure template extraction custom command is registered and runs before HTML generation." && exit 1)
        # Ensure verify script is executable
        COMMAND ${CMAKE_COMMAND} -E chmod u+x "${VERIFY_SCRIPT}"
        # Generate HTML content
        COMMAND ${CMAKE_COMMAND} -E echo "Generating HTML from template and plugins..."
        COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8 PYTHONPATH="${CMAKE_SOURCE_DIR}/tools"
            python3 "${GENERATE_HTML_SCRIPT}" embedded "${EMBEDDED_TEMPLATE_FILE}" "${PLUGINS_DIR}" "${GENERATED_HTML_HEADER}"
            || (echo "ERROR: HTML generation script failed. Check script output above for details." && exit 1)
        # Verify file was created and contains actual content
        COMMAND ${CMAKE_COMMAND} -E echo "Verifying generated content..."
        COMMAND sh "${VERIFY_SCRIPT}" "${GENERATED_HTML_HEADER}"
            || (echo "ERROR: Content verification failed. Generated file may contain placeholder content." && exit 1)
        COMMAND ${CMAKE_COMMAND} -E echo "=== HTML GENERATION COMPLETED ==="
        DEPENDS ${HTML_GEN_DEPENDS} "${FORCE_REGEN_FILE}"
        COMMENT "Generating embedded HTML page with plugin content"
        VERBATIM
    )

    # Create a custom target with ALL to ensure it always runs
    # This target depends on the OUTPUT file, ensuring the custom command executes
    message(WARNING "  -> [DEBUG] Creating custom target 'generate_html_header' with ALL keyword")
    message(WARNING "  -> [DEBUG] GENERATED_HTML_HEADER path: ${GENERATED_HTML_HEADER}")
    message(WARNING "  -> [DEBUG] File exists before target creation: ${EXISTS "${GENERATED_HTML_HEADER}"}")
    if(EXISTS "${GENERATED_HTML_HEADER}")
        file(READ "${GENERATED_HTML_HEADER}" DEBUG_FILE_CONTENT LIMIT 200)
        message(WARNING "  -> [DEBUG] File content preview: ${DEBUG_FILE_CONTENT}")
    endif()
    add_custom_target(generate_html_header ALL DEPENDS "${GENERATED_HTML_HEADER}")
    message(WARNING "  -> [DEBUG] Custom target 'generate_html_header' created and added to default build")

    # Add explicit dependency from component target to custom target
    # This ensures generate_html_header runs before the component compiles
    message(WARNING "  -> [DEBUG] Attempting to find component target...")
    message(WARNING "  -> [DEBUG] COMPONENT_TARGET_NAME: ${COMPONENT_TARGET_NAME}")
    message(WARNING "  -> [DEBUG] COMPONENT_DIR_NAME: ${COMPONENT_DIR_NAME}")
    message(WARNING "  -> [DEBUG] CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")
    message(WARNING "  -> [DEBUG] CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")
    if(TARGET ${COMPONENT_TARGET_NAME})
        add_dependencies(${COMPONENT_TARGET_NAME} generate_html_header)
        message(WARNING "  -> [DEBUG] Added explicit dependency: ${COMPONENT_TARGET_NAME} depends on generate_html_header")
    else()
        message(WARNING "  -> [DEBUG] Component target '${COMPONENT_TARGET_NAME}' not found, trying alternative names...")
        # Try alternative target names
        if(TARGET "__idf_src")
            add_dependencies("__idf_src" generate_html_header)
            message(WARNING "  -> [DEBUG] Added explicit dependency: __idf_src depends on generate_html_header")
        elseif(TARGET "src")
            add_dependencies("src" generate_html_header)
            message(WARNING "  -> [DEBUG] Added explicit dependency: src depends on generate_html_header")
        else()
            message(WARNING "  -> [DEBUG] Could not find component target, relying on OBJECT_DEPENDS only")
            message(WARNING "  -> [DEBUG] This may cause build order issues - custom target may not run before compilation")
        endif()
    endif()

    # Add verification that the file exists and contains actual HTML content (not placeholder)
    # This post-build command runs after the custom target completes
    # Uses Python script for cross-platform content verification
    set(VERIFY_POSTBUILD_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/verify_html_postbuild.py")
    file(WRITE "${VERIFY_POSTBUILD_SCRIPT}"
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "import os\n"
        "import time\n"
        "from pathlib import Path\n"
        "file_path = sys.argv[1] if len(sys.argv) > 1 else None\n"
        "template_path = sys.argv[2] if len(sys.argv) > 2 else None\n"
        "script_path = sys.argv[3] if len(sys.argv) > 3 else None\n"
        "marker_path = sys.argv[4] if len(sys.argv) > 4 else None\n"
        "if not file_path or not os.path.exists(file_path):\n"
        "    print(f'ERROR: File does not exist: {file_path}', file=sys.stderr)\n"
        "    sys.exit(1)\n"
        "file_size = os.path.getsize(file_path)\n"
        "file_mtime = os.path.getmtime(file_path)\n"
        "file_mtime_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(file_mtime))\n"
        "print(f'File size: {file_size} bytes')\n"
        "print(f'File timestamp: {file_mtime_str}')\n"
        "if file_size < 1000:\n"
        "    print(f'ERROR: Generated file is too small ({file_size} bytes), likely contains placeholder content: {file_path}', file=sys.stderr)\n"
        "    sys.exit(1)\n"
        "if template_path and os.path.exists(template_path):\n"
        "    template_mtime = os.path.getmtime(template_path)\n"
        "    if file_mtime < template_mtime:\n"
        "        print(f'WARNING: Generated file is older than template (file: {file_mtime_str}, template: {time.strftime(\"%Y-%m-%d %H:%M:%S\", time.localtime(template_mtime))})', file=sys.stderr)\n"
        "        print(f'  -> File may be stale, regeneration may be needed', file=sys.stderr)\n"
        "if script_path and os.path.exists(script_path):\n"
        "    script_mtime = os.path.getmtime(script_path)\n"
        "    if file_mtime < script_mtime:\n"
        "        print(f'WARNING: Generated file is older than script (file: {file_mtime_str}, script: {time.strftime(\"%Y-%m-%d %H:%M:%S\", time.localtime(script_mtime))})', file=sys.stderr)\n"
        "        print(f'  -> File may be stale, regeneration may be needed', file=sys.stderr)\n"
        "if marker_path and os.path.exists(marker_path):\n"
        "    print(f'WARNING: Execution marker still exists: {marker_path}', file=sys.stderr)\n"
        "    print(f'  -> Custom command may not have completed successfully', file=sys.stderr)\n"
        "    sys.exit(1)\n"
        "with open(file_path, 'r', encoding='utf-8') as f:\n"
        "    content = f.read()\n"
        "if 'HTML generation pending' in content:\n"
        "    print(f'ERROR: Generated file contains placeholder text \\'HTML generation pending\\': {file_path}', file=sys.stderr)\n"
        "    sys.exit(1)\n"
        "if '<html' not in content:\n"
        "    print(f'ERROR: Generated file does not contain valid HTML structure: {file_path}', file=sys.stderr)\n"
        "    sys.exit(1)\n"
        "print(f'Content verification passed: file size {file_size} bytes, contains valid HTML')\n"
        "print(f'Placeholder detection: PASSED (no placeholder text found)')\n"
        "print(f'HTML structure validation: PASSED (contains <html tag)')\n"
    )

    add_custom_command(
        TARGET generate_html_header
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "=== Verifying generated_html.h content ==="
        COMMAND ${CMAKE_COMMAND} -E echo "File path: ${GENERATED_HTML_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E chmod u+x "${VERIFY_POSTBUILD_SCRIPT}"
        COMMAND python3 "${VERIFY_POSTBUILD_SCRIPT}" "${GENERATED_HTML_HEADER}" "${EMBEDDED_TEMPLATE_FILE}" "${GENERATE_HTML_SCRIPT}" "${EXECUTION_MARKER}"
            || (echo "ERROR: Content verification failed. Generated file may contain placeholder content." && exit 1)
        COMMAND ${CMAKE_COMMAND} -E echo "=== Verification successful: generated_html.h contains actual HTML content ==="
        COMMENT "Verifying generated HTML header file contains actual content"
    )

    message(STATUS "  -> Embedded HTML generation commands registered successfully")
endif()

# Step 3: Register external HTML generation custom command (if available)
if(EXISTS "${GENERATE_HTML_SCRIPT}" AND EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}")
    if(EXISTS "${PROJECT_ROOT}/lyktparad-server/web-ui")
        # Use WARNING level for critical command registration to ensure visibility in PlatformIO build logs
        message(WARNING "Registering external HTML generation custom command")
        # Use CMAKE_SOURCE_DIR for PYTHONPATH to ensure consistent path resolution
        add_custom_command(
            OUTPUT "${EXTERNAL_HTML_FILE}"
            COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8 PYTHONPATH="${CMAKE_SOURCE_DIR}/tools"
                python3 "${GENERATE_HTML_SCRIPT}" external "${EXTERNAL_TEMPLATE_FILE}" "${PLUGINS_DIR}" "${EXTERNAL_HTML_FILE}"
            DEPENDS ${EXTERNAL_HTML_GEN_DEPENDS}
            COMMENT "Generating external webserver index.html with plugin content"
            VERBATIM
        )
        message(STATUS "  -> External HTML generation command registered")
    endif()
endif()
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
message(WARNING "=== End HTML Generation Custom Commands Registration ===")

# Ensure generated_html.h is built before mesh_web.c compiles
# In ESP-IDF, we use OBJECT_DEPENDS to ensure the generated file exists before compilation
# The ALL keyword in add_custom_target ensures it builds automatically
# Additionally, we add an explicit dependency to ensure the custom target is built
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
message(WARNING "=== HTML Generation Dependency Setup ===")
if(EXISTS "${GENERATE_HTML_SCRIPT}" AND TARGET generate_html_header)
    message(STATUS "Setting up HTML generation dependencies")

    # Pre-build check: Fail if placeholder file exists (indicates HTML generation didn't run)
    if(EXISTS "${GENERATED_HTML_HEADER}")
        file(READ "${GENERATED_HTML_HEADER}" GENERATED_HTML_CONTENT)
        if(GENERATED_HTML_CONTENT MATCHES "HTML generation pending")
            message(FATAL_ERROR "ERROR: generated_html.h contains placeholder content! HTML generation did not run. Please clean the build directory and rebuild.")
        endif()
    endif()

    message(WARNING "  -> [DEBUG] Setting OBJECT_DEPENDS for mesh_web.c on ${GENERATED_HTML_HEADER}")
    message(WARNING "  -> [DEBUG] File exists when setting OBJECT_DEPENDS: ${EXISTS "${GENERATED_HTML_HEADER}"}")
    set_source_files_properties("mesh_web.c" PROPERTIES OBJECT_DEPENDS "${GENERATED_HTML_HEADER}")
    get_source_file_property(OBJ_DEPS "mesh_web.c" OBJECT_DEPENDS)
    message(WARNING "  -> [DEBUG] OBJECT_DEPENDS value after setting: ${OBJ_DEPS}")

    # OBJECT_DEPENDS should be sufficient when using add_custom_command with OUTPUT
    # The custom target with ALL ensures it runs, and OBJECT_DEPENDS ensures mesh_web.c waits

    # OBJECT_DEPENDS ensures mesh_web.c waits for generated_html.h to exist before compilation
    # The generate_html_header target with ALL keyword builds automatically, ensuring the custom command runs
    message(STATUS "  -> Custom target 'generate_html_header' is configured to build automatically (ALL keyword)")
    message(STATUS "  -> OBJECT_DEPENDS ensures mesh_web.c waits for generated_html.h")
    message(STATUS "  -> Custom command removes existing file to force regeneration (prevents placeholder issues)")

    # Add build verification function that will be called during build
    # This ensures the file exists before compilation proceeds
    # Note: This verification happens at build time, not configure time
    message(STATUS "  -> Build verification: generated_html.h must exist and contain actual HTML before mesh_web.c compiles")
    message(STATUS "  -> If file is missing or contains placeholder, build will fail with clear error message")
else()
    if(NOT EXISTS "${GENERATE_HTML_SCRIPT}")
        message(WARNING "Generate HTML script not found - HTML generation dependencies NOT set up")
        message(WARNING "  -> Script path: ${GENERATE_HTML_SCRIPT}")
    endif()
    if(NOT TARGET generate_html_header)
        message(WARNING "Custom target 'generate_html_header' not found - HTML generation dependencies NOT set up")
        message(WARNING "  -> This may indicate the custom target was not created")
    endif()
endif()
# Use WARNING level for section headers to ensure visibility in PlatformIO build logs
message(WARNING "=== End HTML Generation Dependency Setup ===")