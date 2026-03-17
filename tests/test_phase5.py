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
    parser = argparse.ArgumentParser(description="Validate scheduler phase5 runtime and stack guards")
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("--expect-guard", action="store_true")
    args = parser.parse_args()

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    proc = spawn_qemu(
        build_floppy_qemu_cmd(
            disk_path,
            qemu=args.qemu,
            memory=args.memory,
        ),
        stdin=None,
    )

    try:
        if args.expect_guard:
            required = [
                "SCHED100",
                "SCHED110",
                "SCHED120",
                "SCHED150",
                "SCHED900",
                "[paging] #PF detected",
            ]
        else:
            required = [
                "SCHED100",
                "SCHED110",
                "SCHED120",
                "SCHED190",
                "[sched] phase5 context-switch self-test OK",
                "Entering main loop",
            ]

        log, matched = read_until(proc, [required[-1]], args.timeout)
        if not matched:
            print(f"\nERROR: marker not found: {required[-1]}", file=sys.stderr)
            print(log, file=sys.stderr)
            return 1

        for marker in required:
            if marker in log:
                continue
            print(f"\nERROR: marker not found: {marker}", file=sys.stderr)
            print(log, file=sys.stderr)
            return 1

        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
