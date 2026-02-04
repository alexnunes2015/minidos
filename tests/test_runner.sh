#!/bin/bash

# Advanced MiniDOS Test Runner
# Usage: ./test_runner.sh "cmd1" "cmd2" "cmd3"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="$ROOT_DIR/minidos.img"
QEMU="qemu-system-i386"

if [ ! -f "$DISK" ]; then
    echo "ERROR: $DISK not found"
    exit 1
fi

echo "╔════════════════════════════════════════╗"
echo "║      MiniDOS Automated Test Suite      ║"
echo "╚════════════════════════════════════════╝"
echo ""

# Collect all commands from arguments
if [ $# -eq 0 ]; then
    echo "Usage: $0 cmd1 cmd2 cmd3 ..."
    echo ""
    echo "Examples:"
    echo "  $0 help"
    echo "  $0 ver drives"
    echo "  $0 c: dir type hello.txt"
    exit 1
fi

echo "Commands to execute:"
for i in "$@"; do
    echo "  • $i"
done
echo ""

# Build input with proper delays
build_input() {
    for cmd in "$@"; do
        echo "$cmd"
        sleep 0.2  # Delay between commands
    done
}

# Run test with timeout
echo "Starting MiniDOS..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

(
    # Wait for boot
    sleep 3
    
    # Send commands with delays
    build_input "$@"
    
    # Keep shell alive
    sleep 2
) | timeout 15 $QEMU \
    -drive "file=$DISK,format=raw,if=ide" \
    -m 16M \
    -serial stdio \
    -monitor none \
    -display none \
    2>&1 | grep -v "^$"

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Test completed!"
