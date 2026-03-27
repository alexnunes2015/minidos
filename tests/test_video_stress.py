#!/usr/bin/env python3
import argparse
import os
import sys
import time

from qemu_harness import (
    build_floppy_qemu_cmd,
    read_until,
    resolve_disk_path,
    send_line,
    spawn_qemu,
    terminate_process,
    wait_for_shell_ready,
)


def main():
    parser = argparse.ArgumentParser(description="Stress-test the video driver via the videostress command")
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=120)
    args = parser.parse_args()

    img = resolve_disk_path(args.disk)
    if not os.path.exists(img):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    if args.workers <= 0 or args.iterations <= 0:
        print("ERROR: workers and iterations must be positive", file=sys.stderr)
        return 1

    proc = spawn_qemu(build_floppy_qemu_cmd(img))
    try:
        ready_log, matched = wait_for_shell_ready(proc, 30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")

        command = f"videostress {args.workers} {args.iterations}"
        send_line(proc, command)

        deadline = time.time() + 60.0
        seen_start = False
        max_workers = args.workers
        completed = 0
        limited = False

        while time.time() < deadline:
            remaining = deadline - time.time()
            buf, marker = read_until(proc, ["PTVIDEO100", "PTVIDEO200", "PTVIDEO110"], min(remaining, 3.0))
            if not buf and marker is None:
                continue
            if "PTVIDEO100" in buf:
                seen_start = True
            if "PTVIDEO110" in buf:
                limited = True
            completed += buf.count("PTVIDEO200")
            if completed >= max_workers:
                break

        if not seen_start:
            raise RuntimeError("videostress did not emit PTVIDEO100 marker")
        if completed < max_workers:
            raise RuntimeError(f"videostress completed {completed}/{max_workers} workers")

        print("PASS: videostress completed", "(limited slots)" if limited else "")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
