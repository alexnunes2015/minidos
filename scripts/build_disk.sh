#!/bin/bash
# Create MiniDOS disk with MBR and partitions

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos.img"

echo "=== Building MiniDOS with partitions ==="

# Create empty 256MB disk
echo "Creating disk..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=256 status=none

# Create MBR partition table
echo "Creating partition table (C:, D:, E:)..."
sfdisk "$DISK_IMG" << 'EOF' >/dev/null 2>&1
unit: sectors
/dev/sda1: start=2048, size=131072, Id=6
/dev/sda2: start=133120, size=131072, Id=6
/dev/sda3: start=264192, size=131072, Id=6
EOF

# Write bootloader and kernel
echo "Writing bootloader, stage2, and kernel..."
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 count=446 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=1 skip=510 seek=510 count=2 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/stage2.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none 2>/dev/null
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek=3 conv=notrunc status=none 2>/dev/null

# Write boot logo if it exists (at sector 21, after kernel)
if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    echo "Writing boot logo to sector 21..."
    dd if="$ROOT_DIR/assets/bootlogo/logo.raw" of="$DISK_IMG" bs=512 seek=21 conv=notrunc status=none 2>/dev/null
fi

echo "Formatting partitions and adding test files..."

format_with_sudo() {
sudo bash << SUDO_EOF
ROOT_DIR="$ROOT_DIR"
DISK_IMG="$ROOT_DIR/minidos.img"

# Setup loop devices
LOOP1=$(losetup -f --show -o 1048576 "$DISK_IMG")
LOOP2=$(losetup -f --show -o 68157440 "$DISK_IMG")
LOOP3=$(losetup -f --show -o 135266304 "$DISK_IMG")

trap "losetup -d $LOOP1 $LOOP2 $LOOP3 2>/dev/null" EXIT

# Format
mkfs.vfat -F 16 -n "MINIDOS_C" "$LOOP1"
mkfs.vfat -F 16 -n "MINIDOS_D" "$LOOP2"
mkfs.vfat -F 16 -n "MINIDOS_E" "$LOOP3"

TMPDIR=$(mktemp -d)
trap "losetup -d $LOOP1 $LOOP2 $LOOP3 2>/dev/null; rm -rf $TMPDIR" EXIT

# Add files to C:
mount "$LOOP1" "$TMPDIR"
echo "Welcome to drive C:" > $TMPDIR/README.TXT
echo "Test file 1" > $TMPDIR/FILE1.TXT
echo "Test file 2" > $TMPDIR/FILE2.TXT

# Add boot logo if it exists
if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
    cp "$ROOT_DIR/assets/bootlogo/logo.raw" "$TMPDIR/BOOTLOGO.DAT"
    echo "✓ Boot logo added"
fi

umount "$TMPDIR"

# Add files to D:
mount "$LOOP2" "$TMPDIR"
echo "Welcome to drive D:" > $TMPDIR/README.TXT
echo "Data on D:" > $TMPDIR/DATA.TXT
echo "More text" > $TMPDIR/MORE.TXT
umount "$TMPDIR"

# Add files to E:
mount "$LOOP3" "$TMPDIR"
echo "Welcome to drive E:" > $TMPDIR/README.TXT
echo "System info" > $TMPDIR/INFO.TXT
umount "$TMPDIR"

chown $(id -u 1000):$(id -g 1000) "$DISK_IMG" 2>/dev/null || true
chmod 644 "$DISK_IMG"
SUDO_EOF
}

format_with_mtools() {
    DISK_IMG="$ROOT_DIR/minidos.img"
    C_OFF=$((2048*512))
    D_OFF=$((133120*512))
    E_OFF=$((264192*512))

    mformat -i "$DISK_IMG@@$C_OFF" -F -v MINIDOS_C ::
    mformat -i "$DISK_IMG@@$D_OFF" -F -v MINIDOS_D ::
    mformat -i "$DISK_IMG@@$E_OFF" -F -v MINIDOS_E ::

    echo "Welcome to drive C:" | mcopy -i "$DISK_IMG@@$C_OFF" - ::/README.TXT
    echo "Test file 1" | mcopy -i "$DISK_IMG@@$C_OFF" - ::/FILE1.TXT
    echo "Test file 2" | mcopy -i "$DISK_IMG@@$C_OFF" - ::/FILE2.TXT

    if [ -f "$ROOT_DIR/assets/bootlogo/logo.raw" ]; then
        mcopy -i "$DISK_IMG@@$C_OFF" "$ROOT_DIR/assets/bootlogo/logo.raw" ::/BOOTLOGO.DAT
        echo "✓ Boot logo added"
    fi

    echo "Welcome to drive D:" | mcopy -i "$DISK_IMG@@$D_OFF" - ::/README.TXT
    echo "Data on D:" | mcopy -i "$DISK_IMG@@$D_OFF" - ::/DATA.TXT
    echo "More text" | mcopy -i "$DISK_IMG@@$D_OFF" - ::/MORE.TXT

    echo "Welcome to drive E:" | mcopy -i "$DISK_IMG@@$E_OFF" - ::/README.TXT
    echo "System info" | mcopy -i "$DISK_IMG@@$E_OFF" - ::/INFO.TXT
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
