#!/usr/bin/env python3
"""
HTML Generation Script for Plugin System

Generates HTML pages by combining a main template with plugin HTML/CSS/JS fragments.
Supports both embedded webserver (C string literal output) and external webserver (HTML file output).

Copyright (c) 2025 the_louie

This example code is in the Public Domain (or CC0 licensed, at your option.)

Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.
"""

import sys
import os
import argparse
import re
from pathlib import Path


def escape_c_string(content):
    """Escape content for C string literal."""
    # Escape backslashes first (before other escape sequences)
    content = content.replace('\\', '\\\\')
    # Escape double quotes
    content = content.replace('"', '\\"')
    # Escape newlines
    content = content.replace('\n', '\\n')
    # Escape carriage returns
    content = content.replace('\r', '\\r')
    # Escape tabs
    content = content.replace('\t', '\\t')
    # Check for null bytes (not allowed in C strings)
    if '\x00' in content:
        raise ValueError("Content contains null bytes, cannot be converted to C string")
    return content


def collect_plugin_files(plugins_dir):
    """
    Collect plugin HTML, CSS, and JS files from plugin directories.

    Returns a list of dictionaries, each containing:
    - name: plugin name
    - html_file: path to HTML file (or None)
    - css_file: path to CSS file (or None)
    - js_file: path to JS file (or None)
    """
    plugins = []

    if not os.path.exists(plugins_dir):
        return plugins

    # Scan plugin directories
    for item in os.listdir(plugins_dir):
        plugin_path = os.path.join(plugins_dir, item)
        if not os.path.isdir(plugin_path):
            continue

        plugin_name = item

        # Check for required plugin files
        plugin_c_file = os.path.join(plugin_path, f"{plugin_name}_plugin.c")
        plugin_h_file = os.path.join(plugin_path, f"{plugin_name}_plugin.h")

        if not os.path.exists(plugin_c_file) or not os.path.exists(plugin_h_file):
            continue

        # Find HTML file (either <plugin-name>.html or index.html)
        html_file = os.path.join(plugin_path, f"{plugin_name}.html")
        if not os.path.exists(html_file):
            html_file = os.path.join(plugin_path, "index.html")
            if not os.path.exists(html_file):
                html_file = None

        # Find CSS file
        css_file = os.path.join(plugin_path, "css", f"{plugin_name}.css")
        if not os.path.exists(css_file):
            css_file = None

        # Find JS file
        js_file = os.path.join(plugin_path, "js", f"{plugin_name}.js")
        if not os.path.exists(js_file):
            js_file = None

        plugins.append({
            'name': plugin_name,
            'html_file': html_file,
            'css_file': css_file,
            'js_file': js_file,
        })

    # Sort plugins alphabetically by name
    plugins.sort(key=lambda x: x['name'])

    return plugins


def read_file_safe(file_path):
    """Read a file, returning None if file doesn't exist."""
    if file_path is None or not os.path.exists(file_path):
        return None

    try:
        with open(file_path, 'rb') as f:
            content_bytes = f.read()

        # Decode as UTF-8, replacing invalid sequences
        try:
            content = content_bytes.decode('utf-8')
        except UnicodeDecodeError:
            content = content_bytes.decode('utf-8', errors='replace')

        return content
    except Exception as e:
        print(f"Warning: Failed to read {file_path}: {e}", file=sys.stderr)
        return None


def format_plugin_display_name(plugin_name):
    """
    Convert plugin directory name to display name.

    Examples:
    - "effects" -> "Effects"
    - "sequence" -> "Sequence"
    - "my_plugin" -> "My Plugin"
    """
    if not plugin_name:
        return ""

    # Replace underscores with spaces
    display_name = plugin_name.replace('_', ' ')

    # Capitalize first letter of each word
    words = display_name.split()
    display_name = ' '.join(word.capitalize() for word in words)

    return display_name


def generate_dropdown_html(plugins):
    """
    Generate dropdown HTML for plugin selection.

    Returns HTML string for <select> element with plugin options.
    """
    if not plugins:
        return ""

    options = []
    options.append('<option value="">Select Plugin...</option>')

    for plugin in plugins:
        display_name = format_plugin_display_name(plugin['name'])
        # Escape HTML special characters in plugin name
        plugin_name_escaped = plugin['name'].replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')
        display_name_escaped = display_name.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
        options.append(f'<option value="{plugin_name_escaped}">{display_name_escaped}</option>')

    # Return just the select element - container div is in template
    dropdown_html = f'''<select id="plugin-selector" class="plugin-selector" aria-label="Select plugin">
{chr(10).join(options)}
</select>'''

    return dropdown_html


def generate_layout_css():
    """
    Generate CSS for page layout with fixed header and plugin selection.

    Returns CSS string for:
    - .page-header (fixed, 150px height)
    - .page-title (title styling)
    - .plugin-dropdown-container (dropdown positioning)
    - .plugin-selector (dropdown styling)
    - .page-content (content area with margin-top)
    - .plugin-section (hidden by default)
    - .plugin-section.active (visible)
    - Responsive design
    """
    css = '''
/* Plugin Selection Dropdown Layout Styles */
.page-header {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    height: 150px;
    background-color: #2c3e50;
    color: white;
    padding: 20px;
    z-index: 1000;
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}

.page-header > .page-title {
    font-size: 1.5em;
    font-weight: bold;
    margin: 0;
    margin-bottom: 10px;
    flex: 0 1 auto;
}

.page-header > .connection-status {
    margin-top: 0;
    margin-left: 20px;
    flex: 0 0 auto;
}

.plugin-dropdown-container {
    display: flex;
    align-items: center;
    margin-left: auto;
    flex: 0 0 auto;
}

.plugin-selector {
    padding: 8px 12px;
    border: 2px solid #ddd;
    border-radius: 8px;
    font-size: 16px;
    background: white;
    color: #333;
    cursor: pointer;
    min-width: 180px;
}

.plugin-selector:hover {
    border-color: #667eea;
}

.plugin-selector:focus {
    outline: none;
    border-color: #667eea;
    box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
}

.page-content {
    margin-top: 150px;
    min-height: calc(100vh - 150px);
    padding: 20px;
    overflow-y: auto;
}

.plugin-section {
    display: none;
}

.plugin-section.active {
    display: block;
}

/* Responsive design */
@media (max-width: 768px) {
    .page-header {
        flex-direction: column;
        align-items: flex-start;
        height: auto;
        min-height: 150px;
        padding: 15px;
    }

    .page-header > .page-title {
        font-size: 1.2em;
        margin-bottom: 10px;
        flex: 1 1 100%;
    }

    .page-header > .connection-status {
        margin-left: 0;
        margin-top: 10px;
        width: 100%;
    }

    .plugin-dropdown-container {
        width: 100%;
        margin-left: 0;
        margin-top: 10px;
    }

    .plugin-selector {
        width: 100%;
    }

    .page-content {
        margin-top: 150px;
        min-height: calc(100vh - 150px);
    }
}

@media (max-width: 480px) {
    .page-header > .page-title {
        font-size: 1em;
    }

    .page-header {
        padding: 10px;
    }
}
'''
    return css


def generate_selection_js(plugins):
    """
    Generate JavaScript for plugin selection functionality.

    Returns JavaScript string that:
    - Initializes plugin selector on DOM ready
    - Handles dropdown change events
    - Shows/hides plugin sections
    - Persists selection in localStorage
    """
    if not plugins:
        return ""

    # Create list of plugin names for JavaScript
    plugin_names_js = ', '.join(f'"{plugin["name"].replace("\\", "\\\\").replace('"', '\\"')}"' for plugin in plugins)

    js = f'''
(function() {{
    'use strict';

    var plugins = [{plugin_names_js}];

    var PluginSelector = {{
        init: function() {{
            var selector = document.getElementById('plugin-selector');
            if (!selector) {{
                return;
            }}

            // Load saved selection from localStorage
            var savedSelection = null;
            try {{
                savedSelection = localStorage.getItem('selectedPlugin');
            }} catch (e) {{
                // localStorage not available, ignore
            }}

            // Set initial selection
            var initialSelection = savedSelection || (plugins.length > 0 ? plugins[0] : '');
            if (initialSelection && this.isValidPlugin(initialSelection)) {{
                selector.value = initialSelection;
                this.selectPlugin(initialSelection);
            }}

            // Add change event listener
            selector.addEventListener('change', function(e) {{
                var pluginName = e.target.value;
                PluginSelector.selectPlugin(pluginName);

                // Save to localStorage
                try {{
                    if (pluginName) {{
                        localStorage.setItem('selectedPlugin', pluginName);
                    }} else {{
                        localStorage.removeItem('selectedPlugin');
                    }}
                }} catch (e) {{
                    // localStorage not available, ignore
                }}
            }});
        }},

        isValidPlugin: function(pluginName) {{
            return plugins.indexOf(pluginName) !== -1;
        }},

        selectPlugin: function(pluginName) {{
            // Hide all plugin sections
            var sections = document.querySelectorAll('.plugin-section');
            sections.forEach(function(section) {{
                section.classList.remove('active');
                section.style.display = 'none';
            }});

            // Show selected plugin section
            // Note: pluginName is unescaped (from select value), but data-plugin-name
            // attribute may be HTML-escaped. Use getAttribute() which returns unescaped value.
            if (pluginName) {{
                for (var i = 0; i < sections.length; i++) {{
                    var section = sections[i];
                    var sectionPluginName = section.getAttribute('data-plugin-name');
                    if (sectionPluginName === pluginName) {{
                        section.classList.add('active');
                        section.style.display = 'block';
                        break;
                    }}
                }}
            }}
        }}
    }};

    // Initialize on DOM ready
    if (document.readyState === 'loading') {{
        document.addEventListener('DOMContentLoaded', function() {{
            PluginSelector.init();
        }});
    }} else {{
        PluginSelector.init();
    }}
}})();
'''
    return js


def generate_html_for_embedded(template_file, plugins_dir, output_file):
    """
    Generate HTML for embedded webserver (C string literal format).

    Reads template file, inserts plugin HTML/CSS/JS, and outputs as C string literal.
    """
    # Read template
    template_content = read_file_safe(template_file)
    if template_content is None:
        print(f"Error: Template file not found: {template_file}", file=sys.stderr)
        return 1

    # Collect plugin files
    plugins = collect_plugin_files(plugins_dir)

    # Generate dropdown HTML
    dropdown_html = generate_dropdown_html(plugins)

    # Generate layout CSS
    layout_css = generate_layout_css()

    # Generate selection JavaScript
    selection_js = generate_selection_js(plugins)

    # Collect plugin CSS and JS content
    plugin_css_content = []
    plugin_js_content = []
    plugin_html_sections = []

    for plugin in plugins:
        # Read plugin CSS
        if plugin['css_file']:
            css_content = read_file_safe(plugin['css_file'])
            if css_content:
                # Wrap in comment for identification
                plugin_css_content.append(f"\n/* Plugin: {plugin['name']} */\n{css_content}")

        # Read plugin JS
        if plugin['js_file']:
            js_content = read_file_safe(plugin['js_file'])
            if js_content:
                # Wrap in comment for identification
                plugin_js_content.append(f"\n/* Plugin: {plugin['name']} */\n{js_content}")

        # Read plugin HTML
        if plugin['html_file']:
            html_content = read_file_safe(plugin['html_file'])
            if html_content:
                # Wrap in section with plugin class and data attribute
                # Escape HTML special characters for attribute value
                plugin_name_escaped = plugin['name'].replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')
                plugin_html_sections.append(f'<section class="plugin-section plugin-{plugin["name"]}" data-plugin-name="{plugin_name_escaped}">{html_content}</section>')

    # Replace placeholders in template
    html_content = template_content

    # Replace {{PAGE_TITLE}} placeholder
    if '{{PAGE_TITLE}}' in html_content:
        html_content = html_content.replace('{{PAGE_TITLE}}', 'MAKERS JÖNKÖPING LJUSPARAD 2026')

    # Replace {{PLUGIN_DROPDOWN}} placeholder
    if '{{PLUGIN_DROPDOWN}}' in html_content:
        html_content = html_content.replace('{{PLUGIN_DROPDOWN}}', dropdown_html)

    # Replace {{PLUGIN_LAYOUT_CSS}} placeholder (insert in <style> tag)
    if '{{PLUGIN_LAYOUT_CSS}}' in html_content:
        html_content = html_content.replace('{{PLUGIN_LAYOUT_CSS}}', layout_css)
    elif layout_css:
        # If no placeholder, insert before </style>
        html_content = re.sub(r'(</style>)', layout_css + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_CSS}} placeholder (insert before </style>)
    if '{{PLUGIN_CSS}}' in html_content:
        css_insert = '\n'.join(plugin_css_content) if plugin_css_content else ''
        html_content = html_content.replace('{{PLUGIN_CSS}}', css_insert)
    elif plugin_css_content:
        # If no placeholder, insert before </style>
        css_insert = '\n'.join(plugin_css_content)
        html_content = re.sub(r'(</style>)', css_insert + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_HTML}} placeholder (insert before closing </div> of container, before </body>)
    if '{{PLUGIN_HTML}}' in html_content:
        html_insert = '\n'.join(plugin_html_sections) if plugin_html_sections else ''
        html_content = html_content.replace('{{PLUGIN_HTML}}', html_insert)
    elif plugin_html_sections:
        # If no placeholder, insert before </body> but after main content
        # Try to insert before </div> that closes container, or before </body>
        html_insert = '\n'.join(plugin_html_sections)
        # Insert before </body> (safer fallback)
        html_content = re.sub(r'(</body>)', html_insert + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_SELECTION_JS}} placeholder (insert before </script>)
    if '{{PLUGIN_SELECTION_JS}}' in html_content:
        html_content = html_content.replace('{{PLUGIN_SELECTION_JS}}', selection_js)
    elif selection_js:
        # If no placeholder, insert before </script>
        html_content = re.sub(r'(</script>)', selection_js + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_JS}} placeholder (insert before </script>)
    if '{{PLUGIN_JS}}' in html_content:
        js_insert = '\n'.join(plugin_js_content) if plugin_js_content else ''
        html_content = html_content.replace('{{PLUGIN_JS}}', js_insert)
    elif plugin_js_content:
        # If no placeholder, insert before </script>
        js_insert = '\n'.join(plugin_js_content)
        html_content = re.sub(r'(</script>)', js_insert + r'\n\1', html_content, count=1)

    # Escape for C string literal
    escaped_content = escape_c_string(html_content)

    # Generate C header file
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)

    header_guard = "GENERATED_HTML_PAGE_H"
    var_name = "html_page"

    header_content = f"""/* Generated HTML page file
 *
 * This file was automatically generated. Do not edit manually.
 * Template: {os.path.basename(template_file)}
 * Plugins directory: {os.path.basename(plugins_dir)}
 *
 * Copyright (c) 2025 the_louie
 */

#ifndef {header_guard}
#define {header_guard}

#include <stddef.h>

/* Embedded HTML page content as C string literal */
static const char {var_name}[] = "{escaped_content}";

#endif /* {header_guard} */
"""

    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(header_content)
        print(f"Generated: {output_file}")
        return 0
    except Exception as e:
        print(f"Error: Failed to write output file: {e}", file=sys.stderr)
        return 1


def generate_html_for_external(template_file, plugins_dir, output_file):
    """
    Generate HTML for external webserver (HTML file format).

    Reads template file, inserts plugin HTML/CSS/JS links, and outputs as HTML file.
    """
    # Read template
    template_content = read_file_safe(template_file)
    if template_content is None:
        print(f"Error: Template file not found: {template_file}", file=sys.stderr)
        return 1

    # Collect plugin files
    plugins = collect_plugin_files(plugins_dir)

    # Generate dropdown HTML
    dropdown_html = generate_dropdown_html(plugins)

    # Generate layout CSS
    layout_css = generate_layout_css()

    # Generate selection JavaScript
    selection_js = generate_selection_js(plugins)

    # Generate plugin CSS links
    plugin_css_links = []
    for plugin in plugins:
        if plugin['css_file']:
            # Generate relative path from web-ui root
            css_path = f"/plugins/{plugin['name']}/css/{plugin['name']}.css"
            plugin_css_links.append(f'    <link rel="stylesheet" href="{css_path}">')

    # Generate plugin JS script tags
    plugin_js_scripts = []
    for plugin in plugins:
        if plugin['js_file']:
            # Generate relative path from web-ui root
            js_path = f"/plugins/{plugin['name']}/js/{plugin['name']}.js"
            plugin_js_scripts.append(f'    <script src="{js_path}"></script>')

    # Generate plugin HTML sections (with links to plugin HTML files, or inline if needed)
    # For external webserver, we can reference plugin HTML files directly, or inline them
    # For simplicity, we'll inline plugin HTML sections
    plugin_html_sections = []
    for plugin in plugins:
        if plugin['html_file']:
            html_content = read_file_safe(plugin['html_file'])
            if html_content:
                # Add data-plugin-name attribute
                # Escape HTML special characters for attribute value
                plugin_name_escaped = plugin['name'].replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')
                plugin_html_sections.append(f'    <section class="plugin-section plugin-{plugin["name"]}" data-plugin-name="{plugin_name_escaped}">{html_content}</section>')

    # Replace placeholders in template
    html_content = template_content

    # Replace {{PAGE_TITLE}} placeholder
    if '{{PAGE_TITLE}}' in html_content:
        html_content = html_content.replace('{{PAGE_TITLE}}', 'MAKERS JÖNKÖPING LJUSPARAD 2026')

    # Replace {{PLUGIN_DROPDOWN}} placeholder
    if '{{PLUGIN_DROPDOWN}}' in html_content:
        html_content = html_content.replace('{{PLUGIN_DROPDOWN}}', dropdown_html)

    # Replace {{PLUGIN_LAYOUT_CSS}} placeholder (insert in <head> or <style> tag)
    if '{{PLUGIN_LAYOUT_CSS}}' in html_content:
        html_content = html_content.replace('{{PLUGIN_LAYOUT_CSS}}', layout_css)
    elif layout_css:
        # If no placeholder, try to insert in <head> as <style> tag
        if '</head>' in html_content:
            html_content = re.sub(r'(</head>)', f'    <style>{layout_css}\n    </style>\n\\1', html_content, count=1)
        elif '</style>' in html_content:
            html_content = re.sub(r'(</style>)', layout_css + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_CSS}} placeholder (insert in <head> after main CSS)
    if '{{PLUGIN_CSS}}' in html_content:
        css_insert = '\n'.join(plugin_css_links) if plugin_css_links else ''
        html_content = html_content.replace('{{PLUGIN_CSS}}', css_insert)
    elif plugin_css_links:
        # If no placeholder, insert before </head>
        css_insert = '\n'.join(plugin_css_links)
        html_content = re.sub(r'(</head>)', css_insert + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_HTML}} placeholder
    if '{{PLUGIN_HTML}}' in html_content:
        html_insert = '\n'.join(plugin_html_sections) if plugin_html_sections else ''
        html_content = html_content.replace('{{PLUGIN_HTML}}', html_insert)
    elif plugin_html_sections:
        # If no placeholder, insert before </body> but after main content
        html_insert = '\n'.join(plugin_html_sections)
        html_content = re.sub(r'(</body>)', html_insert + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_SELECTION_JS}} placeholder (insert before </body>)
    if '{{PLUGIN_SELECTION_JS}}' in html_content:
        html_content = html_content.replace('{{PLUGIN_SELECTION_JS}}', selection_js)
    elif selection_js:
        # If no placeholder, insert as <script> tag before </body>
        if '</body>' in html_content:
            html_content = re.sub(r'(</body>)', f'    <script>{selection_js}\n    </script>\n\\1', html_content, count=1)
        elif '</script>' in html_content:
            html_content = re.sub(r'(</script>)', selection_js + r'\n\1', html_content, count=1)

    # Replace {{PLUGIN_JS}} placeholder (insert before </body>)
    if '{{PLUGIN_JS}}' in html_content:
        js_insert = '\n'.join(plugin_js_scripts) if plugin_js_scripts else ''
        html_content = html_content.replace('{{PLUGIN_JS}}', js_insert)
    elif plugin_js_scripts:
        # If no placeholder, insert before </body>
        js_insert = '\n'.join(plugin_js_scripts)
        html_content = re.sub(r'(</body>)', js_insert + r'\n\1', html_content, count=1)

    # Write HTML file
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)

    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(html_content)
        print(f"Generated: {output_file}")
        return 0
    except Exception as e:
        print(f"Error: Failed to write output file: {e}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(
        description='Generate HTML pages by combining template with plugin HTML/CSS/JS'
    )
    parser.add_argument(
        'mode',
        choices=['embedded', 'external'],
        help='Generation mode: embedded (C string literal) or external (HTML file)'
    )
    parser.add_argument(
        'template',
        help='Path to HTML template file'
    )
    parser.add_argument(
        'plugins_dir',
        help='Path to plugins directory'
    )
    parser.add_argument(
        'output',
        help='Path to output file'
    )

    args = parser.parse_args()

    if args.mode == 'embedded':
        return generate_html_for_embedded(args.template, args.plugins_dir, args.output)
    else:  # external
        return generate_html_for_external(args.template, args.plugins_dir, args.output)


if __name__ == '__main__':
    sys.exit(main())
