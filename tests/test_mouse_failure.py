#!/usr/bin/env python3
import argparse
import os
import sys

from qemu_harness import (
    build_floppy_qemu_cmd,
    resolve_disk_path,
    spawn_qemu,
    terminate_process,
    wait_for_marker_sequence,
)


def main():
    parser = argparse.ArgumentParser(
        description="Verify forced mouse initialization failure is observable and non-fatal."
    )
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--ready-timeout", type=float, default=30.0)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    proc = None
    try:
        proc = spawn_qemu(
            build_floppy_qemu_cmd(
                disk_path,
                qemu=args.qemu,
                memory=args.memory,
            )
        )
        log, matched = wait_for_marker_sequence(
            proc,
            [["MOUSE201"], ["SHELL100"]],
            args.ready_timeout,
            echo=not args.quiet,
        )
        if not matched:
            print("ERROR: forced mouse failure or shell marker missing", file=sys.stderr)
            print(log, file=sys.stderr)
            return 1
        if "[mouse] PS/2 mouse enabled on IRQ12" in log:
            print("ERROR: mouse announced as enabled after initialization failure", file=sys.stderr)
            return 1
        if "[mouse] first packet received" in log:
            print("ERROR: mouse packet was accepted after initialization failure", file=sys.stderr)
            return 1

        print("PASS: MOUSE201 was observable, mouse stayed disabled, and shell continued")
        return 0
    finally:
        if proc:
            terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
