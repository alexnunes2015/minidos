#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG_PATH="${1:-$ROOT_DIR/minidos.img}"
BUILD_DIR="$ROOT_DIR/build"

EXPECTED_IMG_SIZE=$((128 * 1024 * 1024))
EXPECTED_RESERVED_SECTORS=256
EXPECTED_STAGE2_SECTORS=4
KERNEL_LOAD_SECTOR=5
EXPECTED_ROOT_ENTRIES=512
EXPECTED_SECTORS_PER_FAT=128
EXPECTED_TOTAL_SECTORS=262144

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required host tool: $1"
}

read_u8() {
    od -An -j "$2" -N 1 -tu1 "$1" | tr -d ' \n'
}

read_u16() {
    od -An -j "$2" -N 2 -tu2 "$1" | tr -d ' \n'
}

read_u32() {
    od -An -j "$2" -N 4 -tu4 "$1" | tr -d ' \n'
}

read_hex() {
    od -An -j "$2" -N "$3" -tx1 "$1" | tr -d ' \n'
}

assert_eq() {
    local actual="$1"
    local expected="$2"
    local label="$3"

    if [ "$actual" != "$expected" ]; then
        fail "$label mismatch: expected $expected, got $actual"
    fi
}

require_entry() {
    local target="$1"
    if ! mdir -i "$IMG_PATH" "$target" >/dev/null 2>&1; then
        fail "missing FAT entry: $target"
    fi
}

require_cmd od
require_cmd awk
require_cmd stat
require_cmd mdir

[ -f "$IMG_PATH" ] || fail "disk image not found: $IMG_PATH"
[ -f "$BUILD_DIR/boot.bin" ] || fail "missing build artifact: $BUILD_DIR/boot.bin"
[ -f "$BUILD_DIR/stage2.bin" ] || fail "missing build artifact: $BUILD_DIR/stage2.bin"
[ -f "$BUILD_DIR/kernel.bin" ] || fail "missing build artifact: $BUILD_DIR/kernel.bin"

img_size="$(stat -c%s "$IMG_PATH")"
boot_size="$(stat -c%s "$BUILD_DIR/boot.bin")"
stage2_size="$(stat -c%s "$BUILD_DIR/stage2.bin")"
kernel_size="$(stat -c%s "$BUILD_DIR/kernel.bin")"
kernel_sectors="$(((kernel_size + 511) / 512))"

assert_eq "$img_size" "$EXPECTED_IMG_SIZE" "disk image size"
assert_eq "$boot_size" "512" "boot sector size"

if [ "$stage2_size" -gt $((EXPECTED_STAGE2_SECTORS * 512)) ]; then
    fail "stage2.bin exceeds fixed stage2 budget (${stage2_size} bytes)"
fi

boot_sig="$(read_hex "$IMG_PATH" 510 2)"
assert_eq "$boot_sig" "55aa" "boot signature"

assert_eq "$(read_u16 "$IMG_PATH" 11)" "512" "bytes/sector"
assert_eq "$(read_u8 "$IMG_PATH" 13)" "8" "sectors/cluster"
assert_eq "$(read_u16 "$IMG_PATH" 14)" "$EXPECTED_RESERVED_SECTORS" "reserved sectors"
assert_eq "$(read_u8 "$IMG_PATH" 16)" "2" "FAT count"
assert_eq "$(read_u16 "$IMG_PATH" 17)" "$EXPECTED_ROOT_ENTRIES" "root entry count"
assert_eq "$(read_u16 "$IMG_PATH" 19)" "0" "total sectors (16-bit field)"
assert_eq "$(read_u8 "$IMG_PATH" 21)" "248" "media descriptor"
assert_eq "$(read_u16 "$IMG_PATH" 22)" "$EXPECTED_SECTORS_PER_FAT" "sectors/FAT"
assert_eq "$(read_u32 "$IMG_PATH" 32)" "$EXPECTED_TOTAL_SECTORS" "total sectors (32-bit field)"

if [ "$kernel_sectors" -gt $((EXPECTED_RESERVED_SECTORS - KERNEL_LOAD_SECTOR)) ]; then
    fail "kernel.bin exceeds reserved boot area (${kernel_sectors} sectors)"
fi

if [ -f "$BUILD_DIR/stage2.lst" ]; then
    offset_hex="$(awk '/kernel_sectors:/ {print $2; exit}' "$BUILD_DIR/stage2.lst")"
    if [ -n "$offset_hex" ]; then
        patched_kernel_sectors="$(read_u16 "$BUILD_DIR/stage2.bin" "$((16#$offset_hex))")"
        assert_eq "$patched_kernel_sectors" "$kernel_sectors" "patched stage2 kernel sector count"
    fi
fi

require_entry "::/README.TXT"
require_entry "::/DOSSHELL.ELF"
require_entry "::/EDIT.ELF"
require_entry "::/USER"
require_entry "::/USER/ADM"
require_entry "::/USER/ADM/Desktop"
require_entry "::/USER/ADM/Documents"
require_entry "::/USER/ADM/Images"
require_entry "::/USER/ADM/Songs"
require_entry "::/AIOS"
require_entry "::/AIOS/STARTUI.ELF"
require_entry "::/AIOS/FOLDER.PNG"
require_entry "::/USER/ADM/Desktop/EXPER1.TXT"
require_entry "::/USER/ADM/Desktop/EXPER2.MD"
require_entry "::/USER/ADM/Desktop/EXPER3.BIN"
require_entry "::/USER/ADM/Desktop/EXPER4.DAT"
require_entry "::/USER/ADM/Desktop/PROJECTS"
require_entry "::/USER/ADM/Desktop/TOOLS"
require_entry "::/GAMES"
require_entry "::/GAMES/GUESS100.ELF"
require_entry "::/PTEST"
require_entry "::/PTEST/PTCPU.ELF"
require_entry "::/PTEST/PTWAIT.ELF"
require_entry "::/PTEST/PTIO.ELF"
require_entry "::/PTEST/PTTHRD.ELF"

if [ -f "$ROOT_DIR/external_apps/apps/win95_demo/bg.bmp" ]; then
    require_entry "::/AIOS/BG.BMP"
fi

if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    require_entry "::/BOOTLOGO.DAT"
fi
if [ -f "$ROOT_DIR/assets/bootlogo/logo.pal" ]; then
    require_entry "::/BOOTLOGO.PAL"
fi

echo "Image verification passed:"
echo "  image=$IMG_PATH"
echo "  size=${img_size} bytes"
echo "  kernel_sectors=${kernel_sectors}"
echo "  reserved_sectors=${EXPECTED_RESERVED_SECTORS}"
echo "  stage2_size=${stage2_size}"
