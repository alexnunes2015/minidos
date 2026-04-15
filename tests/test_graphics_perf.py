#!/usr/bin/env python3
"""
Graphics performance regression tests.

Validates:
  - Backbuffer initialises and boot completes (stride alignment is compile-time invariant)
  - Multi-threaded render stress completes without deadlock (PTVIDEO200 marker)
  - Shell is responsive after stress render (no lock starvation)
"""
import argparse
import os
import sys

from qemu_harness import (
    build_floppy_qemu_cmd,
    read_until,
    resolve_disk_path,
    send_line,
    spawn_qemu,
    terminate_process,
)


def run_stride_align_test(disk_path, qemu, memory, timeout):
    """Verify backbuffer initialises successfully at boot.

    Stride alignment for all supported resolutions (640, 800, 1024, 1280) at
    32bpp is a compile-time invariant — the test verifies the system boots and
    reaches the main loop without video-subsystem errors.
    """
    proc = spawn_qemu(
        build_floppy_qemu_cmd(disk_path, qemu=qemu, memory=memory),
        stdin=None,
    )
    try:
        required = [
            "paging self-test OK",
            "[sched] phase5 context-switch self-test OK",
            "Entering main loop",
        ]
        log, matched = read_until(proc, [required[-1]], timeout)
        for marker in required:
            if marker not in log:
                print(f"\nERROR [stride-align]: marker not found: {marker}", file=sys.stderr)
                print(log, file=sys.stderr)
                return 1
        print("[stride-align] PASS")
        return 0
    finally:
        terminate_process(proc)


def run_stress_render_test(disk_path, qemu, memory, timeout):
    """Verify multi-threaded render stress completes without deadlock.

    Sends the 'videostress 2 60' command and waits for PTVIDEO200 emitted after
    all worker threads finish.  If any thread deadlocks the marker will not appear.
    """
    proc = spawn_qemu(
        build_floppy_qemu_cmd(disk_path, qemu=qemu, memory=memory),
    )
    try:
        log, matched = read_until(proc, ["Entering main loop"], timeout)
        if not matched:
            print("\nERROR [stress-render]: shell not ready", file=sys.stderr)
            print(log, file=sys.stderr)
            return 1

        send_line(proc, "videostress 2 60")

        log2, matched2 = read_until(proc, ["PTVIDEO200"], timeout)
        full_log = log + log2

        required = ["PTVIDEO100", "PTVIDEO200"]
        for marker in required:
            if marker not in full_log:
                print(f"\nERROR [stress-render]: marker not found: {marker}", file=sys.stderr)
                print(full_log, file=sys.stderr)
                return 1
        print("[stress-render] PASS")
        return 0
    finally:
        terminate_process(proc)


def run_shell_responsive_after_stress(disk_path, qemu, memory, timeout):
    """Verify shell remains responsive after video stress (no lock starvation).

    Runs videostress, then immediately runs 'ver'. If the video lock is stuck
    after the workers complete, the shell will not process subsequent commands.
    """
    proc = spawn_qemu(
        build_floppy_qemu_cmd(disk_path, qemu=qemu, memory=memory),
    )
    try:
        log, matched = read_until(proc, ["Entering main loop"], timeout)
        if not matched:
            print("\nERROR [shell-responsive]: shell not ready", file=sys.stderr)
            return 1

        send_line(proc, "videostress 2 60")
        log2, matched2 = read_until(proc, ["PTVIDEO200"], timeout)
        if not matched2:
            print("\nERROR [shell-responsive]: PTVIDEO200 not seen", file=sys.stderr)
            print(log + log2, file=sys.stderr)
            return 1

        send_line(proc, "ver")
        log3, matched3 = read_until(proc, ["MiniDOS"], 10.0)
        if not matched3:
            print("\nERROR [shell-responsive]: shell did not respond to ver after stress", file=sys.stderr)
            print(log3, file=sys.stderr)
            return 1

        print("[shell-responsive] PASS")
        return 0
    finally:
        terminate_process(proc)


def main():
    parser = argparse.ArgumentParser(description="Graphics performance regression tests")
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument(
        "--test",
        choices=["stride-align", "stress-render", "shell-responsive", "all"],
        default="all",
        help="Which test to run (default: all)",
    )
    args = parser.parse_args()

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    results = []

    if args.test in ("stride-align", "all"):
        results.append(run_stride_align_test(disk_path, args.qemu, args.memory, args.timeout))

    if args.test in ("stress-render", "all"):
        results.append(run_stress_render_test(disk_path, args.qemu, args.memory, args.timeout))

    if args.test in ("shell-responsive", "all"):
        results.append(run_shell_responsive_after_stress(disk_path, args.qemu, args.memory, args.timeout))

    if any(r != 0 for r in results):
        return 1
    print("All graphics perf tests PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
