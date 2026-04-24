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

def wait_for_patterns(proc, patterns, timeout_s, *, echo=True):
    deadline = time.time() + timeout_s
    buf = ""

    for pattern in patterns:
        remaining = max(0.1, deadline - time.time())
        extra, matched = read_until(proc, [pattern], timeout_s=remaining, echo=echo)
        buf += extra
        if matched != pattern:
            raise RuntimeError(f"Timeout waiting for pattern: {pattern}")

    return buf


def send_accept(proc, cmd, timeout_s=8.0):
    accepted_patterns = [f"Command: {cmd}", "Bad command", "File not found", "Invalid drive"]

    send_line(proc, cmd)
    out, matched = read_until(proc, accepted_patterns, timeout_s=timeout_s)
    if not matched:
        send_line(proc, cmd)
        extra, matched = read_until(proc, accepted_patterns, timeout_s=timeout_s)
        out += extra
        if not matched:
            raise RuntimeError(f"Timeout waiting for command acceptance: {cmd}")
    return out


def send_cmd(proc, cmd, patterns, timeout_s=8.0):
    out = send_accept(proc, cmd, timeout_s=timeout_s)
    for pattern in patterns:
        if pattern in out:
            continue
        extra, matched = read_until(proc, [pattern], timeout_s=timeout_s)
        out += extra
        if matched != pattern:
            raise RuntimeError(f"Timeout waiting for pattern {pattern} after command: {cmd}")
    return out


def main():
    parser = argparse.ArgumentParser(description="Validate background ELF multitasking")
    parser.add_argument("--disk", default="minidos.img")
    args = parser.parse_args()

    img = resolve_disk_path(args.disk)
    if not os.path.exists(img):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    proc = spawn_qemu(build_floppy_qemu_cmd(img))

    try:
        ready_deadline = time.time() + 30.0
        ready_log, matched = wait_for_shell_ready(proc, 30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        if matched != "Entering main loop" and "Entering main loop" not in ready_log:
            remaining = max(0.1, ready_deadline - time.time())
            extra_log, matched = read_until(proc, ["Entering main loop"], timeout_s=remaining)
            ready_log += extra_log
            if not matched:
                raise RuntimeError("shell banner appeared before main loop became ready")
        dismiss_default_gui(proc, ready_log, 8.0, echo=True)

        send_accept(proc, "cd ptest")

        send_cmd(proc, "runbg ptcpu", ["Started background app PTCPU pid="])
        send_cmd(proc, "runbg ptwait", ["Started background app PTWAIT pid="])
        send_cmd(proc, "runbg ptthrd", ["Started background app PTTHRD pid="])
        cpu_pid = 3
        wait_pid = 4
        thrd_pid = 5

        wait_for_patterns(proc, ["PTTHRD100", "PTTHRD101", "PTTHRD110"], 10.0)

        out = send_cmd(proc, "top 200 1", ["PTCPU", "PTWAIT", "PTTHRD", "worker", "PTTHRD.ELF"], timeout_s=12.0)
        for token in ["PTCPU", "PTWAIT", "PTTHRD", "worker", "PTCPU.ELF", "PTWAIT.ELF", "PTTHRD.ELF"]:
            if token not in out:
                raise RuntimeError(f"top output missing {token}:\n{out}")

        out = send_cmd(proc, f"kill {thrd_pid}", ["Background app group stopped"])
        if "Background app group stopped" not in out:
            raise RuntimeError("kill did not report success")

        out = send_cmd(proc, "top 200 1", ["PTCPU", "PTWAIT"], timeout_s=12.0)
        if "PTTHRD" in out or "worker" in out:
            raise RuntimeError(f"killed app group still visible in top:\n{out}")

        send_cmd(proc, f"kill {cpu_pid}", ["Background app group stopped"])
        send_cmd(proc, f"kill {wait_pid}", ["Background app group stopped"])
        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after multitask test")

        print("PASS: background ELF multitasking works with grouped child threads")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
