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
                # Wrap in section with plugin class
                plugin_html_sections.append(f'<section class="plugin-section plugin-{plugin["name"]}">{html_content}</section>')

    # Replace placeholders in template
    html_content = template_content

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
                plugin_html_sections.append(f'    <section class="plugin-section plugin-{plugin["name"]}">{html_content}</section>')

    # Replace placeholders in template
    html_content = template_content

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
