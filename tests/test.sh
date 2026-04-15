#!/bin/bash

# MiniDOS Test Suite
# Allows automatic testing of shell commands

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK="$ROOT_DIR/minidos.img"
OUTPUT="/tmp/minidos_test.log"
READY_TIMEOUT="${READY_TIMEOUT:-30}"
POST_PM_DELAY="${POST_PM_DELAY:-2}"
CMD_TIMEOUT="${CMD_TIMEOUT:-10}"
PYTHON="${PYTHON:-python3}"

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
    
    # Run via serial, waiting for shell readiness before sending commands
    $PYTHON "$ROOT_DIR/tests/test_serial.py" \
        --ready-timeout "$READY_TIMEOUT" \
        --cmd-timeout "$CMD_TIMEOUT" \
        "${commands[@]}" \
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
        run_test "Directory Listing A:" "a:" "dir"
        ;;

    rmdir)
        run_test "Create and Remove Empty Directory" "mkdir TESTDIR" "rmdir TESTDIR"
        ;;
    
    type)
        run_test "Type File" "a:" "type hello.txt"
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
        echo "  rmdir   - Test create/remove empty directory"
        echo "  type    - Test file viewing"
        echo "  shell   - Test multiple commands"
        exit 1
        ;;
esac

# Show test output location
echo -e "${GREEN}Test output saved to: $OUTPUT${NC}"
