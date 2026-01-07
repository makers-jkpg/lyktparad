# Plugin Build System - Developer Guide

**Last Updated:** 2026-01-04

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Plugin Directory Structure](#plugin-directory-structure)
4. [Build System Integration](#build-system-integration)
5. [Embedded Webserver HTML](#embedded-webserver-html)
6. [External Webserver Integration](#external-webserver-integration)
7. [Usage Examples](#usage-examples)
8. [Implementation Details](#implementation-details)

## Overview

### Purpose

The Plugin Build System provides automatic discovery, compilation, and integration of plugin files into the ESP32 firmware build. Plugins can include HTML, JavaScript, and CSS files that are automatically copied to the external webserver. Plugin source files (`.c` and `.h`) are compiled and embedded in the firmware.

### Key Features

- **Automatic Plugin Discovery**: Plugins are automatically discovered in `src/plugins/` without manual CMakeLists.txt modification
- **Source File Compilation**: Plugin `.c` and `.h` files are automatically compiled and linked into firmware
- **External Server Integration**: Plugin HTML/JS/CSS files are automatically copied to the external webserver during build
- **Empty Directory Support**: Build system works correctly even with no plugins (empty plugins directory)

### Design Decisions

**Automatic Discovery**: Plugins are discovered automatically by scanning the `src/plugins/` directory. No manual CMakeLists.txt modification is required.

**Source File Compilation**: Plugin `.c` and `.h` files are automatically compiled and linked into the firmware binary.

**External Server File Copying**: Plugin HTML/JS/CSS files are copied to the external webserver during CMake configuration. These files are NOT embedded in firmware - they are only served by the external webserver.

**Embedded Webserver**: The embedded webserver uses a simple static HTML page embedded directly in `mesh_web.c`. This page provides basic plugin control (selection, play, pause, rewind) but does not serve plugin-specific HTML files.

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
        ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
        │ Source Files     │ │ Embedded         │ │ External         │
        │ Compilation      │ │ Webserver HTML   │ │ Webserver        │
        │                  │ │                  │ │ File Copying     │
        │ - Add to SRCS    │ │ - Static HTML    │ │                  │
        │ - Add to INCLUDE │ │   in mesh_web.c  │ │ - Copy HTML/JS/  │
        │   _DIRS          │ │ - Simple plugin  │ │   CSS files      │
        │                  │ │   control UI     │ │ - Preserve dir   │
        └──────────────────┘ └──────────────────┘ └──────────────────┘
                   │                 │                     │
                   └─────────────────┴─────────────────────┘
                                     │
                                     ▼
                          ┌──────────────────────┐
                          │  Firmware Build      │
                          │                      │
                          │  - Plugin .c files   │
                          │    compiled          │
                          │  - Static HTML       │
                          │    embedded          │
                          └──────────────────────┘
```

### Build Process

1. **Configuration Phase**: CMake scans `src/plugins/` directory and discovers plugins
2. **Source Collection**: Plugin source files (`.c` and `.h`) are added to build
3. **File Copying**: Plugin HTML/JS/CSS files are copied to external webserver directory (NOT embedded in firmware)
4. **Compilation**: Plugin source files are compiled
5. **Linking**: All plugin code is linked into the firmware

**Note**: Plugin HTML/JS/CSS files are NOT embedded in firmware. They are only copied to the external webserver for serving. The embedded webserver uses a simple static HTML page defined directly in `mesh_web.c`.

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
├── effect_strobe/
│   ├── effect_strobe_plugin.h
│   └── effect_strobe_plugin.c
├── effect_fade/
│   ├── effect_fade_plugin.h
│   └── effect_fade_plugin.c
├── sequence/
│   ├── sequence_plugin.h
│   ├── sequence_plugin.c
│   ├── sequence.html
│   ├── js/
│   │   └── sequence.js
│   └── css/
│       └── sequence.css
└── my_plugin/
    ├── my_plugin_plugin.h
    ├── my_plugin_plugin.c
    ├── index.html
    ├── js/
    │   └── my_plugin.js
    └── css/
        └── my_plugin.css
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
4. **Web File Collection**: Optional HTML/JS/CSS files are collected for copying to external webserver

### Empty Directory Handling

The build system handles empty plugins directories gracefully:
- If `src/plugins/` doesn't exist, build continues normally
- If `src/plugins/` is empty, build continues normally
- No warnings or errors are generated for missing plugins directory

## Embedded Webserver HTML

### Overview

The embedded webserver on the root node serves a simple static HTML page that is embedded directly in `mesh_web.c` as a C string literal. This page provides basic plugin control functionality: plugin selection, play, pause, and rewind buttons.

### HTML Page Structure

The HTML page is defined as a static const char array in `mesh_web.c`:

```c
static const char simple_html_page[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
// ... HTML content ...
"</html>";
```

### Features

The embedded HTML page includes:
- **Plugin Selection Dropdown**: Lists all available plugins
- **Control Buttons**: Play (activate), Pause, and Rewind (reset)
- **Active Plugin Display**: Shows which plugin is currently active
- **Status Messages**: Success/error feedback for operations
- **Auto-refresh**: Active plugin status is polled every 5 seconds

### API Integration

The HTML page uses JavaScript to interact with the plugin API endpoints:
- `GET /api/plugins` - Load plugin list
- `GET /api/plugin/active` - Get active plugin
- `POST /api/plugin/activate` - Activate plugin
- `POST /api/plugin/pause` - Pause plugin
- `POST /api/plugin/reset` - Reset plugin

### Limitations

- The embedded webserver does NOT serve plugin-specific HTML files
- Plugin HTML/JS/CSS files are only served by the external webserver
- The embedded page provides basic control only, not full plugin interfaces

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

### Accessing Plugin Files

Plugin HTML/JS/CSS files are served by the external webserver, not embedded in firmware. To access them:

**From Web Browser:**
- HTML: `http://<server-ip>/plugins/<plugin-name>/<plugin-name>.html`
- JavaScript: `http://<server-ip>/plugins/<plugin-name>/js/<plugin-name>.js`
- CSS: `http://<server-ip>/plugins/<plugin-name>/css/<plugin-name>.css`

**Note**: Plugin files are NOT accessible from the embedded webserver. The embedded webserver only serves the simple plugin control page.

### Including Plugins

Plugins are included via `plugins.h`:

```c
#include "plugins/plugins.h"
```

Note: `plugins.h` is a placeholder that should be generated by CMake or manually updated to include plugin headers.

## Implementation Details

### File Copying Process

Plugin HTML/JS/CSS files are copied to the external webserver during CMake configuration:
- Files are copied using `configure_file()` with `COPYONLY` option
- Directory structure is preserved
- Files are copied to `lyktparad-server/web-ui/plugins/<plugin-name>/`

### CMake Variables

The build system uses the following CMake variables:
- `PLUGINS_DIR`: Path to plugins directory (`src/plugins/`)
- `PLUGIN_SOURCES`: List of plugin `.c` source files
- `PLUGIN_INCLUDE_DIRS`: List of plugin include directories
- `PLUGIN_HTML_FILES`: List of plugin HTML files (for external server copying)
- `PLUGIN_JS_FILES`: List of plugin JavaScript files (for external server copying)
- `PLUGIN_CSS_FILES`: List of plugin CSS files (for external server copying)

### Build Dependencies

Plugin source files are compiled as part of the normal build process:
- Plugin `.c` files are added to `ALL_SOURCES`
- Plugin include directories are added to `ALL_INCLUDE_DIRS`
- No special build dependencies are required

### Error Handling

The build system handles errors gracefully:
- Missing plugins directory: Build continues (no plugins)
- Invalid plugin structure: Warning logged, plugin skipped
- Missing required files: Warning logged, plugin skipped
- File copying errors: Warning logged, build continues

### Limitations

- **External Server Only**: Plugin HTML/JS/CSS files are only served by the external webserver, not the embedded webserver
- **Configure Time Copying**: External server files are copied at configure time, not build time (files must be present before build)
- **No Firmware Embedding**: Plugin web files are NOT embedded in firmware, reducing firmware size but requiring external server for plugin interfaces