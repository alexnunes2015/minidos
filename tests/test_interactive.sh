#!/bin/bash

# Interactive Test Mode
# Allows real-time command input to MiniDOS shell

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="$ROOT_DIR/minidos.img"
QEMU="qemu-system-i386"

if [ ! -f "$DISK" ]; then
    echo "ERROR: $DISK not found"
    exit 1
fi

echo "╔════════════════════════════════════════╗"
echo "║   MiniDOS Interactive Shell (Testing)  ║"
echo "╚════════════════════════════════════════╝"
echo ""
echo "Type 'quit' or 'exit' to stop the test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Create a FIFO for input
mkfifo /tmp/minidos_input 2>/dev/null || true

# Build command list from arguments or stdin
if [ $# -gt 0 ]; then
    # Commands from arguments
    {
        for cmd in "$@"; do
            echo "$cmd"
            sleep 0.5
        done
        sleep 2
    } > /tmp/minidos_input &
else
    # Interactive mode - read from user
    (
        while true; do
            read -p "minidos> " input
            if [ "$input" = "quit" ] || [ "$input" = "exit" ]; then
                sleep 1
                break
            fi
            echo "$input"
            sleep 0.5
        done
    ) > /tmp/minidos_input &
fi

INPUT_PID=$!

# Run QEMU with input from FIFO
timeout 30 $QEMU \
    -drive "file=$DISK,format=raw,if=ide" \
    -m 16M \
    -serial stdio \
    -monitor none \
    -display none \
    < /tmp/minidos_input 2>&1

# Cleanup
kill $INPUT_PID 2>/dev/null || true
rm -f /tmp/minidos_input

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Test completed!"
