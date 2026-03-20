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


def run_foreground_app(proc, cmd, patterns, timeout_s=10.0):
    out = send_cmd(proc, cmd, patterns, timeout_s=timeout_s)
    extra, matched = read_until(proc, ["APPRET001"], timeout_s=timeout_s)
    out += extra
    if matched != "APPRET001":
        raise RuntimeError(f"Timeout waiting for APPRET001 after {cmd}")
    return out


def main():
    parser = argparse.ArgumentParser(description="Validate user/kernel isolation for ELF and COM apps")
    parser.add_argument("--disk", default="minidos.img")
    args = parser.parse_args()

    img = resolve_disk_path(args.disk)
    if not os.path.exists(img):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    tool_env = dict(os.environ)
    tool_env["IMG_PATH"] = img
    run_host(["./external_apps/add_app.sh", "external_apps/templates/badptr.c", "BADPTR"], env=tool_env)
    run_host(["./external_apps/add_app.sh", "external_apps/templates/usrfault.c", "USRFAULT"], env=tool_env)
    run_host(["./external_apps/add_app.sh", "--format", "com", "external_apps/templates/badptr.c", "BADCOM"], env=tool_env)
    run_host(["./external_apps/add_app.sh", "--format", "com", "external_apps/templates/usrfault.c", "USRFCOM"], env=tool_env)

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

        out = send_cmd(proc, "a:", ["Switched to drive A:"])
        if "Switched to drive A:" not in out:
            raise RuntimeError("failed to switch to drive A:")

        out = send_cmd(proc, "elfls", ["BADPTR"])
        extra, matched = read_until(proc, ["USRFAULT"], timeout_s=8.0)
        out += extra
        if matched != "USRFAULT":
            raise RuntimeError("elfls did not list USRFAULT")
        if "BADPTR" not in out or "USRFAULT" not in out:
            raise RuntimeError(f"elfls output missing isolation apps:\n{out}")

        out = run_foreground_app(proc, "badptr", ["BADP190", "BADP900"], timeout_s=12.0)
        if "BADP190" not in out or "BADP900" in out:
            raise RuntimeError(f"badptr did not reject the kernel pointer:\n{out}")

        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after badptr")

        out = run_foreground_app(proc, "usrfault", ["USRF100", "[paging] #PF detected"], timeout_s=15.0)
        for marker in ["USRF100", "APPFLT900", "[paging] #PF detected", "CR2=0x00010000", "mode=user", "APPRET001"]:
            if marker not in out:
                raise RuntimeError(f"usrfault output missing {marker}:\n{out}")

        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after usrfault")

        out = run_foreground_app(proc, "run badcom", ["BADP190", "BADP900"], timeout_s=12.0)
        if "BADP190" not in out or "BADP900" in out:
            raise RuntimeError(f"badcom did not reject the kernel pointer:\n{out}")

        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after badcom")

        out = run_foreground_app(proc, "usrfcom", ["USRF100", "[paging] #PF detected"], timeout_s=15.0)
        for marker in ["USRF100", "APPFLT900", "[paging] #PF detected", "CR2=0x00010000", "mode=user", "APPRET001"]:
            if marker not in out:
                raise RuntimeError(f"usrfcom output missing {marker}:\n{out}")

        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after usrfcom")

        print("PASS: ELF and COM apps stay isolated from kernel memory and bad user pointers")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
