#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys

from qemu_harness import (
    build_floppy_qemu_cmd,
    dismiss_default_gui,
    read_until,
    repo_root,
    resolve_disk_path,
    send_line,
    spawn_qemu,
    terminate_process,
    wait_for_shell_ready,
)

ROOT = repo_root()


def run_host(command, env):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(command)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def send_command(proc, command, patterns, timeout_s=12.0):
    send_line(proc, command)
    accepted = [f"Command: {command}"] + list(patterns)
    output, matched = read_until(proc, accepted, timeout_s=timeout_s)
    if not matched:
        send_line(proc, command)
        extra, matched = read_until(proc, accepted, timeout_s=timeout_s)
        output += extra
    if not matched:
        raise RuntimeError(f"Timeout waiting for command: {command}\n{output}")

    if not any(pattern in output for pattern in patterns):
        extra, matched = read_until(proc, patterns, timeout_s=timeout_s)
        output += extra
        if not matched:
            raise RuntimeError(f"Timeout waiting for patterns after {command}: {patterns}\n{output}")
    return output


def run_matrix(proc, command):
    output = send_command(proc, command, ["SYSC190", "SYSC900"], timeout_s=15.0)
    if "SYSC190" not in output or "SYSC900" in output:
        raise RuntimeError(f"syscall matrix failed for {command}:\n{output}")

    output = send_command(proc, "ver", ["MiniDOS Version 0.1"])
    if "MiniDOS Version 0.1" not in output:
        raise RuntimeError(f"shell did not resume after {command}:\n{output}")


def main():
    parser = argparse.ArgumentParser(description="Validate invalid user syscall arguments")
    parser.add_argument("--disk", default="minidos.img")
    args = parser.parse_args()

    image = resolve_disk_path(args.disk)
    if not os.path.exists(image):
        print(f"ERROR: disk image not found: {image}", file=sys.stderr)
        return 2

    env = dict(os.environ)
    env["IMG_PATH"] = image
    run_host(["./external_apps/add_app.sh", "tests/fixtures/syscall_matrix/syscall_matrix.c", "SYSCMAT"], env)
    run_host(["./external_apps/add_app.sh", "--format", "com", "tests/fixtures/syscall_matrix/syscall_matrix.c", "SYSCOM"], env)

    proc = spawn_qemu(build_floppy_qemu_cmd(image))
    try:
        ready_log, matched = wait_for_shell_ready(proc, 30.0)
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        dismiss_default_gui(proc, ready_log, 8.0)

        run_matrix(proc, "syscmat")
        run_matrix(proc, "run syscom")

        print("PASS: invalid syscall pointers, ranges, formats, and unknown calls are contained for ELF and COM")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
