#!/bin/bash
# Create MiniDOS disk with MBR and partitions

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos.img"

echo "=== Building MiniDOS with partitions ==="

# Create empty 256MB disk
echo "Creating disk..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=256 status=none

# Create MBR partition table
echo "Creating partition table (A:)..."
sfdisk "$DISK_IMG" << 'EOF' >/dev/null 2>&1
unit: sectors
/dev/sda1: start=2048, size=522240, Id=6
EOF

# Write bootloader and kernel
echo "Writing bootloader, stage2, and kernel..."
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 count=446 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 skip=510 seek=510 count=2 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/stage2.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek=3 conv=notrunc status=none 2>/dev/null

# Write boot logo raw data if it exists (fixed LBA for bootloader)
if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    PAL_SECTOR=98
    LOGO_SECTOR=100
    KERNEL_BYTES=$(stat -c%s "$ROOT_DIR/build/kernel.bin")
    KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))
    KERNEL_END=$((3 + KERNEL_SECTORS))

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

# Add boot logo if it exists
if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    cp "$ROOT_DIR/assets/bootlogo/logo.raw" "$TMPDIR/BOOTLOGO.DAT"
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
    mformat -i "$DISK_IMG@@$A_OFF" -v MINIDOS_A ::

    echo "Welcome to drive A:" | mcopy -i "$DISK_IMG@@$A_OFF" - ::/README.TXT

    if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
        mcopy -i "$DISK_IMG@@$A_OFF" "$ROOT_DIR/assets/bootlogo/logo.raw" ::/BOOTLOGO.DAT
        echo "✓ Boot logo added"
    fi
}

if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
    format_with_sudo
elif command -v mformat >/dev/null 2>&1 && command -v mcopy >/dev/null 2>&1; then
    format_with_mtools
else
    echo "WARNING: Could not format/mount partitions. Install mtools or run with sudo." >&2
fi

echo ""
echo "✓ MiniDOS disk ready: $DISK_IMG (256MB)"
echo ""
echo "Test with:"
echo "  qemu-system-i386 -drive file=minidos.img,format=raw,if=ide -m 16M"
echo ""
echo "Or use: make run"
