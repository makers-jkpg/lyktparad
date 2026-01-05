#!/usr/bin/env python3
"""
Extract HTML template from mesh_web.c

This script extracts the HTML content from mesh_web.c's html_page[] string literal
and converts it to a proper HTML template file with plugin placeholders.
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

    # Find start of html_page[] (line 39, 0-indexed is 38)
    start_line_idx = 38  # 0-indexed
    # Find end (look for </html>"; pattern, around line 1593)
    end_line_idx = None
    for i in range(start_line_idx, len(lines)):
        line = lines[i]
        # Check if line contains </html> and ends with ";
        if '</html>' in line and line.strip().endswith('";'):
            end_line_idx = i + 1  # Include this line
            break

    if end_line_idx is None:
        # Fallback: search for pattern
        for i in range(start_line_idx, min(start_line_idx + 2000, len(lines))):
            if '</html>' in lines[i]:
                end_line_idx = i + 1
                break

    if end_line_idx is None:
        print(f"Error: Could not find end of html_page[]", file=sys.stderr)
        return None

    # Extract HTML content lines (lines 40-1593, 1-indexed)
    html_lines = lines[start_line_idx:end_line_idx]

    # Process each line to extract content between quotes
    html_content = []
    for line in html_lines:
        # Remove leading/trailing whitespace
        stripped = line.strip()
        # Skip empty lines
        if not stripped:
            continue

        # Extract content between quotes
        # Pattern: "content" or "content";
        # Handle escaped quotes in content
        if stripped.startswith('"'):
            # Find the closing quote (may be escaped)
            content_start = 1
            content_end = -1
            if stripped.endswith('";'):
                content_end = -2
            elif stripped.endswith('"'):
                content_end = -1

            if content_end != -1:
                content = stripped[content_start:content_end]
                # Un-escape the content
                content = unescape_c_string(content)
                html_content.append(content)
            else:
                # Try regex to extract quoted content
                matches = re.findall(r'"([^"]*(?:\\.[^"]*)*)"', stripped)
                for match in matches:
                    content = unescape_c_string(match)
                    html_content.append(content)
        else:
            # Try to extract any quoted content from the line
            matches = re.findall(r'"([^"]*(?:\\.[^"]*)*)"', stripped)
            for match in matches:
                content = unescape_c_string(match)
                html_content.append(content)

    # Join all content
    full_html = ''.join(html_content)

    return full_html

def main():
    if len(sys.argv) < 3:
        print("Usage: extract_html_template.py <input_c_file> <output_template_file>", file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    html_content = extract_html_from_c_file(input_file)
    if html_content is None:
        sys.exit(1)

    # Write template file
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(html_content)

    print(f"Extracted HTML template to: {output_file}")
    print(f"HTML size: {len(html_content)} bytes")

if __name__ == '__main__':
    main()
