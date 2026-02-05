#!/bin/bash
# Create disk with FAT16 partitions and test files

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos_files.img"

echo "=== Creating disk with partitions and files ==="
echo "Creating 256MB disk..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=256 status=none

echo "Creating partition table..."
parted -s "$DISK_IMG" mklabel msdos
parted -s "$DISK_IMG" mkpart primary fat16 1MiB 255MiB
parted -s "$DISK_IMG" set 1 boot on

echo "Writing bootloader..."
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none

echo "Writing kernel..."
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none

echo ""
echo "Formatting partitions and adding files (requires sudo)..."
sudo bash << EOF
ROOT_DIR="$ROOT_DIR"
DISK_IMG="$ROOT_DIR/minidos_files.img"
LOOP=$(losetup -f --show -P "$DISK_IMG")
trap "losetup -d $LOOP" EXIT

mkfs.vfat -F 16 -n "A" "${LOOP}p1" >/dev/null 2>&1

TMPDIR=$(mktemp -d)
trap "losetup -d $LOOP; rm -rf $TMPDIR" EXIT

# Add files to A:
mount "${LOOP}p1" "$TMPDIR"
echo "Welcome to drive A:" > $TMPDIR/README.TXT
umount "$TMPDIR"

chown $(id -u 1000):$(id -g 1000) "$DISK_IMG" 2>/dev/null || true
EOF

echo "✓ Disk ready!"
echo ""
echo "Test with:"
echo "  qemu-system-i386 -drive file=$DISK_IMG,format=raw,if=ide -m 16M"
