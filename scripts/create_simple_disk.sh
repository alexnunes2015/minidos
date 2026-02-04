#!/bin/bash
# Script simples para testar teclado - sem partições
# Apenas precisa de kernel e bootloader

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMG="$ROOT_DIR/minidos_simple.img"
DISK_SIZE_MB=64

echo "=== Creating simple test disk (no partitions needed) ==="

# Create empty disk
dd if=/dev/zero of="$DISK_IMG" bs=1M count=$DISK_SIZE_MB status=progress 2>/dev/null

# Write bootloader to sector 0
dd if="$ROOT_DIR/build/boot.bin" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none
echo "✓ Bootloader written to sector 0"

# Write kernel to sector 1
dd if="$ROOT_DIR/build/kernel.bin" of="$DISK_IMG" bs=512 seek=1 conv=notrunc status=none
echo "✓ Kernel written to sector 1"

echo ""
echo "=== Test disk ready! ==="
echo ""
echo "Start with: qemu-system-i386 -drive file=$DISK_IMG,format=raw,if=ide -m 16M"
echo ""
echo "Test keyboard input:"
echo "  hello       - lowercase letters"
echo "  HELLO       - Hold SHIFT for uppercase"
echo "  test: 123!  - Numbers and special chars with SHIFT"
echo "  help        - List commands"
