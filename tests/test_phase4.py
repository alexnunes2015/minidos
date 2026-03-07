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
    _, accepted = read_until(proc, [f"Command: {cmd}"], timeout_s=8.0)
    if not accepted:
        raise RuntimeError(f"Timeout waiting for command acceptance: {cmd}")

    out, matched = read_until(proc, patterns, timeout_s=8.0)
    if not matched:
        raise RuntimeError(f"Timeout waiting for patterns: {patterns}")
    return out


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

        print("PASS: phase4 ELF apps executed successfully")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
