#!/bin/bash
# Convert BMP to raw VGA Mode 13h format (320x200, 256 colors)

INPUT="$1"
OUTPUT="${2:-logo.raw}"

if [ -z "$INPUT" ]; then
    echo "Usage: $0 <input.bmp> [output.raw]"
    echo ""
    echo "Input must be a BMP file (any size)"
    echo "Output will be 320x200, 256 colors raw format for VGA Mode 13h"
    exit 1
fi

if [ ! -f "$INPUT" ]; then
    echo "Error: Input file '$INPUT' not found"
    exit 1
fi

# Check if ImageMagick is installed
if ! command -v convert &> /dev/null; then
    echo "Error: ImageMagick not installed. Install with:"
    echo "  sudo apt install imagemagick"
    exit 1
fi

echo "Converting '$INPUT' to VGA Mode 13h format..."

# Convert to 320x200, 256 colors, RAW format (just pixel data)
convert "$INPUT" \
    -resize 320x200! \
    -colors 256 \
    -depth 8 \
    -flip \
    GRAY:"$OUTPUT"

if [ $? -eq 0 ]; then
    SIZE=$(stat -f%z "$OUTPUT" 2>/dev/null || stat -c%s "$OUTPUT" 2>/dev/null)
    echo "✓ Converted successfully: $OUTPUT ($SIZE bytes)"
    echo ""
    echo "Expected size: 64000 bytes (320x200)"
    if [ "$SIZE" -ne 64000 ]; then
        echo "⚠ Warning: Size mismatch! File may not display correctly."
    fi
else
    echo "✗ Conversion failed"
    exit 1
fi
