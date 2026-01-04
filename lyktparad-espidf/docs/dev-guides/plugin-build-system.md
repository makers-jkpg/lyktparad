# Plugin Build System - Developer Guide

**Last Updated:** 2025-01-XX

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Plugin Directory Structure](#plugin-directory-structure)
4. [Build System Integration](#build-system-integration)
5. [File Embedding](#file-embedding)
6. [External Webserver Integration](#external-webserver-integration)
7. [Usage Examples](#usage-examples)
8. [Implementation Details](#implementation-details)

## Overview

### Purpose

The Plugin Build System provides automatic discovery, compilation, and integration of plugin files into the ESP32 firmware build. Plugins can include HTML, JavaScript, and CSS files that are automatically embedded into the firmware for the embedded webserver and copied to the external webserver.

### Key Features

- **Automatic Plugin Discovery**: Plugins are automatically discovered in `src/plugins/` without manual CMakeLists.txt modification
- **File Embedding**: HTML/JS/CSS files are converted to C string literals and embedded in firmware
- **External Server Integration**: Plugin files are automatically copied to the external webserver during build
- **Empty Directory Support**: Build system works correctly even with no plugins (empty plugins directory)

### Design Decisions

**Automatic Discovery**: Plugins are discovered automatically by scanning the `src/plugins/` directory. No manual CMakeLists.txt modification is required.

**File Embedding**: HTML/JS/CSS files are converted to C string literals at build time and embedded as static arrays in generated header files. This approach is simple and consistent with the existing embedded HTML approach.

**String Literal Embedding**: Files are embedded as C string literals (not binary embedding) for simplicity. This matches the existing approach used in `mesh_web.c` for the main HTML page.

**Build-Time File Copying**: Plugin files are copied to the external webserver during CMake configuration. This ensures files are available when the server runs.

## Architecture

### File Flow

```
┌─────────────────────────────────────────────────────────┐
│  Plugin Source Directory (src/plugins/<plugin-name>/)   │
│                                                          │
│  - <plugin-name>_plugin.h                               │
│  - <plugin-name>_plugin.c                               │
│  - <plugin-name>.html (or index.html)                   │
│  - js/<plugin-name>.js (optional)                       │
│  - css/<plugin-name>.css (optional)                     │
└──────────────────┬──────────────────────────────────────┘
                   │
                   │ Build System Discovery
                   ▼
┌─────────────────────────────────────────────────────────┐
│  CMake Plugin Discovery                                  │
│                                                          │
│  1. Scan src/plugins/ directory                         │
│  2. Find plugin subdirectories                          │
│  3. Validate plugin structure                           │
│  4. Collect source files                                │
│  5. Collect web files (HTML/JS/CSS)                     │
└──────────────────┬──────────────────────────────────────┘
                   │
                   ├─────────────────┬─────────────────────┐
                   ▼                 ▼                     ▼
        ┌──────────────────┐ ┌──────────────┐ ┌──────────────────┐
        │ Source Files     │ │ File         │ │ External         │
        │ Compilation      │ │ Embedding    │ │ Webserver        │
        │                  │ │              │ │ File Copying     │
        │ - Add to SRCS    │ │ - Convert to │ │                  │
        │ - Add to INCLUDE │ │   C strings  │ │ - Copy HTML/JS/  │
        │   _DIRS          │ │ - Generate   │ │   CSS files      │
        │                  │ │   headers    │ │ - Preserve dir   │
        └──────────────────┘ └──────────────┘ └──────────────────┘
                   │                 │                     │
                   └─────────────────┴─────────────────────┘
                                     │
                                     ▼
                          ┌──────────────────────┐
                          │  Firmware Build      │
                          │                      │
                          │  - Plugin .c files   │
                          │    compiled          │
                          │  - Generated headers │
                          │    included          │
                          └──────────────────────┘
```

### Build Process

1. **Configuration Phase**: CMake scans `src/plugins/` directory and discovers plugins
2. **Source Collection**: Plugin source files (`.c` and `.h`) are added to build
3. **File Embedding**: HTML/JS/CSS files are converted to C string headers using Python script
4. **File Copying**: Plugin web files are copied to external webserver directory
5. **Compilation**: Plugin source files are compiled with embedded headers
6. **Linking**: All plugin code is linked into the firmware

## Plugin Directory Structure

### Required Structure

Each plugin must be in its own subdirectory within `src/plugins/`:

```
src/plugins/
├── plugins.h                    # Central include file (created by build system)
└── <plugin-name>/               # Plugin directory (plugin name in lowercase with underscores)
    ├── <plugin-name>_plugin.h   # Plugin header (REQUIRED)
    ├── <plugin-name>_plugin.c   # Plugin implementation (REQUIRED)
    ├── <plugin-name>.html       # HTML content (optional, or index.html)
    ├── index.html               # Alternative HTML file name (optional)
    ├── js/                      # JavaScript directory (optional)
    │   └── <plugin-name>.js     # JavaScript file (optional)
    └── css/                     # CSS directory (optional)
        └── <plugin-name>.css    # CSS file (optional)
```

### File Naming Conventions

- **Plugin Name**: Lowercase with underscores (e.g., `effects`, `sequence`, `my_plugin`)
- **Header File**: `<plugin-name>_plugin.h` (required)
- **Source File**: `<plugin-name>_plugin.c` (required)
- **HTML File**: `<plugin-name>.html` or `index.html` (optional)
- **JavaScript File**: `js/<plugin-name>.js` (optional)
- **CSS File**: `css/<plugin-name>.css` (optional)

### Example Plugin Structure

```
src/plugins/
├── plugins.h
├── effects/
│   ├── effects_plugin.h
│   ├── effects_plugin.c
│   ├── effects.html
│   ├── js/
│   │   └── effects.js
│   └── css/
│       └── effects.css
└── sequence/
    ├── sequence_plugin.h
    ├── sequence_plugin.c
    ├── index.html
    ├── js/
    │   └── sequence.js
    └── css/
        └── sequence.css
```

## Build System Integration

### CMakeLists.txt Integration

The build system automatically integrates plugins via code in `src/CMakeLists.txt`:

```cmake
# Plugin system build integration
# Discover plugins in src/plugins/ directory and automatically include them in the build
set(PLUGINS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
set(PLUGIN_SOURCES "")
set(PLUGIN_INCLUDE_DIRS "")
set(PLUGIN_HTML_FILES "")
set(PLUGIN_JS_FILES "")
set(PLUGIN_CSS_FILES "")

if(EXISTS "${PLUGINS_DIR}" AND IS_DIRECTORY "${PLUGINS_DIR}")
    # Scan for plugins...
    # Collect source files...
    # Collect web files...
endif()

# Add plugin sources to build
list(APPEND ALL_SOURCES ${PLUGIN_SOURCES})
list(APPEND ALL_INCLUDE_DIRS ${PLUGIN_INCLUDE_DIRS})
```

### Plugin Discovery Process

1. **Directory Scanning**: CMake scans `src/plugins/` for subdirectories
2. **Validation**: Each subdirectory is checked for required files (`<plugin-name>_plugin.h` and `<plugin-name>_plugin.c`)
3. **Source Collection**: Valid plugins have their `.c` files added to `PLUGIN_SOURCES` and include directories added to `PLUGIN_INCLUDE_DIRS`
4. **Web File Collection**: Optional HTML/JS/CSS files are collected for embedding and copying

### Empty Directory Handling

The build system handles empty plugins directories gracefully:
- If `src/plugins/` doesn't exist, build continues normally
- If `src/plugins/` is empty, build continues normally
- No warnings or errors are generated for missing plugins directory

## File Embedding

### Overview

HTML, JavaScript, and CSS files are converted to C string literals at build time using a Python script (`src/plugins/convert_file_to_c_string.py`). The script generates header files containing static const char arrays with the file content.

### Conversion Process

1. **File Reading**: Source file is read as UTF-8
2. **String Escaping**: Special characters are escaped for C string literals:
   - Backslashes: `\` → `\\`
   - Double quotes: `"` → `\"`
   - Newlines: `\n` → `\\n`
   - Carriage returns: `\r` → `\\r`
   - Tabs: `\t` → `\\t`
3. **Header Generation**: Header file is generated with:
   - Header guards
   - Static const char array with escaped content
   - Size constant (excluding null terminator)

### Generated Header Format

For a plugin named `effects` with an HTML file:

**Input**: `src/plugins/effects/effects.html`

**Output**: `build/generated_plugin_headers/plugin_effects_html.h`

```c
/* Generated header file from effects.html
 *
 * This file was automatically generated. Do not edit manually.
 * Source: src/plugins/effects/effects.html
 *
 * Copyright (c) 2025 the_louie
 */

#ifndef PLUGIN_EFFECTS_HTML_H
#define PLUGIN_EFFECTS_HTML_H

#include <stddef.h>

/* Embedded HTML content as C string literal */
static const char plugin_effects_html[] = "<!DOCTYPE html>...";
static const size_t plugin_effects_html_size = sizeof(plugin_effects_html) - 1;  /* Exclude null terminator */

#endif /* PLUGIN_EFFECTS_HTML_H */
```

### CMake Custom Commands

File embedding uses CMake custom commands:

```cmake
add_custom_command(
    OUTPUT "${header_file}"
    COMMAND python3 "${CONVERT_SCRIPT}" "${html_file}" "${header_file}" "html"
    DEPENDS "${html_file}" "${CONVERT_SCRIPT}"
    COMMENT "Generating C string header from ${html_file}"
    VERBATIM
)
```

### Usage in Plugin Code

Plugins can include generated headers to access embedded content:

```c
#include "plugin_effects_html.h"

// Access HTML content
const char *html = plugin_effects_html;
size_t html_len = plugin_effects_html_size;
```

## External Webserver Integration

### File Copying

Plugin HTML/JS/CSS files are automatically copied to the external webserver during CMake configuration:

**Source**: `src/plugins/<plugin-name>/`
**Destination**: `lyktparad-server/web-ui/plugins/<plugin-name>/`

### Directory Structure Preservation

The directory structure is preserved during copying:
- HTML files are copied to `web-ui/plugins/<plugin-name>/`
- JS files are copied to `web-ui/plugins/<plugin-name>/js/`
- CSS files are copied to `web-ui/plugins/<plugin-name>/css/`

### Server Configuration

The external webserver serves plugin files from `/plugins/`:

```javascript
// Serve plugin files from web-ui/plugins/ at /plugins/
app.use('/plugins', express.static(path.join(__dirname, 'web-ui/plugins'), {
    // Serve plugin files with proper MIME types
    setHeaders: (res, filePath) => {
        if (filePath.endsWith('.js')) {
            res.setHeader('Content-Type', 'application/javascript');
        } else if (filePath.endsWith('.css')) {
            res.setHeader('Content-Type', 'text/css');
        }
    }
}));
```

### File Access URLs

Plugin files are accessible at:
- HTML: `/plugins/<plugin-name>/<plugin-name>.html` or `/plugins/<plugin-name>/index.html`
- JavaScript: `/plugins/<plugin-name>/js/<plugin-name>.js`
- CSS: `/plugins/<plugin-name>/css/<plugin-name>.css`

## Usage Examples

### Creating a New Plugin

1. **Create Plugin Directory**:
   ```bash
   mkdir -p src/plugins/my_plugin/js
   mkdir -p src/plugins/my_plugin/css
   ```

2. **Create Required Files**:
   - `src/plugins/my_plugin/my_plugin_plugin.h`
   - `src/plugins/my_plugin/my_plugin_plugin.c`

3. **Create Optional Web Files**:
   - `src/plugins/my_plugin/my_plugin.html` (or `index.html`)
   - `src/plugins/my_plugin/js/my_plugin.js` (optional)
   - `src/plugins/my_plugin/css/my_plugin.css` (optional)

4. **Build**: Plugins are automatically discovered and included

### Accessing Embedded Files

In plugin code:

```c
#include "plugin_my_plugin_html.h"
#include "plugin_my_plugin_js.h"
#include "plugin_my_plugin_css.h"

void my_plugin_get_html(const char **html, size_t *len) {
    *html = plugin_my_plugin_html;
    *len = plugin_my_plugin_html_size;
}
```

### Including Plugins

Plugins are included via `plugins.h`:

```c
#include "plugins/plugins.h"
```

Note: `plugins.h` is a placeholder that should be generated by CMake or manually updated to include plugin headers.

## Implementation Details

### Python Conversion Script

The file conversion script (`src/plugins/convert_file_to_c_string.py`) handles:
- UTF-8 file reading with error handling
- C string literal escaping
- Header guard generation
- Variable name generation from file paths
- Error handling and validation

### CMake Variables

The build system uses the following CMake variables:
- `PLUGINS_DIR`: Path to plugins directory (`src/plugins/`)
- `PLUGIN_SOURCES`: List of plugin `.c` source files
- `PLUGIN_INCLUDE_DIRS`: List of plugin include directories
- `PLUGIN_HTML_FILES`: List of plugin HTML files for embedding
- `PLUGIN_JS_FILES`: List of plugin JavaScript files for embedding
- `PLUGIN_CSS_FILES`: List of plugin CSS files for embedding
- `GENERATED_PLUGIN_DIR`: Directory for generated header files

### Build Dependencies

Generated header files are created before compilation:
- CMake custom commands generate headers as build outputs
- Headers are added to include directories
- Source files that include headers depend on generated headers

### Error Handling

The build system handles errors gracefully:
- Missing plugins directory: Build continues (no plugins)
- Invalid plugin structure: Warning logged, plugin skipped
- Missing required files: Warning logged, plugin skipped
- File conversion errors: Build fails with error message

### Limitations

- **File Size**: Large HTML/JS/CSS files increase firmware size
- **Memory Constraints**: Embedded files consume flash memory
- **Build Time**: File conversion adds build time (minimal)
- **Configure Time Copying**: External server files are copied at configure time, not build time (files must be present before build)
