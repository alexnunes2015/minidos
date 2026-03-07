#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$ROOT_DIR/external_apps/runtime"
BUILD_DIR="$ROOT_DIR/build/external_apps"
IMG_PATH="$ROOT_DIR/minidos.img"

usage() {
    echo "Usage: $0 <path/to/app.c> [APPNAME]"
    echo "  APPNAME must be 1-8 chars: A-Z, 0-9, underscore"
}

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: Missing required command: $1" >&2
        exit 1
    fi
}

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
APP_ELF="$APP_BUILD_DIR/$APP_BASE.elf"
APP_DST="$APP_BUILD_DIR/$APP_BASE.ELF"

echo "Building $APP_BASE from $SRC_PATH..."

nasm -f elf32 "$TOOLS_DIR/entry.asm" -o "$ENTRY_OBJ"

gcc -m32 -ffreestanding -O2 -Wall -Wextra \
    -fno-stack-protector -fno-pic -fno-pie -fno-common \
    -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
    -I"$TOOLS_DIR" \
    -c "$SRC_PATH" -o "$APP_OBJ"

ld -m elf_i386 -T "$TOOLS_DIR/app.ld" -o "$APP_ELF" "$ENTRY_OBJ" "$APP_OBJ"
if [[ ! -s "$APP_ELF" ]]; then
    echo "ERROR: Generated file is empty: $APP_ELF" >&2
    exit 1
fi

cp "$APP_ELF" "$APP_DST"
echo "Copying $(basename "$APP_DST") to A: in minidos.img..."
mcopy -o -i "$IMG_PATH" "$APP_DST" "::/$APP_BASE.ELF"

echo "Done."
echo "App installed as A:\\$APP_BASE.ELF"
echo "In MiniDOS, execute by typing: ${APP_BASE,,} or run ${APP_BASE,,}"
