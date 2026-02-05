#!/bin/bash
# Convert BMP to raw VGA Mode 13h format (320x200, 256 colors)

INPUT="$1"
OUTPUT="${2:-logo.raw}"
PALETTE_OUTPUT="${OUTPUT%.raw}.pal"
TMP_BMP="$(mktemp --suffix=.bmp)"

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

# Convert to 320x200, 256 colors, 8-bit paletted BMP (uncompressed)
convert "$INPUT" \
    -resize 320x200! \
    -colors 256 \
    -depth 8 \
    -type palette \
    -compress none \
    -define bmp:compression=none \
    BMP3:"$TMP_BMP"

if [ $? -ne 0 ]; then
    echo "✗ Conversion failed"
    exit 1
fi

# Extract raw pixel data and 256-color palette (VGA DAC format)
python3 - "$TMP_BMP" "$OUTPUT" "$PALETTE_OUTPUT" <<'PY'
import struct
import sys

bmp_path, out_raw, out_pal = sys.argv[1:4]
data = open(bmp_path, "rb").read()

if data[0:2] != b"BM":
    raise SystemExit("Not a BMP file")

offset = struct.unpack_from("<I", data, 10)[0]
dib_size = struct.unpack_from("<I", data, 14)[0]
width = struct.unpack_from("<i", data, 18)[0]
height = struct.unpack_from("<i", data, 22)[0]
bpp = struct.unpack_from("<H", data, 28)[0]

if bpp != 8:
    raise SystemExit(f"Expected 8bpp BMP, got {bpp}")

palette_bytes = offset - 14 - dib_size
palette = data[14 + dib_size : 14 + dib_size + palette_bytes]
entries = palette_bytes // 4

pal_out = bytearray()
for i in range(entries):
    b, g, r, _ = palette[i * 4 : i * 4 + 4]
    pal_out += bytes((r >> 2, g >> 2, b >> 2))

pal_out = pal_out.ljust(256 * 3, b"\x00")

row_size = ((width + 3) // 4) * 4
h = abs(height)
pixels = data[offset : offset + row_size * h]

out = bytearray()
if height > 0:
    for y in range(h - 1, -1, -1):
        out += pixels[y * row_size : y * row_size + width]
else:
    for y in range(h):
        out += pixels[y * row_size : y * row_size + width]

open(out_raw, "wb").write(out)
open(out_pal, "wb").write(pal_out)
PY

rm -f "$TMP_BMP"

SIZE=$(stat -f%z "$OUTPUT" 2>/dev/null || stat -c%s "$OUTPUT" 2>/dev/null)
PSIZE=$(stat -f%z "$PALETTE_OUTPUT" 2>/dev/null || stat -c%s "$PALETTE_OUTPUT" 2>/dev/null)
echo "✓ Converted successfully: $OUTPUT ($SIZE bytes)"
echo "✓ Palette saved: $PALETTE_OUTPUT ($PSIZE bytes)"
echo ""
echo "Expected sizes: 64000 bytes (logo), 768 bytes (palette)"
if [ "$SIZE" -ne 64000 ] || [ "$PSIZE" -ne 768 ]; then
    echo "⚠ Warning: Size mismatch! Files may not display correctly."
fi
