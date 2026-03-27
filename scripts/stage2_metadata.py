#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        fail("Usage: stage2_metadata.py <stage2.lst> <stage2.meta>")

    lst_path = Path(sys.argv[1])
    meta_path = Path(sys.argv[2])

    if not lst_path.exists():
        fail(f"{lst_path} does not exist")

    offset_value = None

    with lst_path.open() as fh:
        for line in fh:
            if "kernel_sectors:" not in line:
                continue
            parts = line.strip().split()
            if len(parts) < 2:
                continue
            offset_value = int(parts[1], 16)
            break

    if offset_value is None:
        fail("kernel_sectors label not found in stage2.lst")

    meta_path.parent.mkdir(parents=True, exist_ok=True)
    meta_path.write_text(json.dumps({"kernel_sectors_offset": hex(offset_value)}))
    print(f"Generated stage2 metadata with kernel_sectors offset {hex(offset_value)}")


if __name__ == "__main__":
    main()
