#!/usr/bin/env python3
import argparse
import os
import sys

from qemu_harness import (
    build_floppy_qemu_cmd,
    read_until,
    resolve_disk_path,
    spawn_qemu,
    terminate_process,
)


def main():
    parser = argparse.ArgumentParser(description="Validate paging boot and #PF diagnostics")
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("--expect-fault", action="store_true")
    args = parser.parse_args()

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    cmd = build_floppy_qemu_cmd(
        disk_path,
        qemu=args.qemu,
        memory=args.memory,
    )
    proc = spawn_qemu(cmd, stdin=None)
    try:
        if args.expect_fault:
            _, matched = read_until(
                proc,
                ["[paging] #PF detected", "[paging] CR2=0x00900000"],
                args.timeout,
            )
            return 0 if matched else 1

        required = [
            "[paging] init",
            "[paging] enabled",
            "paging self-test OK",
            "Entering main loop",
        ]
        for marker in required:
            _, matched = read_until(proc, [marker], args.timeout)
            if not matched:
                print(f"\nERROR: marker not found: {marker}", file=sys.stderr)
                return 1
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
