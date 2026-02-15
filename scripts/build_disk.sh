#!/bin/bash
# Create MiniDOS disk with MBR and partitions

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos.img"
GUESS_APP_NAME="GUESS100"
GUESS_APP_DIR="$ROOT_DIR/build/external_apps/$GUESS_APP_NAME"
GUESS_APP_ELF="$GUESS_APP_DIR/$GUESS_APP_NAME.ELF"

echo "=== Building MiniDOS with partitions ==="

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

build_guess_game

# Clean stale partitioning processes from interrupted runs
pkill -f "sfdisk .*minidos.img" >/dev/null 2>&1 || true

# Create empty 256MB disk
echo "Creating disk..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=256 status=none

# Create MBR partition table
echo "Creating partition table (A:)..."
if ! timeout 10s sfdisk --force --no-reread --no-tell-kernel --wipe always "$DISK_IMG" >/dev/null 2>&1 << 'EOF'
unit: sectors
/dev/sda1: start=2048, size=522240, Id=6
EOF
then
    echo "ERROR: sfdisk timed out while creating partition table." >&2
    echo "Diagnostic command:" >&2
    echo "  timeout 8s sfdisk --force --no-reread --no-tell-kernel --wipe always minidos.img << 'EOF'" >&2
    echo "  unit: sectors" >&2
    echo "  /dev/sda1: start=2048, size=522240, Id=6" >&2
    echo "  EOF" >&2
    exit 1
fi

# Write bootloader and kernel
echo "Writing bootloader, stage2, and kernel..."

# Patch kernel sector count into stage2 (auto)
KERNEL_BYTES=$(stat -c%s "$ROOT_DIR/build/kernel.bin")
KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))
STAGE2_LST="$ROOT_DIR/build/stage2.lst"
STAGE2_BIN="$ROOT_DIR/build/stage2.bin"

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
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 count=446 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 skip=510 seek=510 count=2 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/stage2.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek=5 conv=notrunc status=none 2>/dev/null

# Write boot logo raw data if it exists (fixed LBA for bootloader)
if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    PAL_SECTOR=98
    LOGO_SECTOR=100
    KERNEL_BYTES=$(stat -c%s "$ROOT_DIR/build/kernel.bin")
    KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))
    KERNEL_END=$((5 + KERNEL_SECTORS))

    if [ "$KERNEL_END" -ge "$PAL_SECTOR" ]; then
        echo "WARNING: Kernel overlaps boot logo area. Increase logo LBA or shrink kernel." >&2
    else
        if [ -f "$ROOT_DIR/assets/bootlogo/logo.pal" ]; then
            echo "Writing boot logo palette to sector $PAL_SECTOR..."
            dd if="$ROOT_DIR/assets/bootlogo/logo.pal" of="$DISK_IMG" bs=512 seek="$PAL_SECTOR" conv=notrunc status=none 2>/dev/null
        fi

        LOGO_BYTES=$(stat -c%s "$ROOT_DIR/assets/bootlogo/logo.raw")
        LOGO_SECTORS=$(((LOGO_BYTES + 511) / 512))
        LOGO_END=$((LOGO_SECTOR + LOGO_SECTORS))
        if [ "$LOGO_END" -ge 2048 ]; then
            echo "WARNING: Boot logo would overlap the FAT partition. Skipping raw logo write." >&2
        else
            echo "Writing boot logo to sector $LOGO_SECTOR..."
            dd if="$ROOT_DIR/assets/bootlogo/logo.raw" of="$DISK_IMG" bs=512 seek="$LOGO_SECTOR" conv=notrunc status=none 2>/dev/null
        fi
    fi
fi

echo "Formatting partition and adding README..."

format_with_sudo() {
sudo bash << SUDO_EOF
ROOT_DIR="$ROOT_DIR"
DISK_IMG="$ROOT_DIR/minidos.img"

# Setup loop devices
LOOP1=$(losetup -f --show -o 1048576 "$DISK_IMG")

trap "losetup -d $LOOP1 2>/dev/null" EXIT

# Format
mkfs.vfat -F 16 -n "MINIDOS_A" "$LOOP1"

TMPDIR=$(mktemp -d)
trap "losetup -d $LOOP1 2>/dev/null; rm -rf $TMPDIR" EXIT

# Add files to A:
mount "$LOOP1" "$TMPDIR"
echo "Welcome to drive A:" > $TMPDIR/README.TXT
mkdir -p "$TMPDIR/GAMES"
cp "$GUESS_APP_ELF" "$TMPDIR/GAMES/$GUESS_APP_NAME.ELF"

# Add boot logo if it exists
if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    cp "$ROOT_DIR/assets/bootlogo/logo.raw" "$TMPDIR/BOOTLOGO.DAT"
    if [ -f "$ROOT_DIR/assets/bootlogo/logo.pal" ]; then
        cp "$ROOT_DIR/assets/bootlogo/logo.pal" "$TMPDIR/BOOTLOGO.PAL"
    fi
    echo "✓ Boot logo added"
fi

umount "$TMPDIR"

chown $(id -u 1000):$(id -g 1000) "$DISK_IMG" 2>/dev/null || true
chmod 644 "$DISK_IMG"
SUDO_EOF
}

format_with_mtools() {
    DISK_IMG="$ROOT_DIR/minidos.img"
    A_OFF=$((2048*512))

    # NOTE: Do NOT use -F here (it forces FAT32). We need FAT16.
    if ! mformat -i "$DISK_IMG@@$A_OFF" -v MINIDOS_A ::; then
        echo "ERROR: mformat failed for partition A:" >&2
        return 1
    fi

    if ! echo "Welcome to drive A:" | mcopy -i "$DISK_IMG@@$A_OFF" - ::/README.TXT; then
        echo "ERROR: failed to write README.TXT via mtools" >&2
        return 1
    fi
    if ! mmd -i "$DISK_IMG@@$A_OFF" ::/GAMES; then
        echo "ERROR: failed to create GAMES directory via mtools" >&2
        return 1
    fi
    if ! mcopy -o -i "$DISK_IMG@@$A_OFF" "$GUESS_APP_ELF" "::/GAMES/$GUESS_APP_NAME.ELF"; then
        echo "ERROR: failed to write $GUESS_APP_NAME.ELF via mtools" >&2
        return 1
    fi

    if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
        if ! mcopy -i "$DISK_IMG@@$A_OFF" "$ROOT_DIR/assets/bootlogo/logo.raw" ::/BOOTLOGO.DAT; then
            echo "ERROR: failed to write BOOTLOGO.DAT via mtools" >&2
            return 1
        fi
        if [ -f "$ROOT_DIR/assets/bootlogo/logo.pal" ]; then
            if ! mcopy -i "$DISK_IMG@@$A_OFF" "$ROOT_DIR/assets/bootlogo/logo.pal" ::/BOOTLOGO.PAL; then
                echo "ERROR: failed to write BOOTLOGO.PAL via mtools" >&2
                return 1
            fi
        fi
        echo "✓ Boot logo added"
    fi
}

if command -v mformat >/dev/null 2>&1 && command -v mcopy >/dev/null 2>&1; then
    format_with_mtools || exit 1
elif command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
    format_with_sudo
else
    echo "ERROR: Could not format partition A:. Install mtools (mformat/mcopy) or configure passwordless sudo." >&2
    echo "ERROR: Aborting image build to avoid booting with an invalid FAT filesystem." >&2
    exit 1
fi

echo ""
echo "✓ MiniDOS disk ready: $DISK_IMG (256MB)"
echo ""
echo "Test with:"
echo "  qemu-system-i386 -drive file=minidos.img,format=raw,if=ide -m 16M"
echo ""
echo "Or use: make run"
