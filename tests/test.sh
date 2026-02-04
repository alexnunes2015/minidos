#!/bin/bash

# MiniDOS Test Suite
# Allows automatic testing of shell commands

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="$ROOT_DIR/minidos.img"
QEMU="qemu-system-i386"
OUTPUT="/tmp/minidos_test.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check disk exists
if [ ! -f "$DISK" ]; then
    echo -e "${RED}Error: $DISK not found${NC}"
    exit 1
fi

# Test function
run_test() {
    local name="$1"
    shift
    local commands=("$@")
    
    echo -e "${YELLOW}═══════════════════════════════════${NC}"
    echo -e "${YELLOW}TEST: $name${NC}"
    echo -e "${YELLOW}═══════════════════════════════════${NC}"
    
    # Build command input
    input=""
    for cmd in "${commands[@]}"; do
        input+="$cmd"$'\n'
    done
    
    # Run QEMU with input
    echo "$input" | timeout 10 $QEMU \
        -drive "file=$DISK,format=raw,if=ide" \
        -m 16M \
        -serial stdio \
        -monitor none \
        -display none \
        2>&1 | tee "$OUTPUT"
    
    echo ""
}

# Available tests
case "${1:-help}" in
    help)
        run_test "Help Command" "help"
        ;;
    
    ver)
        run_test "Version Command" "ver"
        ;;
    
    drives)
        run_test "List Drives" "drives"
        ;;
    
    dir)
        run_test "Directory Listing C:" "c:" "dir"
        ;;
    
    type)
        run_test "Type File" "c:" "type hello.txt"
        ;;
    
    shell)
        run_test "Multiple Commands" "ver" "drives" "help"
        ;;
    
    *)
        echo "Usage: $0 [test_name]"
        echo ""
        echo "Available tests:"
        echo "  help    - Test help command"
        echo "  ver     - Test version command"
        echo "  drives  - Test drives command"
        echo "  dir     - Test directory listing"
        echo "  type    - Test file viewing"
        echo "  shell   - Test multiple commands"
        exit 1
        ;;
esac

# Show test output location
echo -e "${GREEN}Test output saved to: $OUTPUT${NC}"
