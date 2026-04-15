#!/bin/bash
# Create MiniDOS 128MB FAT16 disk image

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos.img"
DISK_KB=$((128 * 1024))
DISK_SECTORS=$((DISK_KB * 2))
RESERVED_SECTORS=256
KERNEL_LOAD_SECTOR=5
APP_SPECS=$(cat <<'EOF'
GUESS100|GAMES|external_apps/apps/guess100/guess100.c
DOSSHELL||external_apps/apps/dosshell/dosshell.c
EDIT||external_apps/apps/edit/edit.c
STARTUI|AIOS|external_apps/apps/win95_demo/win95_demo.c
PTCPU|PTEST|external_apps/apps/ptcpu/ptcpu.c
PTWAIT|PTEST|external_apps/apps/ptwait/ptwait.c
PTIO|PTEST|external_apps/apps/ptio/ptio.c
PTGFX|PTEST|external_apps/apps/ptgfx/ptgfx.c
PTTHRD|PTEST|external_apps/apps/ptthrd/ptthrd.c
EOF
)

STARTUI_ASSET_DIR="$ROOT_DIR/external_apps/apps/win95_demo"
STARTUI_RUNTIME_DIR="AIOS"
USER_HOME_DIR="USER/ADM"
USER_HOME_SUBDIRS=(Desktop Documents Images Songs MainMenu)

IMAGE_POPULATOR=""

error_exit() {
    echo "ERROR: $*" >&2
    exit 1
}

select_image_populator() {
    if command -v mcopy >/dev/null 2>&1 && command -v mmd >/dev/null 2>&1; then
        IMAGE_POPULATOR="mtools"
        echo "Image population method: mtools"
        return
    fi
    if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
        IMAGE_POPULATOR="sudo"
        echo "Image population method: sudo"
        return
    fi
    error_exit "Could not populate the disk image. Install mtools (mcopy/mmd) or configure passwordless sudo."
}

ensure_mkfs_tool() {
    if ! command -v mkfs.vfat >/dev/null 2>&1; then
        error_exit "mkfs.vfat is required to build the disk image. Install dosfstools."
    fi
}

ensure_python3() {
    if ! command -v python3 >/dev/null 2>&1; then
        error_exit "python3 is required to generate stage2 metadata."
    fi
}

select_image_populator
ensure_mkfs_tool
ensure_python3

echo "=== Building MiniDOS disk image ==="

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

copy_startui_resources_tree() {
    local target_root="$1"
    local runtime_dir="$target_root/$STARTUI_RUNTIME_DIR"
    local resource
    local base

    mkdir -p "$runtime_dir"
    for resource in "$STARTUI_ASSET_DIR"/*; do
        [ -f "$resource" ] || continue
        base="$(basename "$resource")"
        case "$base" in
            *.c|*.h|*.asm|*.ld|*.md)
                continue
                ;;
        esac
        cp "$resource" "$runtime_dir/$base"
    done
}

write_desktop_experiments_tree() {
    local target_root="$1"
    local desktop_dir="$target_root/$USER_HOME_DIR/Desktop"

    mkdir -p "$desktop_dir"

    cat > "$desktop_dir/EXPER1.TXT" <<'EOF'
MiniDOS desktop experiment 1.
This file is meant to show a text icon on the desktop.
EOF

    cat > "$desktop_dir/EXPER2.MD" <<'EOF'
# MiniDOS Desktop Experiment 2

This file exercises a markdown-style desktop entry.
EOF

    cat > "$desktop_dir/EXPER3.BIN" <<'EOF'
Binary placeholder for desktop icon tests.
EOF

    cat > "$desktop_dir/EXPER4.DAT" <<'EOF'
Data placeholder for desktop icon tests.
EOF

}

create_user_home_tree() {
    local target_root="$1"
    local subdir

    mkdir -p "$target_root/$USER_HOME_DIR"
    for subdir in "${USER_HOME_SUBDIRS[@]}"; do
        mkdir -p "$target_root/$USER_HOME_DIR/$subdir"
    done
}

copy_payload_tree() {
    local target_root="$1"
    local app_name
    local app_subdir
    local template_rel
    local dest_dir

    echo "Welcome to drive A:" > "$target_root/README.TXT"
    create_user_home_tree "$target_root"
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
    write_desktop_experiments_tree "$target_root"
    copy_startui_resources_tree "$target_root"
    copy_boot_logo_tree "$target_root"
}

build_bundled_apps

KERNEL_BYTES=$(stat -c%s "$ROOT_DIR/build/kernel.bin")
KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))
STAGE2_LST="$ROOT_DIR/build/stage2.lst"
STAGE2_BIN="$ROOT_DIR/build/stage2.bin"
STAGE2_META="$ROOT_DIR/build/stage2.meta"

generate_stage2_metadata() {
    python3 "$ROOT_DIR/scripts/stage2_metadata.py" "$STAGE2_LST" "$STAGE2_META"
}

if [ "$KERNEL_SECTORS" -gt $((RESERVED_SECTORS - KERNEL_LOAD_SECTOR)) ]; then
    echo "ERROR: kernel.bin is too large for the reserved boot area." >&2
    echo "ERROR: kernel sectors=$KERNEL_SECTORS reserved=$RESERVED_SECTORS load_sector=$KERNEL_LOAD_SECTOR" >&2
    exit 1
fi

if [ -f "$STAGE2_LST" ] && [ -f "$STAGE2_BIN" ]; then
    if ! generate_stage2_metadata; then
        error_exit "Failed to generate stage2 metadata; cannot patch kernel sectors."
    fi
    OFFSET_HEX=$(
        python3 - <<'PY' "$STAGE2_META"
import json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
print(data.get("kernel_sectors_offset", ""))
PY
    )
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
        echo "Patched stage2 kernel_sectors=$KERNEL_SECTORS at $OFFSET_HEX"
    else
        echo "WARNING: stage2 metadata missing kernel_sectors offset" >&2
    fi
else
    echo "WARNING: stage2.lst or stage2.bin missing; kernel sectors not patched" >&2
fi

if ! command -v mkfs.vfat >/dev/null 2>&1; then
    echo "ERROR: mkfs.vfat is required to build the default disk image." >&2
    exit 1
fi

echo "Creating IDE HDD image (${DISK_KB}KB)..."
dd if=/dev/zero of="$DISK_IMG" bs=1024 count="$DISK_KB" status=none

echo "Formatting FAT16 disk volume..."
mkfs.vfat \
    -F 16 \
    -f 2 \
    -S 512 \
    -h 0 \
    -M 0xF8 \
    -n "MINIDOS" \
    -R "$RESERVED_SECTORS" \
    -s 8 \
    "$DISK_IMG" >/dev/null

echo "Writing bootloader, stage2, and kernel..."
# Copy jump instruction (3 bytes)
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 count=3 conv=notrunc status=none 2>/dev/null
# Copy boot code (from byte 62 to 512)
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 skip=62 seek=62 count=450 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/stage2.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek="$KERNEL_LOAD_SECTOR" conv=notrunc status=none 2>/dev/null

echo "Adding files to disk image..."

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
    local dest_path
    local resource
    local base
    local dir_part

    ensure_mtools_dir_tree() {
        local rel_path="$1"
        local current=""
        local segment

        rel_path="${rel_path#/}"
        rel_path="${rel_path#::/}"

        IFS='/' read -r -a segments <<< "$rel_path"
        for segment in "${segments[@]}"; do
            [ -n "$segment" ] || continue
            if [ -z "$current" ]; then
                current="$segment"
            else
                current="$current/$segment"
            fi
            if ! mdir -i "$DISK_IMG" "::/$current" >/dev/null 2>&1; then
                if ! mmd -i "$DISK_IMG" "::/$current"; then
                    echo "ERROR: failed to create $current directory via mtools" >&2
                    return 1
                fi
            fi
        done
    }

    if ! echo "Welcome to drive A:" | mcopy -i "$DISK_IMG" - ::/README.TXT; then
        echo "ERROR: failed to write README.TXT via mtools" >&2
        return 1
    fi
    for dir_part in "$USER_HOME_DIR" "$USER_HOME_DIR/Desktop" "$USER_HOME_DIR/Documents" "$USER_HOME_DIR/Images" "$USER_HOME_DIR/Songs" "$USER_HOME_DIR/MainMenu"; do
        if ! ensure_mtools_dir_tree "$dir_part"; then
            return 1
        fi
    done
    while IFS='|' read -r app_name app_subdir template_rel; do
        [ -n "$app_name" ] || continue
        if [ -n "$app_subdir" ]; then
            if ! ensure_mtools_dir_tree "$app_subdir"; then
                return 1
            fi
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
        echo "ERROR: failed to write PTEST/README.TXT via mtools" >&2
        return 1
    fi

    if ! cat <<'EOF' | mcopy -o -i "$DISK_IMG" - "::/USER/ADM/Desktop/EXPER1.TXT"; then
MiniDOS desktop experiment 1.
This file is meant to show a text icon on the desktop.
EOF
        echo "ERROR: failed to write USER/ADM/Desktop/EXPER1.TXT via mtools" >&2
        return 1
    fi

    if ! cat <<'EOF' | mcopy -o -i "$DISK_IMG" - "::/USER/ADM/Desktop/EXPER2.MD"; then
# MiniDOS Desktop Experiment 2

This file exercises a markdown-style desktop entry.
EOF
        echo "ERROR: failed to write USER/ADM/Desktop/EXPER2.MD via mtools" >&2
        return 1
    fi

    if ! cat <<'EOF' | mcopy -o -i "$DISK_IMG" - "::/USER/ADM/Desktop/EXPER3.BIN"; then
Binary placeholder for desktop icon tests.
EOF
        echo "ERROR: failed to write USER/ADM/Desktop/EXPER3.BIN via mtools" >&2
        return 1
    fi

    if ! cat <<'EOF' | mcopy -o -i "$DISK_IMG" - "::/USER/ADM/Desktop/EXPER4.DAT"; then
Data placeholder for desktop icon tests.
EOF
        echo "ERROR: failed to write USER/ADM/Desktop/EXPER4.DAT via mtools" >&2
        return 1
    fi

    for resource in "$STARTUI_ASSET_DIR"/*; do
        [ -f "$resource" ] || continue
        base="$(basename "$resource")"
        case "$base" in
            *.c|*.h|*.asm|*.ld|*.md)
                continue
                ;;
        esac
        if ! mcopy -o -i "$DISK_IMG" "$resource" "::/$STARTUI_RUNTIME_DIR/$base"; then
            echo "ERROR: failed to write $base via mtools" >&2
            return 1
        fi
    done

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

case "$IMAGE_POPULATOR" in
    mtools)
        copy_with_mtools || exit 1
        ;;
    sudo)
        copy_with_sudo || exit 1
        ;;
esac

chmod 644 "$DISK_IMG"

echo ""
echo "✓ MiniDOS IDE HDD ready: $DISK_IMG (128MB)"
echo ""
echo "Test with:"
echo "  qemu-system-i386 -drive file=minidos.img,format=raw,if=ide,index=0 -boot c -m 16M"
echo ""
echo "Or use: make run"
