#!/bin/bash
# Test script for MiniDOS

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Starting MiniDOS in QEMU..."
echo "Press Ctrl+Alt+G to release mouse, Ctrl+Alt+2 for monitor, Ctrl+Alt+1 to return"
echo ""

qemu-system-i386 -fda "$ROOT_DIR/minidos.img" -boot a
