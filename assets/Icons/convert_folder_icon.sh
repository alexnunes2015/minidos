#!/bin/bash
# Convert the Win95 folder PNG into a compiled MiniDOS ARGB icon header.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
INPUT="${1:-$SCRIPT_DIR/Folder.png}"
OUTPUT="${2:-$ROOT_DIR/external_apps/apps/win95_demo/win95_folder_icon.h}"
ICON_W="${3:-28}"
ICON_H="${4:-24}"
TMP_TXT="$(mktemp)"

if [ ! -f "$INPUT" ]; then
    echo "Error: input icon '$INPUT' not found" >&2
    exit 1
fi

if ! command -v convert >/dev/null 2>&1; then
    echo "Error: ImageMagick 'convert' not found." >&2
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"

convert "$INPUT" \
    -background none \
    -alpha on \
    -resize "${ICON_W}x${ICON_H}" \
    -gravity center \
    -extent "${ICON_W}x${ICON_H}" \
    TXT:"$TMP_TXT"

python3 - "$TMP_TXT" "$OUTPUT" "$ICON_W" "$ICON_H" <<'PY'
import re
import sys

src_path, out_path, width_s, height_s = sys.argv[1:5]
exp_w = int(width_s)
exp_h = int(height_s)
header_re = re.compile(r"^# ImageMagick pixel enumeration: (\d+),(\d+),")
line_re = re.compile(r"^(\d+),(\d+):\s+.*#([0-9A-Fa-f]{6,8})")
width = 0
height = 0
pixels = None

with open(src_path, "r", encoding="utf-8", errors="replace") as f:
    for raw in f:
        line = raw.strip()
        if pixels is None:
            header = header_re.match(line)
            if header:
                width = int(header.group(1))
                height = int(header.group(2))
                pixels = [0] * (width * height)
                continue

        match = line_re.match(line)
        if not match or pixels is None:
            continue

        x = int(match.group(1))
        y = int(match.group(2))
        rgba_hex = match.group(3)
        if len(rgba_hex) == 6:
            rgba_hex += "FF"

        r = int(rgba_hex[0:2], 16)
        g = int(rgba_hex[2:4], 16)
        b = int(rgba_hex[4:6], 16)
        a = int(rgba_hex[6:8], 16)
        pixels[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b

if pixels is None:
    raise SystemExit("No pixel data found in converted icon")
if width != exp_w or height != exp_h:
    raise SystemExit(f"Unexpected icon size {width}x{height}; expected {exp_w}x{exp_h}")

lines = []
for y in range(height):
    row = pixels[y * width:(y + 1) * width]
    lines.append("    " + ", ".join(f"0x{px:08X}u" for px in row) + ",")

content = """#ifndef WIN95_FOLDER_ICON_H
#define WIN95_FOLDER_ICON_H

#define WIN95_FOLDER_ICON_WIDTH %d
#define WIN95_FOLDER_ICON_HEIGHT %d

static const unsigned int g_win95_folder_icon_argb[WIN95_FOLDER_ICON_WIDTH * WIN95_FOLDER_ICON_HEIGHT] = {
%s
};

#endif
""" % (width, height, "\n".join(lines))

with open(out_path, "w", encoding="utf-8") as out:
    out.write(content)

opaque = sum(1 for px in pixels if (px >> 24) >= 128)
print(f"✓ Header written: {out_path}")
print(f"✓ Icon size: {width}x{height}, opaque pixels: {opaque}")
PY

rm -f "$TMP_TXT"
