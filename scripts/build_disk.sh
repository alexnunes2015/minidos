#!/bin/bash
# Create MiniDOS 1.44MB FAT12 floppy image

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos.img"
DISK_KB=1440
DISK_SECTORS=2880
RESERVED_SECTORS=192
KERNEL_LOAD_SECTOR=5
APP_SPECS=$(cat <<'EOF'
GUESS100|GAMES|external_apps/apps/guess100/guess100.c
DOSSHELL||external_apps/apps/dosshell/dosshell.c
EDIT||external_apps/apps/edit/edit.c
WIN95UI||external_apps/apps/win95_demo/win95_demo.c
PTCPU|PTEST|external_apps/apps/ptcpu/ptcpu.c
PTWAIT|PTEST|external_apps/apps/ptwait/ptwait.c
PTIO|PTEST|external_apps/apps/ptio/ptio.c
PTGFX|PTEST|external_apps/apps/ptgfx/ptgfx.c
PTTHRD|PTEST|external_apps/apps/ptthrd/ptthrd.c
EOF
)

echo "=== Building MiniDOS floppy image ==="

app_build_dir() {
    printf '%s/build/external_apps/%s' "$ROOT_DIR" "$1"
}

app_elf_path() {
    local app_dir
    app_dir="$(app_build_dir "$1")"
    printf '%s/%s.ELF' "$app_dir" "$1"
}

find_cursor_source() {
    local png_src="$ROOT_DIR/assets/cursor/cursor.png"
    local bmp_src="$ROOT_DIR/assets/cursor/cursor.bmp"

    if [ -f "$png_src" ]; then
        printf '%s' "$png_src"
        return
    fi
    if [ -f "$bmp_src" ]; then
        printf '%s' "$bmp_src"
        return
    fi
}

prepare_cursor_bitmap() {
    local cursor_src
    local cursor_header="$ROOT_DIR/external_apps/runtime/minidos_cursor_bitmap.h"

    cursor_src="$(find_cursor_source)"

    if [ -z "$cursor_src" ] || [ ! -f "$cursor_src" ]; then
        return
    fi

    echo "Preparing cursor bitmap..."
    chmod +x "$ROOT_DIR/assets/cursor/convert_cursor.sh"
    "$ROOT_DIR/assets/cursor/convert_cursor.sh" "$cursor_src" "$cursor_header"
}

ensure_app_toolchain() {
    if ! command -v gcc >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1 || ! command -v nasm >/dev/null 2>&1; then
        echo "ERROR: Missing toolchain for bundled apps (gcc/ld/nasm)." >&2
        exit 1
    fi
}

build_app() {
    local app_name="$1"
    local template_rel="$2"
    local app_dir
    local app_elf

    app_dir="$(app_build_dir "$app_name")"
    app_elf="$(app_elf_path "$app_name")"
    mkdir -p "$app_dir"

    nasm -f elf32 "$ROOT_DIR/external_apps/runtime/entry.asm" -o "$app_dir/entry.o"
    gcc -m32 -ffreestanding -O2 -Wall -Wextra \
        -fno-stack-protector -fno-pic -fno-pie -fno-common \
        -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
        -I"$ROOT_DIR/external_apps/runtime" \
        -c "$ROOT_DIR/$template_rel" -o "$app_dir/app.o"
    ld -m elf_i386 -T "$ROOT_DIR/external_apps/runtime/app.ld" -o "$app_elf" "$app_dir/entry.o" "$app_dir/app.o"

    if [ ! -s "$app_elf" ]; then
        echo "ERROR: Failed to build bundled app: $app_elf" >&2
        exit 1
    fi
}

build_bundled_apps() {
    local app_name
    local app_subdir
    local template_rel

    ensure_app_toolchain
    prepare_cursor_bitmap
    while IFS='|' read -r app_name app_subdir template_rel; do
        [ -n "$app_name" ] || continue
        build_app "$app_name" "$template_rel"
    done <<< "$APP_SPECS"
}

copy_boot_logo_tree() {
    local target_root="$1"

    if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
        cp "$ROOT_DIR/assets/bootlogo/logo.raw" "$target_root/BOOTLOGO.DAT"
        if [ -f "$ROOT_DIR/assets/bootlogo/logo.pal" ]; then
            cp "$ROOT_DIR/assets/bootlogo/logo.pal" "$target_root/BOOTLOGO.PAL"
        fi
        echo "✓ Boot logo added"
    fi
}

write_ptest_readme_tree() {
    local target_root="$1"
    local ptest_dir="$target_root/PTEST"

    mkdir -p "$ptest_dir"
    cat > "$ptest_dir/README.TXT" <<'EOF'
MiniDOS PTEST pack

PTCPU  - busy CPU loop for a few scheduler ticks
PTWAIT - blocking wait/event timeout loop
PTIO   - directory + file create/read/rename/delete churn
PTGFX  - simple graphics/present animation
PTTHRD - app main plus child worker thread in the same ELF group

Usage:
  cd ptest
  elfls
  runbg ptcpu
  runbg ptthrd
  top 200 1

Note:
  background ELFs now show as scheduler tasks; PTTHRD should
  show both the main task and a child worker with the same EXE.
EOF
}

copy_payload_tree() {
    local target_root="$1"
    local app_name
    local app_subdir
    local template_rel
    local dest_dir

    echo "Welcome to drive A:" > "$target_root/README.TXT"
    while IFS='|' read -r app_name app_subdir template_rel; do
        [ -n "$app_name" ] || continue
        dest_dir="$target_root"
        if [ -n "$app_subdir" ]; then
            dest_dir="$target_root/$app_subdir"
            mkdir -p "$dest_dir"
        fi
        cp "$(app_elf_path "$app_name")" "$dest_dir/$app_name.ELF"
    done <<< "$APP_SPECS"

    write_ptest_readme_tree "$target_root"
    copy_boot_logo_tree "$target_root"
}

build_bundled_apps

KERNEL_BYTES=$(stat -c%s "$ROOT_DIR/build/kernel.bin")
KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))
STAGE2_LST="$ROOT_DIR/build/stage2.lst"
STAGE2_BIN="$ROOT_DIR/build/stage2.bin"

if [ "$KERNEL_SECTORS" -gt $((RESERVED_SECTORS - KERNEL_LOAD_SECTOR)) ]; then
    echo "ERROR: kernel.bin is too large for the reserved floppy boot area." >&2
    echo "ERROR: kernel sectors=$KERNEL_SECTORS reserved=$RESERVED_SECTORS load_sector=$KERNEL_LOAD_SECTOR" >&2
    exit 1
fi

if [ -f "$STAGE2_LST" ] && [ -f "$STAGE2_BIN" ]; then
    OFFSET_HEX=$(awk '/kernel_sectors:/ {print $2; exit}' "$STAGE2_LST")
    if [ -n "$OFFSET_HEX" ]; then
        python3 - "$STAGE2_BIN" "$OFFSET_HEX" "$KERNEL_SECTORS" <<'PY'
import sys

bin_path, offset_hex, sectors = sys.argv[1:4]
offset = int(offset_hex, 16)
value = int(sectors) & 0xFFFF

with open(bin_path, "r+b") as f:
    f.seek(offset)
    f.write(bytes((value & 0xFF, (value >> 8) & 0xFF)))
PY
        echo "Patched stage2 kernel_sectors=$KERNEL_SECTORS at 0x$OFFSET_HEX"
    else
        echo "WARNING: kernel_sectors label not found in stage2.lst" >&2
    fi
else
    echo "WARNING: stage2.lst or stage2.bin missing; kernel sectors not patched" >&2
fi

if ! command -v mkfs.vfat >/dev/null 2>&1; then
    echo "ERROR: mkfs.vfat is required to build the default floppy image." >&2
    exit 1
fi

echo "Creating 1.44MB floppy image..."
dd if=/dev/zero of="$DISK_IMG" bs=1024 count="$DISK_KB" status=none

echo "Formatting FAT12 superfloppy..."
mkfs.vfat \
    -F 12 \
    -f 2 \
    -g 2/18 \
    -h 0 \
    -M 0xF0 \
    -n "MINIDOS" \
    -r 224 \
    -R "$RESERVED_SECTORS" \
    -s 1 \
    "$DISK_IMG" >/dev/null

echo "Writing bootloader, stage2, and kernel..."
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/stage2.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek="$KERNEL_LOAD_SECTOR" conv=notrunc status=none 2>/dev/null

echo "Adding files to floppy image..."

copy_with_sudo() {
    local payload_root
    local status

    payload_root=$(mktemp -d)
    copy_payload_tree "$payload_root"

    sudo env DISK_IMG="$DISK_IMG" PAYLOAD_SRC="$payload_root" bash << 'SUDO_EOF'
TMPDIR=$(mktemp -d)
trap "umount \"$TMPDIR\" 2>/dev/null || true; rm -rf \"$TMPDIR\"" EXIT

mount -o loop "$DISK_IMG" "$TMPDIR"
cp -a "$PAYLOAD_SRC"/. "$TMPDIR"/
umount "$TMPDIR"
SUDO_EOF
    status=$?
    rm -rf "$payload_root"
    return $status
}

copy_with_mtools() {
    local app_name
    local app_subdir
    local template_rel
    local created_dirs=""
    local dest_path

    if ! echo "Welcome to drive A:" | mcopy -i "$DISK_IMG" - ::/README.TXT; then
        echo "ERROR: failed to write README.TXT via mtools" >&2
        return 1
    fi
    while IFS='|' read -r app_name app_subdir template_rel; do
        [ -n "$app_name" ] || continue
        if [ -n "$app_subdir" ] && [[ "$created_dirs" != *"|$app_subdir|"* ]]; then
            if ! mmd -i "$DISK_IMG" "::/$app_subdir"; then
                echo "ERROR: failed to create $app_subdir directory via mtools" >&2
                return 1
            fi
            created_dirs="${created_dirs}|$app_subdir|"
        fi

        dest_path="::/$app_name.ELF"
        if [ -n "$app_subdir" ]; then
            dest_path="::/$app_subdir/$app_name.ELF"
        fi

        if ! mcopy -o -i "$DISK_IMG" "$(app_elf_path "$app_name")" "$dest_path"; then
            echo "ERROR: failed to write $app_name.ELF via mtools" >&2
            return 1
        fi
    done <<< "$APP_SPECS"

    if ! cat <<'EOF' | mcopy -o -i "$DISK_IMG" - "::/PTEST/README.TXT"; then
MiniDOS PTEST pack

PTCPU  - busy CPU loop for a few scheduler ticks
PTWAIT - blocking wait/event timeout loop
PTIO   - directory + file create/read/rename/delete churn
PTGFX  - simple graphics/present animation

Usage:
  cd ptest
  elfls
  ptcpu

Note:
  top still attributes ELF runtime to the shell thread until
  ELF apps run as real scheduler processes.
EOF
        echo "ERROR: failed to write PTEST/README.TXT via mtools" >&2
        return 1
    fi

    if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
        if ! mcopy -o -i "$DISK_IMG" "$ROOT_DIR/assets/bootlogo/logo.raw" ::/BOOTLOGO.DAT; then
            echo "ERROR: failed to write BOOTLOGO.DAT via mtools" >&2
            return 1
        fi
        if [ -f "$ROOT_DIR/assets/bootlogo/logo.pal" ]; then
            if ! mcopy -o -i "$DISK_IMG" "$ROOT_DIR/assets/bootlogo/logo.pal" ::/BOOTLOGO.PAL; then
                echo "ERROR: failed to write BOOTLOGO.PAL via mtools" >&2
                return 1
            fi
        fi
        echo "✓ Boot logo added"
    fi
}

if command -v mcopy >/dev/null 2>&1 && command -v mmd >/dev/null 2>&1; then
    copy_with_mtools || exit 1
elif command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
    copy_with_sudo
else
    echo "ERROR: Could not populate the floppy image. Install mtools (mcopy/mmd) or configure passwordless sudo." >&2
    echo "ERROR: Aborting image build to avoid booting with an empty filesystem." >&2
    exit 1
fi

chmod 644 "$DISK_IMG"

echo ""
echo "✓ MiniDOS floppy ready: $DISK_IMG (1.44MB)"
echo ""
echo "Test with:"
echo "  qemu-system-i386 -drive file=minidos.img,format=raw,if=floppy,index=0 -boot a -m 16M"
echo ""
echo "Or use: make run"
