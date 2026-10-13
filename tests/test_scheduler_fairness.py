#!/usr/bin/env python3
import argparse
import os
import sys
import time

from qemu_harness import (
    build_floppy_qemu_cmd,
    dismiss_default_gui,
    read_until,
    resolve_disk_path,
    send_line,
    spawn_qemu,
    terminate_process,
    wait_for_shell_ready,
)


def send_accept(proc, command, timeout_s=8.0):
    accepted = [f"Command: {command}", "Bad command", "File not found", "Invalid drive"]
    send_line(proc, command)
    output, matched = read_until(proc, accepted, timeout_s=timeout_s)
    if not matched:
        send_line(proc, command)
        extra, matched = read_until(proc, accepted, timeout_s=timeout_s)
        output += extra
    if not matched:
        raise RuntimeError(f"Timeout waiting for command acceptance: {command}\n{output}")
    return output


def wait_for_all(proc, markers, timeout_s):
    deadline = time.time() + timeout_s
    output = ""
    remaining_markers = set(markers)
    while remaining_markers:
        remaining = max(0.1, deadline - time.time())
        extra, matched = read_until(proc, sorted(remaining_markers), timeout_s=remaining)
        output += extra
        remaining_markers = {marker for marker in remaining_markers if marker not in output}
        if not matched and remaining_markers:
            missing = ", ".join(sorted(remaining_markers))
            raise RuntimeError(f"Timeout waiting for scheduler markers {missing}\n{output}")
    return output


def run_round(proc, round_number):
    for app in ("ptcpu", "ptwait", "ptthrd"):
        send_accept(proc, f"runbg {app}")

    markers = [
        "PTCPU100 start",
        "PTWAIT100 start",
        "PTTHRD100 start",
        "PTTHRD110",
        "PTCPU190",
        "PTWAIT190",
        "PTTHRD190",
    ]
    wait_for_all(proc, markers, timeout_s=12.0)
    print(f"PASS: scheduler fairness round {round_number} completed")


def main():
    parser = argparse.ArgumentParser(description="Validate scheduler fairness under mixed workloads")
    parser.add_argument("--disk", default="minidos.img")
    args = parser.parse_args()

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    proc = spawn_qemu(build_floppy_qemu_cmd(disk_path))
    try:
        ready_log, matched = wait_for_shell_ready(proc, 30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        dismiss_default_gui(proc, ready_log, 8.0)
        send_accept(proc, "cd ptest")

        run_round(proc, 1)
        run_round(proc, 2)

        output = send_accept(proc, "top 200 1", timeout_s=12.0)
        for task_name in ("PTCPU", "PTWAIT", "PTTHRD", "worker"):
            if task_name in output:
                raise RuntimeError(f"completed scheduler round left task visible: {task_name}\n{output}")

        output = send_accept(proc, "ver")
        extra, matched = read_until(proc, ["MiniDOS Version 0.1"], timeout_s=8.0)
        output += extra
        if matched != "MiniDOS Version 0.1":
            raise RuntimeError(f"shell did not remain responsive after fairness test:\n{output}")

        print("PASS: scheduler fairness preserved mixed CPU, blocked, and child workloads")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
