#!/bin/bash
# Script para criar uma imagem de disco com MBR e partições (sem sudo!)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos_mbr.img"
DISK_SIZE_MB=128

echo "=== Creating MBR disk image with partitions ==="

# Create empty disk
dd if=/dev/zero of="$DISK_IMG" bs=1M count=$DISK_SIZE_MB status=progress
echo "✓ Created 128MB disk"

# Create partition table with sfdisk (works without sudo on files)
# Partition 1: starts at sector 4096 (2MB offset), 32MB, type 6 (FAT16)
# Partition 2: 32MB, type 6 (FAT16)
# Partition 3: 32MB, type 6 (FAT16)
echo "Creating partition table..."

# Write MBR boot code (first 446 bytes can be custom)
# For now, just use the existing bootloader in the partition area

# Create partition entries (each 16 bytes) at offset 446 in MBR
# We'll use dd and printf to create the partition table manually
# Or use parted which should work

if command -v parted &> /dev/null; then
    echo "Using parted for partitioning..."
    parted -s "$DISK_IMG" mklabel msdos
    parted -s "$DISK_IMG" mkpart primary fat16 1MiB 33MiB
    parted -s "$DISK_IMG" mkpart primary fat16 33MiB 65MiB
    parted -s "$DISK_IMG" mkpart primary fat16 65MiB 97MiB
    parted -s "$DISK_IMG" set 1 boot on
else
    echo "Parted not found, using sfdisk..."
    # Create partition table using sfdisk
    cat << EOF | sfdisk "$DISK_IMG"
unit: sectors
/dev/sda1: start=2048, size=65536, Id=6, bootable
/dev/sda2: start=67584, size=65536, Id=6
/dev/sda3: start=133120, size=65536, Id=6
/dev/sda4: start=0, size=0, Id=0
EOF
fi

echo "✓ Partition table created"

echo ""
echo "=== Writing bootloader and kernel ==="

# Write bootloader to MBR (sector 0)
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none
echo "✓ Bootloader written to MBR"

# Write kernel to sector 1
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none
echo "✓ Kernel written to sector 1"

echo ""
echo "=== Formatting partitions ==="

# Create temporary mount point
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Use loop device with sudo just for formatting (minimal privilege elevation)
echo "Setting up partitions (may require sudo)..."

LOOP_DEVICE=$(sudo losetup -f --show -P "$DISK_IMG")
trap "sudo losetup -d $LOOP_DEVICE; rm -rf $TEMP_DIR" EXIT

sleep 1

# Format each partition (with sudo, but minimal prompts)
echo "Formatting partitions (will prompt for sudo once)..."
{
    mkfs.vfat -F 16 -n "DRIVE_C" "${LOOP_DEVICE}p1" 2>/dev/null
    mkfs.vfat -F 16 -n "DRIVE_D" "${LOOP_DEVICE}p2" 2>/dev/null
    mkfs.vfat -F 16 -n "DRIVE_E" "${LOOP_DEVICE}p3" 2>/dev/null
} | sudo sh 2>/dev/null || true

sleep 1

# Mount and add test files
echo "Adding test files..."

for i in 1 2 3; do
    MOUNT_PATH="$TEMP_DIR/mnt_part$i"
    mkdir -p "$MOUNT_PATH"
    
    PART_NAME=$([ $i -eq 1 ] && echo "C" || ([ $i -eq 2 ] && echo "D" || echo "E"))
    
    sudo mount "${LOOP_DEVICE}p$i" "$MOUNT_PATH" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "This is drive $PART_NAME:" | sudo tee "$MOUNT_PATH/README.TXT" > /dev/null 2>&1
        echo "Test file $i" | sudo tee "$MOUNT_PATH/TEST$i.TXT" > /dev/null 2>&1
        sudo umount "$MOUNT_PATH" 2>/dev/null
        echo "  ✓ Drive $PART_NAME: ready"
    fi
done

# Fix permissions
sudo chown $USER:$USER $DISK_IMG 2>/dev/null

echo ""
echo "=== MBR disk image ready! ==="
echo ""
echo "Test with:"
echo "  qemu-system-i386 -drive file=$DISK_IMG,format=raw,if=ide -m 16M"
echo ""
echo "Commands to try in MiniDOS:"
echo "  drives    - List all detected drives"
echo "  C:        - Switch to drive C:"
echo "  dir       - List files on current drive"
echo "  D:        - Switch to drive D:"
echo "  E:        - Switch to drive E:"
