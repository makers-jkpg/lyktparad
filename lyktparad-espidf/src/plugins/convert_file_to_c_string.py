#!/usr/bin/env python3
"""
File to C String Converter

Converts a file (HTML, JS, CSS) into a C string literal header file.
The generated header contains a const char array with the file content.

Copyright (c) 2025 the_louie

This example code is in the Public Domain (or CC0 licensed, at your option.)

Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.
"""

import sys
import os
import re

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
        raise ValueError("File contains null bytes, cannot be converted to C string")
    return content

def generate_header_guard_name(file_path, file_type):
    """Generate header guard name from file path and type."""
    # Extract plugin name from path (e.g., plugins/effects/effects.html -> PLUGIN_EFFECTS_HTML)
    basename = os.path.basename(file_path)
    dirname = os.path.dirname(file_path)
    plugin_name = os.path.basename(dirname)

    # Convert to uppercase and replace special characters with underscores
    plugin_name_upper = re.sub(r'[^a-zA-Z0-9]', '_', plugin_name).upper()
    file_type_upper = file_type.upper()

    return f"PLUGIN_{plugin_name_upper}_{file_type_upper}_H"

def generate_variable_name(file_path, file_type):
    """Generate variable name from file path and type."""
    basename = os.path.basename(file_path)
    dirname = os.path.dirname(file_path)
    plugin_name = os.path.basename(dirname)

    # Convert to lowercase and replace special characters with underscores
    plugin_name_lower = re.sub(r'[^a-zA-Z0-9]', '_', plugin_name).lower()
    file_type_lower = file_type.lower()

    # Handle index.html specially
    if basename == "index.html":
        return f"plugin_{plugin_name_lower}_html"
    else:
        # Remove extension from basename
        basename_noext = os.path.splitext(basename)[0]
        basename_lower = re.sub(r'[^a-zA-Z0-9]', '_', basename_noext).lower()
        return f"plugin_{basename_lower}_{file_type_lower}"

def convert_file_to_c_string(input_file, output_file, file_type):
    """
    Convert a file to a C string literal header file.

    Args:
        input_file: Path to input file (HTML, JS, or CSS)
        output_file: Path to output header file
        file_type: Type of file ("html", "js", or "css")
    """
    try:
        # Read input file as binary, then decode as UTF-8
        with open(input_file, 'rb') as f:
            content_bytes = f.read()

        # Decode as UTF-8, replacing invalid sequences
        try:
            content = content_bytes.decode('utf-8')
        except UnicodeDecodeError:
            # Try to decode with error handling
            content = content_bytes.decode('utf-8', errors='replace')

        # Escape content for C string
        escaped_content = escape_c_string(content)

        # Generate header guard and variable names
        header_guard = generate_header_guard_name(input_file, file_type)
        var_name = generate_variable_name(input_file, file_type)
        size_var_name = f"{var_name}_size"

        # Generate header file content with inline array definition
        header_content = f"""/* Generated header file from {os.path.basename(input_file)}
 *
 * This file was automatically generated. Do not edit manually.
 * Source: {input_file}
 *
 * Copyright (c) 2025 the_louie
 */

#ifndef {header_guard}
#define {header_guard}

#include <stddef.h>

/* Embedded {file_type.upper()} content as C string literal */
static const char {var_name}[] = "{escaped_content}";
static const size_t {size_var_name} = sizeof({var_name}) - 1;  /* Exclude null terminator */

#endif /* {header_guard} */
"""

        # Write header file
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(header_content)

        print(f"Generated: {output_file}")
        return 0

    except FileNotFoundError:
        print(f"Error: Input file not found: {input_file}", file=sys.stderr)
        return 1
    except PermissionError:
        print(f"Error: Permission denied: {input_file}", file=sys.stderr)
        return 1
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: Unexpected error: {e}", file=sys.stderr)
        return 1

def main():
    if len(sys.argv) != 4:
        print("Usage: convert_file_to_c_string.py <input_file> <output_file> <file_type>", file=sys.stderr)
        print("  file_type: html, js, or css", file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    file_type = sys.argv[3].lower()

    if file_type not in ['html', 'js', 'css']:
        print(f"Error: Invalid file_type '{file_type}'. Must be 'html', 'js', or 'css'", file=sys.stderr)
        sys.exit(1)

    # Create output directory if it doesn't exist
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)

    sys.exit(convert_file_to_c_string(input_file, output_file, file_type))

if __name__ == '__main__':
    main()
