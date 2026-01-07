#!/usr/bin/env python3
"""
Convert RGB-rainbow.csv to packed C array format for sequence plugin.

This script reads the RGB-rainbow.csv file and converts it to a packed format
matching the web UI's packGridData() algorithm:
- For each pair of squares (i, i+1):
  - byte0 = (r0 << 4) | g0
  - byte1 = (b0 << 4) | r1
  - byte2 = (g1 << 4) | b1

Output: C array declaration with 384 bytes (256 squares / 2 pairs × 3 bytes)
"""

import sys
import os

def parse_csv_line(line):
    """Parse a CSV line: index;r;g;b"""
    line = line.strip()
    if not line:
        return None
    parts = line.split(';')
    if len(parts) != 4:
        return None
    try:
        index = int(parts[0]) - 1  # Convert to 0-based
        r = int(parts[1])
        g = int(parts[2])
        b = int(parts[3])
        # Validate range (0-15 for 4-bit values)
        if not (0 <= r <= 15 and 0 <= g <= 15 and 0 <= b <= 15):
            print(f"Warning: Invalid color value in line: {line}", file=sys.stderr)
            return None
        return (index, r, g, b)
    except ValueError:
        return None

def pack_pair(r0, g0, b0, r1, g1, b1):
    """Pack two squares into 3 bytes using web UI algorithm"""
    byte0 = (r0 << 4) | g0
    byte1 = (b0 << 4) | r1
    byte2 = (g1 << 4) | b1
    return (byte0, byte1, byte2)

def main():
    # Default CSV path relative to script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    csv_path = os.path.join(script_dir, '..', 'docs', 'example-patterns', 'RGB-rainbow.csv')

    # Allow command-line override
    if len(sys.argv) > 1:
        csv_path = sys.argv[1]

    if not os.path.exists(csv_path):
        print(f"Error: CSV file not found: {csv_path}", file=sys.stderr)
        sys.exit(1)

    # Read and parse CSV
    colors = [None] * 256  # Pre-allocate for 256 squares
    with open(csv_path, 'r') as f:
        for line_num, line in enumerate(f, 1):
            parsed = parse_csv_line(line)
            if parsed is None:
                continue
            index, r, g, b = parsed
            if index < 0 or index >= 256:
                print(f"Warning: Index out of range in line {line_num}: {line}", file=sys.stderr)
                continue
            colors[index] = (r, g, b)

    # Verify all squares are filled
    missing = [i for i, c in enumerate(colors) if c is None]
    if missing:
        print(f"Error: Missing color data for squares: {missing[:10]}{'...' if len(missing) > 10 else ''}", file=sys.stderr)
        sys.exit(1)

    # Pack data: for each pair (i, i+1), pack into 3 bytes
    packed = []
    for i in range(0, 256, 2):
        r0, g0, b0 = colors[i]
        r1, g1, b1 = colors[i + 1]
        byte0, byte1, byte2 = pack_pair(r0, g0, b0, r1, g1, b1)
        packed.extend([byte0, byte1, byte2])

    # Verify packed size
    if len(packed) != 384:
        print(f"Error: Packed data size is {len(packed)}, expected 384", file=sys.stderr)
        sys.exit(1)

    # Generate C array
    print("/* Hardcoded RGB-rainbow default sequence data")
    print(" * Source: RGB-rainbow.csv")
    print(" * Format: Packed format, 2 squares per 3 bytes")
    print(" * Packing: byte0=(r0<<4)|g0, byte1=(b0<<4)|r1, byte2=(g1<<4)|b1")
    print(" * Size: 384 bytes (256 squares × 1.5 bytes per square)")
    print(" * Usage: Default sequence data loaded if no user data exists")
    print(" */")
    print("static const uint8_t sequence_default_rgb_rainbow[384] = {")

    # Format: 12 bytes per line (4 square pairs)
    for i in range(0, len(packed), 12):
        line_bytes = packed[i:i+12]
        hex_bytes = [f"0x{b:02X}" for b in line_bytes]
        comma = "," if i + 12 < len(packed) else ""
        print(f"    {', '.join(hex_bytes)}{comma}")

    print("};")

if __name__ == '__main__':
    main()
