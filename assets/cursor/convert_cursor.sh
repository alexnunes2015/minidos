#!/bin/bash
# Convert a cursor image into a MiniDOS runtime bitmap header.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
INPUT="${1:-}"
OUTPUT="${2:-$ROOT_DIR/external_apps/runtime/minidos_cursor_bitmap.h}"
TMP_TXT="$(mktemp)"

if [ -z "$INPUT" ]; then
    echo "Usage: $0 <input_image> [output_header]"
    echo ""
    echo "Input can be BMP, PNG, or any ImageMagick-supported image."
    echo "The image keeps its original size and is converted to a 3-state cursor bitmap:"
    echo "  transparent = alpha < 50% or key color #FF0000"
    echo "  outline     = dark pixels"
    echo "  fill        = light pixels"
    echo ""
    echo "PNG com alpha é o formato recomendado para transparência real."
    exit 1
fi

if [ ! -f "$INPUT" ]; then
    echo "Error: Input file '$INPUT' not found"
    exit 1
fi

if ! command -v convert >/dev/null 2>&1; then
    echo "Error: ImageMagick 'convert' not found. Install with:"
    echo "  sudo apt install imagemagick"
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"

echo "Converting cursor bitmap '$INPUT'..."
convert "$INPUT" \
    -background none \
    -alpha on \
    -filter point \
    TXT:"$TMP_TXT"

python3 - "$TMP_TXT" "$OUTPUT" <<'PY'
import re
import sys

src_path, out_path = sys.argv[1:3]
header_re = re.compile(r"^# ImageMagick pixel enumeration: (\d+),(\d+),")
line_re = re.compile(r"^(\d+),(\d+):\s+.*#([0-9A-Fa-f]{6,8})")
width = 0
height = 0
pixels = None

with open(src_path, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        if pixels is None:
            header = header_re.match(line.strip())
            if header:
                width = int(header.group(1))
                height = int(header.group(2))
                pixels = [0] * (width * height)
                continue

        m = line_re.match(line.strip())
        if not m:
            continue

        if pixels is None:
            raise SystemExit("Could not determine image size from ImageMagick output")

        x = int(m.group(1))
        y = int(m.group(2))
        rgba_hex = m.group(3)
        if len(rgba_hex) == 6:
            rgba_hex += "FF"
        r = int(rgba_hex[0:2], 16)
        g = int(rgba_hex[2:4], 16)
        b = int(rgba_hex[4:6], 16)
        a = int(rgba_hex[6:8], 16)

        if a < 128 or (r == 255 and g == 0 and b == 0):
            value = 0
        else:
            luminance = (r * 299 + g * 587 + b * 114) // 1000
            value = 1 if luminance < 128 else 2

        if 0 <= x < width and 0 <= y < height:
            pixels[y * width + x] = value

if pixels is None:
    raise SystemExit("No pixel data found in converted image")

lines = []
for y in range(height):
    row = pixels[y * width:(y + 1) * width]
    lines.append("    " + ", ".join(str(v) for v in row) + ",")

content = """#ifndef MINIDOS_CURSOR_BITMAP_H
#define MINIDOS_CURSOR_BITMAP_H

/* Transparent key for BMP fallback assets: #FF0000 */

#define UI_CURSOR_BITMAP_WIDTH %d
#define UI_CURSOR_BITMAP_HEIGHT %d
#define UI_CURSOR_HOTSPOT_X 0
#define UI_CURSOR_HOTSPOT_Y 0

enum {
    UI_CURSOR_PIXEL_TRANSPARENT = 0,
    UI_CURSOR_PIXEL_OUTLINE = 1,
    UI_CURSOR_PIXEL_FILL = 2,
};

static const unsigned char ui_cursor_bitmap[UI_CURSOR_BITMAP_WIDTH * UI_CURSOR_BITMAP_HEIGHT] = {
%s
};

#endif
""" % (width, height, "\n".join(lines))

with open(out_path, "w", encoding="utf-8") as f:
    f.write(content)

transparent = pixels.count(0)
outline = pixels.count(1)
fill = pixels.count(2)
print(f"✓ Header written: {out_path}")
print(f"✓ Pixels -> transparent={transparent} outline={outline} fill={fill}")
PY

rm -f "$TMP_TXT"
