#!/bin/bash

# Simple test runner for MiniDOS shell
# Sends commands via stdin and captures output

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="$ROOT_DIR/minidos.img"
QEMU="qemu-system-i386"

# Check if disk exists
if [ ! -f "$DISK" ]; then
    echo "Error: $DISK not found. Run 'make' first."
    exit 1
fi

# Function to run test
run_test() {
    local test_name="$1"
    shift
    local commands="$@"
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "TEST: $test_name"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # Create command sequence with delays
    (
        # Wait for boot to complete
        sleep 2
        
        # Send commands
        for cmd in $commands; do
            echo "→ $cmd"
            echo "$cmd"
            sleep 1
        done
        
        # Keep connection alive
        sleep 1
    ) | timeout 15 $QEMU \
        -drive file=$DISK,format=raw,if=ide \
        -m 16M \
        -serial stdio \
        -monitor none \
        -display none \
        2>&1 | tee /tmp/minidos_test_output.txt
    
    echo ""
}

# Test cases
if [ "$1" == "" ]; then
    # Default tests
    run_test "Help Command" "help"
    run_test "Version & Drives" "ver" "drives"
    run_test "Drive C: directory" "c:" "dir"
else
    # Custom test
    run_test "Custom Test" "$@"
fi
