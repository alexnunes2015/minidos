#!/usr/bin/env python3
import os
import subprocess
import sys

from qemu_harness import (
    build_floppy_qemu_cmd,
    read_until,
    repo_root,
    send_line,
    spawn_qemu,
    terminate_process,
)

ROOT = repo_root()
IMG = os.path.join(ROOT, "minidos.img")


def run_host(cmd):
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(cmd)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )

def send_cmd(proc, cmd, patterns):
    send_line(proc, cmd)
    accepted_patterns = [f"Command: {cmd}"] + list(patterns)
    out, matched = read_until(proc, accepted_patterns, timeout_s=8.0)
    if not matched:
        raise RuntimeError(f"Timeout waiting for command acceptance: {cmd}")

    if not any(pattern in out for pattern in patterns):
        extra, matched = read_until(proc, patterns, timeout_s=8.0)
        if not matched:
            raise RuntimeError(f"Timeout waiting for patterns: {patterns}")
        out += extra

    return out


def send_text(proc, text):
    if proc.stdin is None:
        raise RuntimeError("QEMU process has no stdin pipe")
    proc.stdin.write(text.encode("latin1"))
    proc.stdin.flush()


def wait_for_marker(proc, marker, timeout_s):
    _, matched = read_until(proc, [marker], timeout_s=timeout_s)
    if not matched:
        raise RuntimeError(f"Timeout waiting for marker: {marker}")


def main():
    if not os.path.exists(IMG):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    run_host(["./external_apps/add_app.sh", "external_apps/templates/hello_elf.c", "HELLOELF"])
    run_host(["./external_apps/add_app.sh", "external_apps/templates/stat_elf.c", "STATELF"])

    qemu_cmd = build_floppy_qemu_cmd(IMG)
    proc = spawn_qemu(qemu_cmd)

    try:
        _, matched = read_until(proc, ["MiniDOS Shell Ready", "Entering main loop"], timeout_s=30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        send_cmd(proc, "elfls", ["HELLOELF.ELF"])
        out, matched = read_until(proc, ["STATELF"], timeout_s=8.0)
        if not matched:
            raise RuntimeError("timeout waiting for STATELF listing")
        if "STATELF" not in out:
            raise RuntimeError("elfls output missing expected apps")

        out = send_cmd(proc, "hello_elf", ["hello_elf: running"])
        if "hello_elf: running" not in out:
            raise RuntimeError("hello_elf output missing expected line")

        out = send_cmd(proc, "run stat_elf", ["stat_elf:"])
        if "stat_elf:" not in out:
            raise RuntimeError("stat_elf output missing expected line")

        send_cmd(proc, "dosshell", ["Executing DOSSHELL.ELF..."])
        wait_for_marker(proc, "APPIN001", 20.0)
        send_text(proc, "q")
        wait_for_marker(proc, "APPRET001", 20.0)
        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after DOSSHELL input")

        send_cmd(proc, "edit", ["Executing EDIT.ELF..."])
        wait_for_marker(proc, "APPIN001", 20.0)
        send_text(proc, "\x1b")
        wait_for_marker(proc, "APPRET001", 20.0)
        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after EDIT input")

        out = send_cmd(proc, "ed", ["Bad command or file name"])
        if "Bad command or file name" not in out:
            raise RuntimeError("short app prefix unexpectedly matched EDIT")

        print("PASS: phase4 ELF apps executed successfully")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
