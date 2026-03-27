#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
import time

from qemu_harness import (
    build_floppy_qemu_cmd,
    dismiss_default_gui,
    read_until,
    resolve_disk_path,
    repo_root,
    send_line,
    spawn_qemu,
    terminate_process,
    wait_for_marker,
    wait_for_shell_ready,
)

ROOT = repo_root()


def run_host(cmd, *, env=None):
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(cmd)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )

def send_cmd(proc, cmd, patterns, timeout_s=8.0):
    send_line(proc, cmd)
    accepted_patterns = [f"Command: {cmd}"] + list(patterns)
    out, matched = read_until(proc, accepted_patterns, timeout_s=timeout_s)
    if not matched:
        send_line(proc, cmd)
        extra, matched = read_until(proc, accepted_patterns, timeout_s=timeout_s)
        out += extra
        if not matched:
            raise RuntimeError(f"Timeout waiting for command acceptance: {cmd}")

    if not any(pattern in out for pattern in patterns):
        extra, matched = read_until(proc, patterns, timeout_s=timeout_s)
        if not matched:
            raise RuntimeError(f"Timeout waiting for patterns: {patterns}")
        out += extra

    return out


def send_text(proc, text):
    if proc.stdin is None:
        raise RuntimeError("QEMU process has no stdin pipe")
    proc.stdin.write(text.encode("latin1"))
    proc.stdin.flush()


def main():
    parser = argparse.ArgumentParser(description="Validate foreground ELF execution path")
    parser.add_argument("--disk", default="minidos.img")
    args = parser.parse_args()

    img = resolve_disk_path(args.disk)
    if not os.path.exists(img):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    tool_env = dict(os.environ)
    tool_env["IMG_PATH"] = img
    run_host(["./external_apps/add_app.sh", "external_apps/apps/hello_elf/hello_elf.c", "HELLOELF"], env=tool_env)
    run_host(["./external_apps/add_app.sh", "external_apps/apps/stat_elf/stat_elf.c", "STATELF"], env=tool_env)
    run_host(["./external_apps/add_app.sh", "external_apps/apps/stress/stress.c", "STRESS"], env=tool_env)

    qemu_cmd = build_floppy_qemu_cmd(img)
    proc = spawn_qemu(qemu_cmd)

    try:
        ready_log, matched = wait_for_shell_ready(proc, 30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        ready_log, _ = dismiss_default_gui(proc, ready_log, 8.0, echo=True)
        out = send_cmd(proc, "elfls", ["STRESS"])
        if "HELLOELF.ELF" not in out or "STATELF" not in out or "STRESS" not in out:
            raise RuntimeError("elfls output missing expected apps")

        out = send_cmd(proc, "hello_elf", ["hello_elf: running"])
        if "hello_elf: running" not in out:
            raise RuntimeError("hello_elf output missing expected line")

        out = send_cmd(proc, "run stat_elf", ["stat_elf:"])
        if "stat_elf:" not in out:
            raise RuntimeError("stat_elf output missing expected line")

        out = send_cmd(proc, "stress", ["STRS190", "STRS900"], timeout_s=25.0)
        if "STRS900" in out and "STRS190" not in out:
            raise RuntimeError(f"stress app reported failure:\n{out}")
        if "STRS190" not in out:
            raise RuntimeError("stress app did not reach pass marker")
        wait_for_marker(proc, "APPRET001", 20.0)
        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after STRESS")

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
