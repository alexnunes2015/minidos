#!/bin/bash

# Test script to send commands to MiniDOS shell via QEMU
# Usage: ./test_shell.sh "cmd1" "cmd2" "cmd3" ...

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU="qemu-system-i386"
DISK="$ROOT_DIR/minidos.img"
TIMEOUT=2

# Function to send commands to QEMU
send_commands() {
    local commands="$@"
    
    # Start QEMU and send commands with delays
    (
        # Wait for shell prompt
        sleep 1
        
        # Send each command
        while IFS= read -r cmd; do
            if [ -n "$cmd" ]; then
                echo "[TEST] Sending: $cmd" >&2
                echo "$cmd"
                sleep 0.5
            fi
        done <<< "$commands"
        
        # Keep alive briefly
        sleep 1
    ) | timeout $TIMEOUT $QEMU -drive "file=$DISK,format=raw,if=ide,index=0" -boot c -m 16M -nographic -serial stdio 2>&1 || true
}

# Test cases
if [ $# -eq 0 ]; then
    echo "Usage: $0 \"cmd1\" \"cmd2\" \"cmd3\" ..."
    echo ""
    echo "Examples:"
    echo "  $0 \"help\""
    echo "  $0 \"ver\" \"drives\" \"dir\""
    echo "  $0 \"c:\" \"dir\""
    exit 1
fi

echo "=== MiniDOS Shell Test ==="
echo "Commands to send: $@"
echo ""

# Join all arguments with newlines
commands=$(printf '%s\n' "$@")
send_commands "$commands"
