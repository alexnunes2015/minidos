#!/usr/bin/env python3
import argparse
import os
import sys

from qemu_harness import (
    build_floppy_qemu_cmd,
    dismiss_default_gui,
    read_until,
    resolve_disk_path,
    send_line,
    spawn_qemu,
    terminate_process,
    wait_for_marker,
    wait_for_shell_ready,
)


def send_accept(proc, cmd, timeout_s=8.0):
    accepted_patterns = [f"Command: {cmd}", "Bad command", "File not found", "Invalid ELF"]

    send_line(proc, cmd)
    out, matched = read_until(proc, accepted_patterns, timeout_s=timeout_s)
    if not matched:
        send_line(proc, cmd)
        extra, matched = read_until(proc, accepted_patterns, timeout_s=timeout_s)
        out += extra
        if not matched:
            raise RuntimeError(f"Timeout waiting for command acceptance: {cmd}")
    return out


def send_cmd(proc, cmd, patterns, timeout_s=12.0):
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
    parser = argparse.ArgumentParser(description="Validate dynamic userland slots with an ELF image over 1 MiB")
    parser.add_argument("--disk", default="minidos.img")
    args = parser.parse_args()

    img = resolve_disk_path(args.disk)
    if not os.path.exists(img):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    proc = spawn_qemu(build_floppy_qemu_cmd(img))

    try:
        ready_log, matched = wait_for_shell_ready(proc, 30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        dismiss_default_gui(proc, ready_log, 8.0, echo=True)

        send_accept(proc, "cd ptest")
        out = send_cmd(proc, "ptbig", ["PTBIG100", "PTBIG190"], timeout_s=20.0)
        if "PTBIG900" in out or "PTBIG901" in out:
            raise RuntimeError(f"PTBIG reported failure:\n{out}")

        if "APPRET001" not in out:
            wait_for_marker(proc, "APPRET001", 20.0)
        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after PTBIG")

        print("PASS: large ELF mapped and returned successfully")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
