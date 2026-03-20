#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$ROOT_DIR/external_apps/runtime"
BUILD_DIR="$ROOT_DIR/build/external_apps"
IMG_PATH="${IMG_PATH:-$ROOT_DIR/minidos.img}"

usage() {
    echo "Usage: $0 [--format elf|com] <path/to/app.c> [APPNAME]"
    echo "  APPNAME must be 1-8 chars: A-Z, 0-9, underscore"
}

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: Missing required command: $1" >&2
        exit 1
    fi
}

find_cursor_source() {
    local png_src="$ROOT_DIR/assets/cursor/cursor.png"
    local bmp_src="$ROOT_DIR/assets/cursor/cursor.bmp"

    if [[ -f "$png_src" ]]; then
        printf '%s' "$png_src"
        return
    fi
    if [[ -f "$bmp_src" ]]; then
        printf '%s' "$bmp_src"
        return
    fi
}

prepare_cursor_bitmap() {
    local cursor_src
    local cursor_header="$ROOT_DIR/external_apps/runtime/minidos_cursor_bitmap.h"

    cursor_src="$(find_cursor_source)"

    if [[ -z "$cursor_src" || ! -f "$cursor_src" ]]; then
        return
    fi

    echo "Preparing cursor bitmap..."
    chmod +x "$ROOT_DIR/assets/cursor/convert_cursor.sh"
    "$ROOT_DIR/assets/cursor/convert_cursor.sh" "$cursor_src" "$cursor_header"
}

APP_FORMAT="elf"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --format)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: Missing value for --format" >&2
                exit 1
            fi
            APP_FORMAT="$2"
            shift 2
            ;;
        --format=*)
            APP_FORMAT="${1#*=}"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            echo "ERROR: Unknown option: $1" >&2
            usage
            exit 1
            ;;
        *)
            break
            ;;
    esac
done

APP_FORMAT="$(echo "$APP_FORMAT" | tr '[:upper:]' '[:lower:]')"
if [[ "$APP_FORMAT" != "elf" && "$APP_FORMAT" != "com" ]]; then
    echo "ERROR: Unsupported format '$APP_FORMAT'." >&2
    echo "Allowed values: elf, com" >&2
    exit 1
fi

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 1
fi

SRC_INPUT="$1"
if [[ -f "$SRC_INPUT" ]]; then
    SRC_PATH="$SRC_INPUT"
else
    SRC_PATH="$ROOT_DIR/$SRC_INPUT"
fi

if [[ ! -f "$SRC_PATH" ]]; then
    echo "ERROR: Source file not found: $SRC_INPUT" >&2
    exit 1
fi

if [[ $# -eq 2 ]]; then
    APP_BASE="$2"
else
    APP_BASE="$(basename "$SRC_PATH")"
    APP_BASE="${APP_BASE%.*}"
fi

APP_BASE="$(echo "$APP_BASE" | tr '[:lower:]' '[:upper:]')"
if [[ ! "$APP_BASE" =~ ^[A-Z0-9_]{1,8}$ ]]; then
    echo "ERROR: Invalid APPNAME '$APP_BASE'." >&2
    echo "Allowed pattern: ^[A-Z0-9_]{1,8}$" >&2
    exit 1
fi

need_cmd gcc
need_cmd ld
need_cmd nasm
if [[ "$APP_FORMAT" == "com" ]]; then
    need_cmd objcopy
fi
need_cmd mcopy
need_cmd mdir

if [[ ! -f "$IMG_PATH" ]]; then
    echo "ERROR: $IMG_PATH not found. Run 'make' first." >&2
    exit 1
fi

if ! mdir -i "$IMG_PATH" :: >/dev/null 2>&1; then
    echo "ERROR: $IMG_PATH is not a readable FAT floppy image for mtools." >&2
    echo "Run 'make verify-image' and rebuild the image if needed." >&2
    exit 1
fi

mkdir -p "$BUILD_DIR/$APP_BASE"
APP_BUILD_DIR="$BUILD_DIR/$APP_BASE"

ENTRY_OBJ="$APP_BUILD_DIR/entry.o"
APP_OBJ="$APP_BUILD_DIR/app.o"
APP_ELF="$APP_BUILD_DIR/$APP_BASE.$APP_FORMAT.elf"
APP_LINKER_SCRIPT="$TOOLS_DIR/app.ld"
APP_DST="$APP_BUILD_DIR/$APP_BASE.ELF"
APP_EXT="ELF"

if [[ "$APP_FORMAT" == "com" ]]; then
    APP_LINKER_SCRIPT="$TOOLS_DIR/app_com.ld"
    APP_DST="$APP_BUILD_DIR/$APP_BASE.COM"
    APP_EXT="COM"
fi

echo "Building $APP_BASE ($APP_EXT) from $SRC_PATH..."

prepare_cursor_bitmap

nasm -f elf32 "$TOOLS_DIR/entry.asm" -o "$ENTRY_OBJ"

gcc -m32 -ffreestanding -O2 -Wall -Wextra \
    -fno-stack-protector -fno-pic -fno-pie -fno-common \
    -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
    -I"$TOOLS_DIR" \
    -c "$SRC_PATH" -o "$APP_OBJ"

ld -m elf_i386 -T "$APP_LINKER_SCRIPT" -o "$APP_ELF" "$ENTRY_OBJ" "$APP_OBJ"
if [[ ! -s "$APP_ELF" ]]; then
    echo "ERROR: Generated file is empty: $APP_ELF" >&2
    exit 1
fi

if [[ "$APP_FORMAT" == "com" ]]; then
    objcopy -O binary "$APP_ELF" "$APP_DST"
    if [[ ! -s "$APP_DST" ]]; then
        echo "ERROR: Generated file is empty: $APP_DST" >&2
        exit 1
    fi
    if [[ $(wc -c < "$APP_DST") -gt 65536 ]]; then
        echo "ERROR: COM app exceeds 64 KiB limit: $APP_DST" >&2
        exit 1
    fi
else
    cp "$APP_ELF" "$APP_DST"
fi

echo "Copying $(basename "$APP_DST") to A: in minidos.img..."
mcopy -o -i "$IMG_PATH" "$APP_DST" "::/$APP_BASE.$APP_EXT"

echo "Done."
echo "App installed as A:\\$APP_BASE.$APP_EXT"
echo "In MiniDOS, execute by typing: ${APP_BASE,,} or run ${APP_BASE,,}"
