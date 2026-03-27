#!/usr/bin/env python3
import argparse
import os
import shutil
import sys
import tempfile
import time

from qemu_harness import (
    build_floppy_qemu_cmd,
    read_until,
    resolve_disk_path,
    spawn_qemu,
    terminate_process,
    wait_for_marker_sequence,
)

ROOT_DIR = os.getcwd()

BOOT_SECTORS_PER_FAT_OFFSET = 0x16


def prepare_degraded_disk(src_path):
    temp_file = tempfile.NamedTemporaryFile(prefix="minidos-storage-", suffix=".img", delete=False)
    temp_path = temp_file.name
    temp_file.close()
    shutil.copy(src_path, temp_path)
    with open(temp_path, "r+b") as image:
        image.seek(BOOT_SECTORS_PER_FAT_OFFSET)
        image.write(b"\x00\x00")
        image.flush()
    return temp_path


def main():
    parser = argparse.ArgumentParser(description="Verify storage failure emits DISK021 and aborts cleanly.")
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--ready-timeout", type=float, default=30.0)
    parser.add_argument("--cmd-timeout", type=float, default=10.0)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    tmp_disk = prepare_degraded_disk(disk_path)

    proc = None
    try:
        qemu_cmd = build_floppy_qemu_cmd(
            tmp_disk,
            qemu=args.qemu,
            memory=args.memory,
        )
        proc = spawn_qemu(qemu_cmd)

        stop_marker = "STOP 0x00000004"
        required_groups = [[stop_marker]]
        log, matched = wait_for_marker_sequence(
            proc,
            required_groups,
            args.ready_timeout,
            echo=not args.quiet,
        )
        if not matched or matched != stop_marker:
            print("ERROR: storage failure STOP marker not emitted", file=sys.stderr)
            print(log, file=sys.stderr)
            return 1
        if "DISK021" not in log:
            print("ERROR: DISK021 missing from serial output", file=sys.stderr)
            print(log, file=sys.stderr)
            return 1

        print("PASS: storage failure emitted DISK021 and halted as expected (STOP 0x00000004)")
        return 0
    finally:
        if proc:
            terminate_process(proc)
        if os.path.exists(tmp_disk):
            os.unlink(tmp_disk)


if __name__ == "__main__":
    raise SystemExit(main())
