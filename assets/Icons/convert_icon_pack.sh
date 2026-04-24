#!/bin/bash
# Convert the Win95 icon pack into a compiled MiniDOS ARGB header.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUTPUT="${1:-$ROOT_DIR/build/generated_apps/STARTUI/win95_icon_pack.h}"
ICON_W="${2:-32}"
ICON_H="${3:-32}"
TMP_DIR="$(mktemp -d)"
MANIFEST="$TMP_DIR/manifest.txt"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

if ! command -v convert >/dev/null 2>&1; then
    echo "Error: ImageMagick 'convert' not found." >&2
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"

ICON_SPECS=$(cat <<'EOF'
folder_closed|folder_closed.png|WIN95_ICON_FOLDER_CLOSED
folder_open|folder_open.png|WIN95_ICON_FOLDER_OPEN
file_unknown|file_unknown.png|WIN95_ICON_FILE_UNKNOWN
file_txt|file_txt.png|WIN95_ICON_FILE_TXT
file_wav|file_wav.png|WIN95_ICON_FILE_WAV
file_image|file_image.png|WIN95_ICON_FILE_IMAGE
app|app.png|WIN95_ICON_APP
floppy|floppy.png|WIN95_ICON_FLOPPY
hard_drive|hard_drive.png|WIN95_ICON_HARD_DRIVE
computer|computer.png|WIN95_ICON_COMPUTER
recycle_bin|recycle_bin.png|WIN95_ICON_RECYCLE_BIN
settings|settings.png|WIN95_ICON_SETTINGS
terminal|terminal.png|WIN95_ICON_TERMINAL
notepad|notepad.png|WIN95_ICON_NOTEPAD
network|network.png|WIN95_ICON_NETWORK
speaker|speaker.png|WIN95_ICON_SPEAKER
EOF
)

while IFS='|' read -r icon_name icon_file enum_name; do
    [ -n "$icon_name" ] || continue
    input_path="$SCRIPT_DIR/$icon_file"
    output_txt="$TMP_DIR/$icon_name.txt"
    bg_color=""

    if [ ! -f "$input_path" ]; then
        echo "Error: required icon '$input_path' not found" >&2
        exit 1
    fi

    bg_color="$(convert "$input_path" -format '%[pixel:p{0,0}]' info:)"

    convert "$input_path" \
        -fuzz 6% \
        -transparent "$bg_color" \
        -background none \
        -alpha on \
        -resize "${ICON_W}x${ICON_H}" \
        -gravity center \
        -extent "${ICON_W}x${ICON_H}" \
        TXT:"$output_txt"

    printf '%s|%s|%s\n' "$icon_name" "$enum_name" "$output_txt" >> "$MANIFEST"
done <<< "$ICON_SPECS"

python3 - "$MANIFEST" "$OUTPUT" "$ICON_W" "$ICON_H" <<'PY'
import re
import sys

manifest_path, out_path, width_s, height_s = sys.argv[1:5]
exp_w = int(width_s)
exp_h = int(height_s)
header_re = re.compile(r"^# ImageMagick pixel enumeration: (\d+),(\d+),")
line_re = re.compile(r"^(\d+),(\d+):\s+.*#([0-9A-Fa-f]{6,8})")


def parse_icon(txt_path):
    width = 0
    height = 0
    pixels = None

    with open(txt_path, "r", encoding="utf-8", errors="replace") as f:
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
        raise SystemExit(f"No pixel data found in converted icon: {txt_path}")
    if width != exp_w or height != exp_h:
        raise SystemExit(
            f"Unexpected icon size {width}x{height} for {txt_path}; expected {exp_w}x{exp_h}"
        )
    return width, height, pixels


icons = []
with open(manifest_path, "r", encoding="utf-8") as manifest:
    for raw in manifest:
        line = raw.strip()
        if not line:
            continue
        icon_name, enum_name, txt_path = line.split("|", 2)
        width, height, pixels = parse_icon(txt_path)
        icons.append((icon_name, enum_name, width, height, pixels))

enum_lines = []
array_blocks = []
table_lines = []

for index, (icon_name, enum_name, width, height, pixels) in enumerate(icons):
    array_name = f"g_{icon_name}_argb"
    enum_lines.append(f"    {enum_name} = {index},")
    table_lines.append(f"    {{{width}, {height}, {array_name}}},")

    rows = []
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        rows.append("    " + ", ".join(f"0x{px:08X}u" for px in row) + ",")

    array_blocks.append(
        "static const unsigned int %s[%d * %d] = {\n%s\n};"
        % (array_name, width, height, "\n".join(rows))
    )

content = """#ifndef WIN95_ICON_PACK_H
#define WIN95_ICON_PACK_H

typedef struct {
    unsigned short width;
    unsigned short height;
    const unsigned int* pixels;
} win95_icon_bitmap_t;

enum {
%s
    WIN95_ICON_COUNT = %d
};

%s

static const win95_icon_bitmap_t g_win95_icon_pack[WIN95_ICON_COUNT] = {
%s
};

#endif
""" % (
    "\n".join(enum_lines),
    len(icons),
    "\n\n".join(array_blocks),
    "\n".join(table_lines),
)

with open(out_path, "w", encoding="utf-8") as out:
    out.write(content)

print(f"✓ Header written: {out_path}")
print(f"✓ Packed {len(icons)} icons at {exp_w}x{exp_h}")
PY
