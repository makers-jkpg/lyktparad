#!/usr/bin/env python3
"""
Remove all HTML string literals from mesh_web.c between the comment
and the API comment.
"""

import re
import sys

def remove_html_strings(input_file, output_file):
    """Remove all string literal lines between the html_page comment and API comment."""
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    # Find the start (after "/* The html_page variable is defined in generated_html.h */")
    # and end (before "/* API: GET /api/nodes - Returns number of nodes in mesh */")
    start_idx = None
    end_idx = None

    for i, line in enumerate(lines):
        if '/* The html_page variable is defined in generated_html.h */' in line:
            start_idx = i + 1  # Start removing from next line
        elif start_idx is not None and '/* API: GET /api/nodes - Returns number of nodes in mesh */' in line:
            end_idx = i  # Stop before this line
            break

    if start_idx is None or end_idx is None:
        print(f"Error: Could not find start or end markers", file=sys.stderr)
        print(f"start_idx={start_idx}, end_idx={end_idx}", file=sys.stderr)
        return False

    # Remove all lines that are string literals (starting with " and ending with " or ";
    # between start_idx and end_idx
    new_lines = lines[:start_idx]

    # Check each line between start_idx and end_idx
    for i in range(start_idx, end_idx):
        line = lines[i]
        stripped = line.strip()
        # Skip lines that are string literals (starting with " and ending with " or ";
        if not (stripped.startswith('"') and (stripped.endswith('"') or stripped.endswith('";'))):
            new_lines.append(line)

    new_lines.extend(lines[end_idx:])

    with open(output_file, 'w', encoding='utf-8') as f:
        f.writelines(new_lines)

    removed_count = sum(1 for i in range(start_idx, end_idx)
                       if lines[i].strip().startswith('"') and
                       (lines[i].strip().endswith('"') or lines[i].strip().endswith('";')))
    print(f"Removed {removed_count} string literal lines (from line {start_idx+1} to {end_idx})")

    return True

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: remove_html_strings.py <input_file> <output_file>", file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    if remove_html_strings(input_file, output_file):
        print(f"Successfully processed {input_file} -> {output_file}")
    else:
        sys.exit(1)
