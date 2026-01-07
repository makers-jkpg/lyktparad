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
3. **File Copying**: Plugin HTML/JS/CSS files are copied to external webserver directory
4. **Compilation**: Plugin source files are compiled
5. **Linking**: All plugin code is linked into the firmware

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

## HTML Page Generation

### Overview

The build system generates the main HTML page by combining a template file with plugin HTML, CSS, and JavaScript fragments. This process is handled by `tools/generate_html.py`, which supports both embedded (C string literal) and external (HTML file) output modes.

### Generation Process

1. **Template Reading**: The script reads the template file (embedded or external)
2. **Plugin Discovery**: Plugins are discovered from the plugins directory
3. **Plugin Name Formatting**: Directory names are formatted for display (e.g., "effects" → "Effects")
4. **Dropdown Generation**: A dropdown menu is generated listing all available plugins
5. **CSS Generation**: Layout CSS is generated for the page header, content area, and plugin visibility
6. **JavaScript Generation**: Selection JavaScript is generated for dropdown functionality and localStorage persistence
7. **Plugin HTML Wrapping**: Each plugin's HTML is wrapped in a `<section>` tag with `data-plugin-name` attribute
8. **Placeholder Replacement**: Template placeholders are replaced with generated content

### Template Placeholders

The template file supports the following placeholders:

- `{{PAGE_TITLE}}`: Replaced with "MAKERS JÖNKÖPING LJUSPARAD 2026"
- `{{PLUGIN_DROPDOWN}}`: Replaced with dropdown HTML
- `{{PLUGIN_LAYOUT_CSS}}`: Replaced with layout CSS for page structure
- `{{PLUGIN_SELECTION_JS}}`: Replaced with JavaScript for plugin selection
- `{{PLUGIN_HTML}}`: Replaced with plugin HTML sections
- `{{PLUGIN_CSS}}`: Replaced with plugin CSS (embedded) or CSS links (external)
- `{{PLUGIN_JS}}`: Replaced with plugin JavaScript (embedded) or script tags (external)

### Plugin HTML Wrapping

Each plugin's HTML is automatically wrapped in a section tag:

```html
<section class="plugin-section plugin-{plugin_name}" data-plugin-name="{plugin_name}">
    <!-- Plugin HTML content here -->
</section>
```

The `data-plugin-name` attribute is used by JavaScript to identify and show/hide plugin sections.

### Dropdown Generation

The dropdown is generated with:
- A placeholder option: "Select Plugin..."
- An option for each plugin with:
  - `value`: Plugin directory name (e.g., "effects")
  - Display text: Formatted plugin name (e.g., "Effects")

Plugin names are formatted by:
1. Replacing underscores with spaces
2. Capitalizing each word

### Layout CSS Generation

The generated CSS includes:
- `.page-header`: Fixed header (150px height) at top of page
- `.page-title`: Title styling
- `.plugin-dropdown-container`: Dropdown positioning
- `.plugin-selector`: Dropdown styling
- `.page-content`: Content area with margin-top to account for fixed header
- `.plugin-section`: Hidden by default (`display: none`)
- `.plugin-section.active`: Visible when selected (`display: block`)
- Responsive design media queries for mobile devices

### Selection JavaScript Generation

The generated JavaScript includes:
- Plugin list array with all plugin names
- `PluginSelector` object with:
  - `init()`: Initializes dropdown and loads saved selection from localStorage
  - `isValidPlugin()`: Validates plugin name
  - `selectPlugin()`: Shows selected plugin, hides others
- Event listener for dropdown changes
- localStorage persistence for selection
- DOM ready initialization

### Build Integration

HTML generation is integrated into the CMake build system:

**Embedded Mode**:
- Template: `templates/embedded_template.html`
- Output: `build/generated_html.h` (C string literal)
- Used by: ESP32 embedded webserver

**External Mode**:
- Template: `lyktparad-server/web-ui/index.html`
- Output: `lyktparad-server/web-ui/index.html` (overwrites template)
- Used by: Node.js external webserver

Both modes use the same generation logic, ensuring consistency between embedded and external webservers.

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