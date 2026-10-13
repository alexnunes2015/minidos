#!/usr/bin/env python3
import argparse
import os
import re
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
APP_MARKER_RE = re.compile(
    r"APPBG100 pid=0x([0-9A-Fa-f]{8}) leader=0x([0-9A-Fa-f]{8}) "
    r"entry=0x([0-9A-Fa-f]{8}) base=0x([0-9A-Fa-f]{8}) bytes=0x([0-9A-Fa-f]{8})"
)


def run_host(cmd, *, env):
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(cmd)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def send_cmd(proc, cmd, patterns, timeout_s=8.0):
    send_line(proc, cmd)
    accepted = [f"Command: {cmd}"] + list(patterns)
    out, matched = read_until(proc, accepted, timeout_s=timeout_s)
    if not matched:
        raise RuntimeError(f"Timeout waiting for command acceptance: {cmd}")

    for pattern in patterns:
        if pattern in out:
            continue
        extra, matched = read_until(proc, [pattern], timeout_s=timeout_s)
        out += extra
        if matched != pattern:
            raise RuntimeError(f"Timeout waiting for {pattern} after {cmd}:\n{out}")
    return out


def wait_for_fault(proc, out, timeout_s=12.0):
    if "APPBG100" not in out:
        extra, matched = read_until(proc, ["APPBG100"], timeout_s=timeout_s)
        out += extra
        if matched != "APPBG100":
            raise RuntimeError(f"app task did not start:\n{out}")
    if "APPFLT900" not in out:
        extra, matched = read_until(proc, ["APPFLT900"], timeout_s=timeout_s)
        out += extra
        if matched != "APPFLT900":
            raise RuntimeError(f"app fault marker missing:\n{out}")
    match = APP_MARKER_RE.search(out)
    if not match:
        raise RuntimeError(f"APPBG100 schema missing:\n{out}")
    return out, tuple(int(value, 16) for value in match.groups())


def assert_no_task(proc, task_names):
    out = send_cmd(proc, "top 200 1", ["STATE"], timeout_s=12.0)
    lowered = out.lower()
    for task_name in task_names:
        if task_name.lower() in lowered:
            raise RuntimeError(f"terminated task still visible in top: {task_name}\n{out}")


def main():
    parser = argparse.ArgumentParser(
        description="Validate user app PID, group, stack, and physical-slot cleanup after faults"
    )
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--ready-timeout", type=float, default=30.0)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    image = resolve_disk_path(args.disk)
    if not os.path.exists(image):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    tool_env = dict(os.environ)
    tool_env["IMG_PATH"] = image
    for source, name in (
        ("external_apps/apps/oldmap/oldmap.c", "OLDMAP"),
        ("external_apps/apps/oldmap/oldmap.c", "OLDCOM"),
        ("tests/fixtures/faultgroup.c", "FLTGRP"),
    ):
        format_args = [] if name != "OLDCOM" else ["--format", "com"]
        run_host(["./external_apps/add_app.sh", *format_args, source, name], env=tool_env)

    proc = spawn_qemu(build_floppy_qemu_cmd(image, qemu=args.qemu, memory=args.memory))
    try:
        ready_log, matched = wait_for_shell_ready(
            proc,
            args.ready_timeout,
            echo=not args.quiet,
        )
        if not matched:
            raise RuntimeError("timeout waiting for shell readiness")
        dismiss_default_gui(proc, ready_log, 8.0, echo=not args.quiet)
        send_cmd(proc, "a:", ["Switched to drive A:"])

        baselines = {}
        for command, task_name in (("runbg oldmap", "OLDMAP"), ("runbg oldcom", "OLDCOM")):
            for iteration in range(7):
                out = send_cmd(proc, command, ["Started background app"], timeout_s=10.0)
                out, fields = wait_for_fault(proc, out)
                pid, leader_pid, _entry, phys_base, slot_bytes = fields
                if pid != leader_pid:
                    raise RuntimeError(f"faulted app was not its own group leader:\n{out}")
                baseline = baselines.setdefault(command, (pid, phys_base, slot_bytes))
                if (pid, phys_base, slot_bytes) != baseline:
                    raise RuntimeError(
                        f"cleanup did not reuse PID/physical slot for {command} iteration {iteration}: "
                        f"expected {baseline}, got {(pid, phys_base, slot_bytes)}\n{out}"
                    )
                assert_no_task(proc, [task_name])

        out = send_cmd(proc, "runbg fltgrp", ["Started background app"], timeout_s=10.0)
        out, _fields = wait_for_fault(proc, out)
        for marker in ("FGMAIN110", "FGCHILD100", "APPFLT900"):
            if marker not in out:
                raise RuntimeError(f"fault-group output missing {marker}:\n{out}")
        assert_no_task(proc, ["FLTGRP", "fault-worker"])

        out = send_cmd(proc, "runbg oldmap", ["Started background app"], timeout_s=10.0)
        out, fields = wait_for_fault(proc, out)
        if (fields[0], fields[3], fields[4]) != baselines["runbg oldmap"]:
            raise RuntimeError(f"group cleanup did not release the reusable slot:\n{out}")

        out = send_cmd(proc, "ver", ["MiniDOS Version 0.1"])
        if "MiniDOS Version 0.1" not in out:
            raise RuntimeError("shell did not resume after cleanup regression")

        print("PASS: repeated ELF/COM faults reuse PID and physical slots; faulted groups leave no visible tasks")
        return 0
    finally:
        terminate_process(proc)


if __name__ == "__main__":
    raise SystemExit(main())
