#!/bin/bash
# Create MiniDOS disk with MBR and partitions

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos.img"
DISK_KB=1440
DISK_SECTORS=2880
RESERVED_SECTORS=160
KERNEL_LOAD_SECTOR=5
GUESS_APP_NAME="GUESS100"
GUESS_APP_DIR="$ROOT_DIR/build/external_apps/$GUESS_APP_NAME"
GUESS_APP_ELF="$GUESS_APP_DIR/$GUESS_APP_NAME.ELF"
DOSSHELL_APP_NAME="DOSSHELL"
DOSSHELL_APP_DIR="$ROOT_DIR/build/external_apps/$DOSSHELL_APP_NAME"
DOSSHELL_APP_ELF="$DOSSHELL_APP_DIR/$DOSSHELL_APP_NAME.ELF"
EDIT_APP_NAME="EDIT"
EDIT_APP_DIR="$ROOT_DIR/build/external_apps/$EDIT_APP_NAME"
EDIT_APP_ELF="$EDIT_APP_DIR/$EDIT_APP_NAME.ELF"

echo "=== Building MiniDOS floppy image ==="

build_guess_game() {
    mkdir -p "$GUESS_APP_DIR"

    if ! command -v gcc >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1 || ! command -v nasm >/dev/null 2>&1; then
        echo "ERROR: Missing toolchain for default games (gcc/ld/nasm)." >&2
        exit 1
    fi

    nasm -f elf32 "$ROOT_DIR/external_apps/runtime/entry.asm" -o "$GUESS_APP_DIR/entry.o"
    gcc -m32 -ffreestanding -O2 -Wall -Wextra \
        -fno-stack-protector -fno-pic -fno-pie -fno-common \
        -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
        -I"$ROOT_DIR/external_apps/runtime" \
        -c "$ROOT_DIR/external_apps/templates/guess100.c" -o "$GUESS_APP_DIR/app.o"
    ld -m elf_i386 -T "$ROOT_DIR/external_apps/runtime/app.ld" -o "$GUESS_APP_ELF" "$GUESS_APP_DIR/entry.o" "$GUESS_APP_DIR/app.o"

    if [ ! -s "$GUESS_APP_ELF" ]; then
        echo "ERROR: Failed to build default game app: $GUESS_APP_ELF" >&2
        exit 1
    fi
}

build_dosshell_app() {
    mkdir -p "$DOSSHELL_APP_DIR"

    if ! command -v gcc >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1 || ! command -v nasm >/dev/null 2>&1; then
        echo "ERROR: Missing toolchain for DOSSHELL app (gcc/ld/nasm)." >&2
        exit 1
    fi

    nasm -f elf32 "$ROOT_DIR/external_apps/runtime/entry.asm" -o "$DOSSHELL_APP_DIR/entry.o"
    gcc -m32 -ffreestanding -O2 -Wall -Wextra \
        -fno-stack-protector -fno-pic -fno-pie -fno-common \
        -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
        -I"$ROOT_DIR/external_apps/runtime" \
        -c "$ROOT_DIR/external_apps/templates/dosshell.c" -o "$DOSSHELL_APP_DIR/app.o"
    ld -m elf_i386 -T "$ROOT_DIR/external_apps/runtime/app.ld" -o "$DOSSHELL_APP_ELF" "$DOSSHELL_APP_DIR/entry.o" "$DOSSHELL_APP_DIR/app.o"

    if [ ! -s "$DOSSHELL_APP_ELF" ]; then
        echo "ERROR: Failed to build DOSSHELL app: $DOSSHELL_APP_ELF" >&2
        exit 1
    fi
}

build_edit_app() {
    mkdir -p "$EDIT_APP_DIR"

    if ! command -v gcc >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1 || ! command -v nasm >/dev/null 2>&1; then
        echo "ERROR: Missing toolchain for EDIT app (gcc/ld/nasm)." >&2
        exit 1
    fi

    nasm -f elf32 "$ROOT_DIR/external_apps/runtime/entry.asm" -o "$EDIT_APP_DIR/entry.o"
    gcc -m32 -ffreestanding -O2 -Wall -Wextra \
        -fno-stack-protector -fno-pic -fno-pie -fno-common \
        -fno-asynchronous-unwind-tables -fno-stack-check -nostdlib \
        -I"$ROOT_DIR/external_apps/runtime" \
        -c "$ROOT_DIR/external_apps/templates/edit.c" -o "$EDIT_APP_DIR/app.o"
    ld -m elf_i386 -T "$ROOT_DIR/external_apps/runtime/app.ld" -o "$EDIT_APP_ELF" "$EDIT_APP_DIR/entry.o" "$EDIT_APP_DIR/app.o"

    if [ ! -s "$EDIT_APP_ELF" ]; then
        echo "ERROR: Failed to build EDIT app: $EDIT_APP_ELF" >&2
        exit 1
    fi
}

build_guess_game
build_dosshell_app
build_edit_app

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
sudo bash << SUDO_EOF
ROOT_DIR="$ROOT_DIR"
DISK_IMG="$ROOT_DIR/minidos.img"
GUESS_APP_NAME="$GUESS_APP_NAME"
GUESS_APP_ELF="$GUESS_APP_ELF"
DOSSHELL_APP_NAME="$DOSSHELL_APP_NAME"
DOSSHELL_APP_ELF="$DOSSHELL_APP_ELF"
EDIT_APP_NAME="$EDIT_APP_NAME"
EDIT_APP_ELF="$EDIT_APP_ELF"

TMPDIR=$(mktemp -d)
trap "umount \"$TMPDIR\" 2>/dev/null || true; rm -rf \"$TMPDIR\"" EXIT

mount -o loop "$DISK_IMG" "$TMPDIR"
echo "Welcome to drive A:" > "$TMPDIR/README.TXT"
mkdir -p "$TMPDIR/GAMES"
cp "$GUESS_APP_ELF" "$TMPDIR/GAMES/$GUESS_APP_NAME.ELF"
cp "$DOSSHELL_APP_ELF" "$TMPDIR/$DOSSHELL_APP_NAME.ELF"
cp "$EDIT_APP_ELF" "$TMPDIR/$EDIT_APP_NAME.ELF"

if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    cp "$ROOT_DIR/assets/bootlogo/logo.raw" "$TMPDIR/BOOTLOGO.DAT"
    if [ -f "$ROOT_DIR/assets/bootlogo/logo.pal" ]; then
        cp "$ROOT_DIR/assets/bootlogo/logo.pal" "$TMPDIR/BOOTLOGO.PAL"
    fi
    echo "✓ Boot logo added"
fi

umount "$TMPDIR"
SUDO_EOF
}

copy_with_mtools() {
    if ! echo "Welcome to drive A:" | mcopy -i "$DISK_IMG" - ::/README.TXT; then
        echo "ERROR: failed to write README.TXT via mtools" >&2
        return 1
    fi
    if ! mmd -i "$DISK_IMG" ::/GAMES; then
        echo "ERROR: failed to create GAMES directory via mtools" >&2
        return 1
    fi
    if ! mcopy -o -i "$DISK_IMG" "$GUESS_APP_ELF" "::/GAMES/$GUESS_APP_NAME.ELF"; then
        echo "ERROR: failed to write $GUESS_APP_NAME.ELF via mtools" >&2
        return 1
    fi
    if ! mcopy -o -i "$DISK_IMG" "$DOSSHELL_APP_ELF" "::/$DOSSHELL_APP_NAME.ELF"; then
        echo "ERROR: failed to write $DOSSHELL_APP_NAME.ELF via mtools" >&2
        return 1
    fi
    if ! mcopy -o -i "$DISK_IMG" "$EDIT_APP_ELF" "::/$EDIT_APP_NAME.ELF"; then
        echo "ERROR: failed to write $EDIT_APP_NAME.ELF via mtools" >&2
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
