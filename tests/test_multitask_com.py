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
    wait_for_shell_ready,
)

ROOT = repo_root()


def run_host(cmd, *, env=None):
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(cmd)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
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
    parser = argparse.ArgumentParser(description="Validate background COM multitasking")
    parser.add_argument("--disk", default="minidos.img")
    args = parser.parse_args()

    img = resolve_disk_path(args.disk)
    if not os.path.exists(img):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    tool_env = dict(os.environ)
    tool_env["IMG_PATH"] = img
    run_host(["./external_apps/add_app.sh", "--format", "com", "external_apps/apps/ptcpu/ptcpu.c", "CMCPU"], env=tool_env)
    run_host(["./external_apps/add_app.sh", "--format", "com", "external_apps/apps/ptwait/ptwait.c", "CMWAIT"], env=tool_env)
    run_host(["./external_apps/add_app.sh", "--format", "com", "external_apps/apps/ptthrd/ptthrd.c", "CMTHRD"], env=tool_env)

    proc = spawn_qemu(build_floppy_qemu_cmd(img))

    try:
        ready_deadline = time.time() + 30.0
        ready_log, matched = wait_for_shell_ready(proc, 30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        if matched != "Entering main loop":
            remaining = max(0.1, ready_deadline - time.time())
            extra_log, matched = read_until(proc, ["Entering main loop"], timeout_s=remaining)
            ready_log += extra_log
            if not matched:
                raise RuntimeError("shell banner appeared before main loop became ready")
        dismiss_default_gui(proc, ready_log, 8.0, echo=True)

        send_cmd(proc, "runbg cmcpu", ["Started background app CMCPU pid="])
        send_cmd(proc, "runbg cmwait", ["Started background app CMWAIT pid="])
        send_cmd(proc, "runbg cmthrd", ["Started background app CMTHRD pid="])
        cpu_pid = 3
        wait_pid = 4
        thrd_pid = 5

        wait_for_patterns(proc, ["PTTHRD100", "PTTHRD101", "PTTHRD110"], 10.0)

        out = send_cmd(proc, "top 200 1", ["CMCPU", "CMWAIT", "CMTHRD", "worker", "CMTHRD.COM"], timeout_s=12.0)
        for token in ["CMCPU", "CMWAIT", "CMTHRD", "worker", "CMCPU.COM", "CMWAIT.COM", "CMTHRD.COM"]:
            if token not in out:
                raise RuntimeError(f"top output missing {token}:\n{out}")

        out = send_cmd(proc, f"kill {thrd_pid}", ["Background app group stopped"])
        if "Background app group stopped" not in out:
            raise RuntimeError("kill did not report success")

        out = send_cmd(proc, "top 200 1", ["CMCPU", "CMWAIT"], timeout_s=12.0)
        if "CMTHRD" in out or "worker" in out:
            raise RuntimeError(f"killed app group still visible in top:\n{out}")

        send_cmd(proc, f"kill {cpu_pid}", ["Background app group stopped"])
        send_cmd(proc, f"kill {wait_pid}", ["Background app group stopped"])
        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after COM multitask test")

        print("PASS: background COM multitasking works with grouped child threads")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
