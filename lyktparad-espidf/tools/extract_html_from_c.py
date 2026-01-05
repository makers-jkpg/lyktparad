#!/usr/bin/env python3
"""
Extract HTML from mesh_web.c and create template file with placeholders.

This script reads the html_page[] string literal from mesh_web.c,
extracts the HTML content, un-escapes C string literals, and creates
a template file with plugin placeholders.
"""

import re
import sys

def unescape_c_string(content):
    """Un-escape C string literal content."""
    # Replace escaped sequences in reverse order of escape_c_string
    content = content.replace('\\\\', '\\')  # Must be first
    content = content.replace('\\"', '"')
    content = content.replace('\\n', '\n')
    content = content.replace('\\r', '\r')
    content = content.replace('\\t', '\t')
    return content

def extract_html_from_c_file(c_file_path):
    """Extract HTML content from mesh_web.c html_page[] string literal."""
    with open(c_file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    # Find start of html_page[] (line 40, 0-indexed is 39)
    # Find end of html_page[] (line 1590, 0-indexed is 1589)
    start_idx = 39  # Line 40 in 1-indexed
    end_idx = 1589  # Line 1590 in 1-indexed

    html_parts = []

    # Extract lines 40-1590 (1-indexed, so 39-1589 in 0-indexed)
    for i in range(start_idx, min(end_idx + 1, len(lines))):
        line = lines[i]

        # Extract content between quotes
        # Handle lines like: "content"
        # or: "content"; (last line)
        matches = re.findall(r'"([^"]*)"', line)
        for match in matches:
            html_parts.append(match)

    # Join all parts
    html_content = ''.join(html_parts)

    # Un-escape C string
    html_content = unescape_c_string(html_content)

    return html_content

def add_plugin_placeholders(html_content):
    """Add plugin placeholders to HTML template."""
    # Find </style> and add {{PLUGIN_CSS}} before it
    html_content = re.sub(
        r'(</style>)',
        r'{{PLUGIN_CSS}}\n\1',
        html_content,
        count=1
    )

    # Find </body> and add {{PLUGIN_HTML}} before it (but after </div> closing container)
    # Insert before </body> but after the closing </div> of container
    html_content = re.sub(
        r'(</body>)',
        r'{{PLUGIN_HTML}}\n\1',
        html_content,
        count=1
    )

    # Find </script> and add {{PLUGIN_JS}} before it
    html_content = re.sub(
        r'(</script>)',
        r'{{PLUGIN_JS}}\n\1',
        html_content,
        count=1
    )

    return html_content

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: extract_html_from_c.py <input_c_file> <output_html_file>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    html_content = extract_html_from_c_file(input_file)
    html_content = add_plugin_placeholders(html_content)

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(html_content)

    print(f"Extracted HTML template to {output_file}")
