# Sequence Mode User Guide

**Last Updated:** 2025-01-15

## Table of Contents

1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Features Overview](#features-overview)
4. [Step-by-Step Usage](#step-by-step-usage)
5. [Examples](#examples)
6. [Creating Sequences with External Tools](#creating-sequences-with-external-tools)
7. [Troubleshooting](#troubleshooting)
8. [FAQ](#faq)

## Introduction

Sequence mode allows you to create synchronized color sequences that play across all nodes in your mesh network. You can design patterns using a 16x16 grid (up to 256 squares), set the playback tempo, and synchronize the sequence to all connected nodes. The sequence will loop continuously, with all nodes playing in perfect synchronization.

> **For Developers**: See [Developer Guide](../dev-guides/mode-sequence.md) for technical implementation details.

### What Can Sequence Mode Do?

- Create animated color patterns using a visual grid interface
- Control playback tempo (speed) from 10ms to 2550ms per step
- Use sequences of variable length (1 to 16 rows, 16 to 256 squares)
- Import and export sequences as CSV files for sharing and backup
- Synchronize sequences across all nodes in the mesh network
- Visual feedback showing which square is currently playing

## Getting Started

### Prerequisites

Before using sequence mode, ensure:

1. **Mesh network is running** - All nodes must be connected to the mesh network
2. **Root node is accessible** - You need access to the root node's web interface
3. **LED hardware is connected** - At least one node should have LED hardware connected

### Accessing the Sequence Interface

1. Connect to your root node's IP address in a web browser
2. The sequence grid interface is displayed on the main page
3. You'll see a 16x16 grid with row and column labels

### Initial State

When you first access the interface:

- **Grid**: Shows 4 rows by default (64 squares visible)
- **Tempo**: Set to 250ms (25 steps per second)
- **All squares**: Start with black color (RGB: 0, 0, 0)
- **Row count selector**: Set to 4 rows

## Features Overview

### Grid Interface

The sequence grid is a 16x16 layout that can display up to 256 squares:

- **Variable rows**: You can show 1 to 16 rows (16 to 256 squares)
- **Dynamic sizing**: Grid automatically resizes to show only visible rows
- **Row labels**: Left side shows row numbers (0-15) or letters (A-F for rows 10-15)
- **Column labels**: Top shows column numbers (0-9) or letters (A-F for columns 10-15)
- **Z label**: Top-left corner for whole-grid operations

### Color Operations

You can set colors in several ways:

1. **Individual square**: Click any square, then pick a color
2. **Entire row**: Click a row label (0-15 or A-F), then pick a color
3. **Entire column**: Click a column label (0-9 or A-F), then pick a color
4. **Whole grid**: Click the Z label, then pick a color

**Color quantization**: Colors are quantized to 16 levels (0-15) per channel for memory efficiency. The color picker shows the full range, but values are automatically converted to 4-bit (16 levels).

### Tempo Control

- **Range**: 10ms to 2550ms per step
- **Step size**: 10ms increments
- **Input field**: Enter tempo value directly or use number input controls
- **Display**: Shows current tempo in milliseconds
- **Effect**: Lower values = faster playback, higher values = slower playback

### Row Count Selector

- **Range**: 1 to 16 rows
- **Default**: 4 rows
- **Effect**: Changes how many rows are visible and included in the sequence
- **Grid resizing**: Grid automatically resizes to show only selected rows

### CSV Import/Export

**Export**:
- Click "Export Sequence" button
- CSV data appears in a text area
- Copy the data to save or share
- Format: `index;RED;GREEN;BLUE` (one line per square)

**Import**:
- Click "Import Sequence" button
- Paste CSV data into the text area
- Click "Import" to load the sequence
- Row count is automatically detected from imported data
- Format must match: `index;RED;GREEN;BLUE` with 4-bit values (0-15)

**Example files**: See `docs/example-patterns/` for example CSV files:
- `police.csv` - Simple alternating pattern
- `SOS.csv` - Morse code SOS pattern
- `RGB-rainbow.csv` - Full rainbow gradient

### Sequence Synchronization

**Sync button**:
- Sends current grid data, tempo, and row count to all nodes
- Sequence automatically starts playing after sync
- All nodes receive the same sequence data
- Synchronization happens via mesh network

**What gets synchronized**:
- All color data (for visible rows)
- Tempo/rhythm value
- Sequence length (number of rows)

### Current Square Indicator

During sequence playback:

- **Visual feedback**: A border highlights the currently playing square
- **Backend sync**: Indicator syncs with backend every row (every 16 squares, every `tempo * 16` milliseconds)
- **Purpose**: Shows which square is currently playing on the backend

### Sequence Playback

- **Automatic start**: Sequence starts playing immediately after sync
- **Looping**: Sequence loops continuously from start to end
- **Synchronization**: All nodes play in sync via BEAT messages
- **Independent playback**: Nodes continue playing even if BEAT messages are lost

## Step-by-Step Usage

### Creating a Simple Pattern

1. **Set row count**: Use the row count selector to choose how many rows you want (e.g., 4 rows)
2. **Set colors**: Click individual squares and pick colors, or use row/column operations
3. **Set tempo**: Enter desired tempo in milliseconds (e.g., 250ms)
4. **Sync**: Click the "Sync" button to send sequence to all nodes
5. **Observe**: Watch the sequence play with the current square indicator moving

### Using Row Operations

1. **Select row**: Click a row label (e.g., row 0)
2. **Pick color**: Use the color picker to choose a color
3. **Apply**: The entire row is filled with that color
4. **Repeat**: Click other row labels to set different colors per row

### Using Column Operations

1. **Select column**: Click a column label (e.g., column 0)
2. **Pick color**: Use the color picker to choose a color
3. **Apply**: The entire column is filled with that color

### Using Whole Grid Operation

1. **Select all**: Click the Z label (top-left corner)
2. **Pick color**: Use the color picker to choose a color
3. **Apply**: All visible squares are filled with that color

### Importing a Sequence

1. **Click Import**: Click the "Import Sequence" button
2. **Paste data**: Paste CSV data into the text area
3. **Import**: Click the "Import" button
4. **Verify**: Check that the grid shows the imported pattern
5. **Sync**: Click "Sync" to send to nodes

### Exporting a Sequence

1. **Click Export**: Click the "Export Sequence" button
2. **Copy data**: Select and copy the CSV data from the text area
3. **Save**: Paste into a text file or save for later use

## Examples

### Example 1: Simple Alternating Pattern (Police Style)

This creates an alternating blue/black pattern similar to police lights:

1. Set row count to 1 (16 squares)
2. Click row label 0
3. Set color to black (RGB: 0, 0, 0)
4. Click squares 1, 3, 5, 7 (odd positions)
5. Set those squares to cyan/blue (RGB: 0, 8, 15)
6. Set tempo to 250ms
7. Sync to nodes

**CSV format** (first 8 squares):
```
1;0;8;15
2;0;0;0
3;0;8;15
4;0;0;0
5;0;8;15
6;0;0;0
7;0;8;15
8;0;0;0
```

See `docs/example-patterns/police.csv` for the complete pattern.

### Example 2: Morse Code Pattern (SOS)

This creates an SOS pattern in Morse code (short-short-short, long-long-long, short-short-short):

1. Set row count to 2 (32 squares)
2. Create pattern with white squares (RGB: 15, 15, 15) for "on" and black (RGB: 0, 0, 0) for "off"
3. Pattern: 3 short, pause, 3 long, pause, 3 short
4. Set tempo to 200ms for readable Morse code
5. Sync to nodes

See `docs/example-patterns/SOS.csv` for the complete pattern.

### Example 3: Rainbow Gradient

This creates a smooth rainbow gradient across the grid:

1. Set row count to 16 (256 squares)
2. Use the import function
3. Import `docs/example-patterns/RGB-rainbow.csv`
4. Set tempo to 100ms for smooth animation
5. Sync to nodes

See `docs/example-patterns/RGB-rainbow.csv` for the complete pattern.

## Creating Sequences with External Tools

While the web interface is great for creating sequences interactively, you can also create and edit sequences using external tools like Excel or Python. This is especially useful for:
- Creating complex patterns programmatically
- Generating mathematical patterns (gradients, waves, etc.)
- Batch editing multiple sequences
- Creating sequences with precise color calculations

### Editing Sequences in Excel

Excel (or other spreadsheet software) can be used to edit CSV files directly:

**Steps:**

1. **Export existing sequence** (optional):
   - Use the web interface to export a sequence
   - Save the CSV data to a file (e.g., `my-sequence.csv`)

2. **Open in Excel**:
   - Open Excel (or LibreOffice Calc, Google Sheets, etc.)
   - Import the CSV file, making sure to:
     - Select "Semicolon" as the delimiter
     - Ensure the file has 4 columns: `index`, `RED`, `GREEN`, `BLUE`

3. **Edit the sequence**:
   - Column A: Index (1-256, must be sequential)
   - Column B: RED value (0-15)
   - Column C: GREEN value (0-15)
   - Column D: BLUE value (0-15)
   - You can use Excel formulas to generate patterns:
     - `=MOD(ROW(),2)*15` - Alternating pattern
     - `=ROUND(SIN(ROW()/10)*7.5+7.5,0)` - Sine wave pattern
     - `=MIN(15,ROW()*0.5)` - Gradient pattern

4. **Save as CSV**:
   - Save the file as CSV format
   - Make sure semicolons (`;`) are used as delimiters
   - Verify the format matches: `index;RED;GREEN;BLUE`

5. **Import back**:
   - Copy the CSV content
   - Use the web interface Import function to load the sequence

**Tips:**
- Always verify RGB values are between 0-15
- Ensure index values are sequential (1, 2, 3, ...)
- Check that semicolons are used, not commas
- Remove any header row if Excel added one

### Generating Sequences with Python

Python is excellent for programmatically generating complex patterns. Here are some examples:

#### Basic Template

```python
def generate_sequence(num_rows=16):
    """Generate a sequence CSV string."""
    lines = []
    for row in range(num_rows):
        for col in range(16):
            index = row * 16 + col + 1  # 1-based index
            # Calculate RGB values (0-15)
            r, g, b = calculate_color(row, col, index)
            lines.append(f"{index};{r};{g};{b}")
    return "\n".join(lines)

def calculate_color(row, col, index):
    """Calculate RGB values for a square."""
    # Your pattern logic here
    return (0, 0, 0)  # Default: black

# Generate and save
csv_content = generate_sequence(16)
with open("my-pattern.csv", "w") as f:
    f.write(csv_content)
```

#### Example 1: Rainbow Gradient

```python
def rainbow_gradient(num_rows=16):
    """Generate a smooth rainbow gradient."""
    lines = []
    total_squares = num_rows * 16
    
    for i in range(total_squares):
        index = i + 1
        # Map position to hue (0-360 degrees)
        hue = (i / total_squares) * 360
        
        # Convert HSV to RGB (simplified, 4-bit output)
        # Using a simple approximation
        h = hue / 60
        c = 15  # Maximum saturation
        x = int(c * (1 - abs((h % 2) - 1)))
        
        if h < 1:
            r, g, b = c, x, 0
        elif h < 2:
            r, g, b = x, c, 0
        elif h < 3:
            r, g, b = 0, c, x
        elif h < 4:
            r, g, b = 0, x, c
        elif h < 5:
            r, g, b = x, 0, c
        else:
            r, g, b = c, 0, x
        
        lines.append(f"{index};{r};{g};{b}")
    
    return "\n".join(lines)

# Generate rainbow
csv = rainbow_gradient(16)
with open("rainbow.csv", "w") as f:
    f.write(csv)
```

#### Example 2: Alternating Pattern

```python
def alternating_pattern(num_rows=1, color1=(0, 8, 15), color2=(0, 0, 0)):
    """Generate alternating pattern (police light style)."""
    lines = []
    for i in range(num_rows * 16):
        index = i + 1
        if i % 2 == 0:  # Even positions
            r, g, b = color1
        else:  # Odd positions
            r, g, b = color2
        lines.append(f"{index};{r};{g};{b}")
    return "\n".join(lines)

# Generate police pattern
csv = alternating_pattern(1, (0, 8, 15), (0, 0, 0))
with open("police.csv", "w") as f:
    f.write(csv)
```

#### Example 3: Sine Wave Pattern

```python
import math

def sine_wave_pattern(num_rows=4, frequency=2, amplitude=7.5, offset=7.5):
    """Generate sine wave pattern."""
    lines = []
    for row in range(num_rows):
        for col in range(16):
            index = row * 16 + col + 1
            # Calculate sine wave value
            position = row * 16 + col
            value = int(amplitude * math.sin(frequency * position * math.pi / 16) + offset)
            # Clamp to 0-15
            value = max(0, min(15, value))
            # Use as intensity for all channels (grayscale)
            lines.append(f"{index};{value};{value};{value}")
    return "\n".join(lines)

# Generate sine wave
csv = sine_wave_pattern(4, frequency=2, amplitude=7.5, offset=7.5)
with open("sine-wave.csv", "w") as f:
    f.write(csv)
```

#### Example 4: Spiral Pattern

```python
import math

def spiral_pattern(num_rows=8):
    """Generate spiral pattern."""
    lines = []
    center_row = num_rows / 2
    center_col = 8
    
    for row in range(num_rows):
        for col in range(16):
            index = row * 16 + col + 1
            # Calculate distance from center
            dx = col - center_col
            dy = row - center_row
            distance = math.sqrt(dx*dx + dy*dy)
            angle = math.atan2(dy, dx)
            
            # Create spiral effect
            spiral_value = int((distance + angle * 2) * 2) % 16
            r = spiral_value
            g = (spiral_value + 5) % 16
            b = (spiral_value + 10) % 16
            
            lines.append(f"{index};{r};{g};{b}")
    return "\n".join(lines)

# Generate spiral
csv = spiral_pattern(8)
with open("spiral.csv", "w") as f:
    f.write(csv)
```

#### Example 5: Loading and Modifying Existing Sequences

```python
def load_sequence(filename):
    """Load a sequence from CSV file."""
    sequence = []
    with open(filename, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(";")
            if len(parts) == 4:
                index, r, g, b = map(int, parts)
                sequence.append((index, r, g, b))
    return sequence

def modify_sequence(sequence, brightness_factor=1.0):
    """Modify sequence brightness."""
    modified = []
    for index, r, g, b in sequence:
        # Adjust brightness
        r = int(min(15, r * brightness_factor))
        g = int(min(15, g * brightness_factor))
        b = int(min(15, b * brightness_factor))
        modified.append((index, r, g, b))
    return modified

def save_sequence(sequence, filename):
    """Save sequence to CSV file."""
    lines = [f"{index};{r};{g};{b}" for index, r, g, b in sequence]
    with open(filename, "w") as f:
        f.write("\n".join(lines))

# Load, modify, and save
seq = load_sequence("rainbow.csv")
seq = modify_sequence(seq, brightness_factor=0.8)  # Reduce brightness
save_sequence(seq, "rainbow-dimmed.csv")
```

**Python Tips:**
- Always clamp RGB values to 0-15 range
- Use `int()` to convert float calculations to integers
- Index values must be 1-based and sequential
- Use semicolons (`;`) as delimiters, not commas
- Test with small sequences first (1-2 rows) before generating large ones

**Importing Python-Generated Sequences:**
1. Run your Python script to generate the CSV file
2. Open the CSV file in a text editor
3. Copy the entire content
4. Use the web interface Import function to load it
5. Verify the pattern displays correctly
6. Sync to nodes

## Troubleshooting

### Sequence Not Playing

**Symptoms**: Sequence syncs but LEDs don't change

**Solutions**:
1. Check that nodes are connected to mesh network
2. Verify LED hardware is properly connected
3. Check serial logs for error messages
4. Try syncing again
5. Verify tempo value is valid (10-2550ms)

### Colors Not Displaying Correctly

**Symptoms**: Colors appear different than expected

**Solutions**:
1. Remember that colors are quantized to 16 levels (0-15 per channel)
2. Color picker shows full range, but values are converted to 4-bit
3. Check that you're using values 0-15 in CSV imports
4. Verify LED hardware is working with other modes (RGB mode)

### Import Fails

**Symptoms**: Import button shows error message

**Solutions**:
1. Check CSV format: must be `index;RED;GREEN;BLUE`
2. Verify index values are 1-256
3. Verify RGB values are 0-15 (4-bit)
4. Check for empty lines or invalid characters
5. Ensure all lines have exactly 4 columns separated by semicolons

### Grid Not Updating

**Symptoms**: Grid doesn't show changes after row count change

**Solutions**:
1. Check that row count selector is set correctly (1-16)
2. Try changing row count again
3. Refresh the page if grid appears stuck
4. Verify JavaScript is enabled in browser

### Current Square Indicator Not Moving

**Symptoms**: Border doesn't move during playback

**Solutions**:
1. Check that sequence is actually playing (LEDs should be changing)
2. Verify backend is responding (check browser console for errors)
3. Try syncing again to restart the indicator
4. Check that tempo value is reasonable (not too fast to see)

### Nodes Not Synchronized

**Symptoms**: Different nodes show different colors at the same time

**Solutions**:
1. Verify all nodes received the sequence (check serial logs)
2. Check mesh network connectivity
3. Try syncing again
4. BEAT messages help maintain sync - check that root node is sending BEATs
5. Some drift is normal, but should correct every row

## FAQ

### What is the maximum sequence length?

The maximum sequence length is **16 rows**, which equals **256 squares** (16 rows × 16 columns).

### What is the minimum sequence length?

The minimum sequence length is **1 row**, which equals **16 squares**.

### What is the minimum/maximum tempo?

- **Minimum tempo**: 10ms per step
- **Maximum tempo**: 2550ms per step
- **Step size**: 10ms increments

### How many nodes can be synchronized?

All nodes in the mesh network can be synchronized. There's no hard limit, but network performance may degrade with very large networks.

### Can I use sequences shorter than 16 rows?

Yes! You can use sequences from 1 to 16 rows. Use the row count selector to choose how many rows are active. The grid will automatically resize to show only the selected rows.

### What is the CSV format?

The CSV format is: `index;RED;GREEN;BLUE`

- **index**: Square index (1-256, where 1 = row 0, col 0)
- **RED**: Red value (0-15, 4-bit)
- **GREEN**: Green value (0-15, 4-bit)
- **BLUE**: Blue value (0-15, 4-bit)

Example: `1;15;0;0` means square 1 (first square) with red=15, green=0, blue=0 (bright red).

### How does the current square indicator work?

The current square indicator:
- Syncs with the backend every row (every 16 squares, every `tempo * 16` milliseconds)
- Shows a border around the currently playing square from the backend
- Automatically starts when sequence is synced
- Updates periodically to show the backend's current position

### Why are colors quantized to 16 levels?

Colors are quantized to 4-bit values (16 levels per channel) for memory efficiency. This allows storing 256 squares in only 384 bytes instead of 768 bytes, which is important for embedded systems with limited memory.

### Can I stop a sequence?

Currently, sequences start automatically after sync and loop continuously. To stop, you can send an RGB command or sync a new sequence.

### What happens if a node loses mesh connection?

If a node loses connection:
- It will continue playing the last received sequence independently
- It will resynchronize when connection is restored and receives a new BEAT message
- The sequence timer continues running locally

### How do I share sequences with others?

Use the Export function to get CSV data, then share the CSV file. Others can import it using the Import function.

### Where are the example patterns?

Example patterns are located in `docs/example-patterns/`:
- `police.csv` - Simple alternating pattern
- `SOS.csv` - Morse code SOS pattern  
- `RGB-rainbow.csv` - Full rainbow gradient
