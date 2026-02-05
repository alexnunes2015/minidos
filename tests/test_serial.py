#!/usr/bin/env python3
import argparse
import os
import select
import subprocess
import sys
import time


def _read_until(proc, patterns, timeout_s, echo=True, max_buf=20000):
    deadline = time.time() + timeout_s
    buf = ""
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if not ready:
            continue
        data = os.read(proc.stdout.fileno(), 4096)
        if not data:
            break
        text = data.decode(errors="ignore")
        if echo:
            sys.stdout.write(text)
            sys.stdout.flush()
        buf += text
        if len(buf) > max_buf:
            buf = buf[-max_buf:]
        for pat in patterns:
            if pat in buf:
                return pat
    return None


def _send_line(proc, line):
    proc.stdin.write((line + "\r").encode())
    proc.stdin.flush()


def main():
    parser = argparse.ArgumentParser(
        description="Run MiniDOS commands via serial and wait for readiness."
    )
    parser.add_argument(
        "commands", nargs="*", help="Commands to send (e.g., ver drives dir)."
    )
    parser.add_argument(
        "--disk",
        default="minidos.img",
        help="Disk image to boot (default: minidos.img).",
    )
    parser.add_argument(
        "--qemu",
        default="qemu-system-i386",
        help="QEMU binary (default: qemu-system-i386).",
    )
    parser.add_argument(
        "--memory",
        default="16M",
        help="QEMU memory size (default: 16M).",
    )
    parser.add_argument(
        "--ready-timeout",
        type=float,
        default=30.0,
        help="Seconds to wait for shell readiness (default: 30).",
    )
    parser.add_argument(
        "--post-pm-delay",
        type=float,
        default=2.0,
        help="Seconds to wait after PM message before sending commands (default: 2).",
    )
    parser.add_argument(
        "--cmd-timeout",
        type=float,
        default=8.0,
        help="Seconds to wait for each command to be accepted (default: 8).",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Do not echo QEMU output while waiting.",
    )
    parser.add_argument(
        "--no-assert",
        action="store_true",
        help="Disable output assertions for known commands.",
    )
    args = parser.parse_args()

    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    disk_path = args.disk
    if not os.path.isabs(disk_path):
        disk_path = os.path.join(root_dir, disk_path)

    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    qemu_cmd = [
        args.qemu,
        "-drive",
        f"file={disk_path},format=raw,if=ide",
        "-m",
        args.memory,
        "-serial",
        "stdio",
        "-monitor",
        "none",
        "-display",
        "none",
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    try:
        pm_marker = "[Stage2] Entering PM..."
        kernel_ready = [
            "=== MiniDOS Kernel Starting ===",
            "System Memory:",
            "Initializing disk driver...",
            "Starting shell...",
            "Entering main loop",
            "MiniDOS Shell Ready",
            "MiniDOS v0.1 Kernel Started",
        ]
        matched = _read_until(
            proc, kernel_ready, args.ready_timeout, echo=not args.quiet
        )
        if not matched:
            # Fallback: if kernel messages never show, try after PM.
            matched = _read_until(
                proc, [pm_marker], args.ready_timeout, echo=not args.quiet
            )
            if not matched:
                print("\nERROR: timeout waiting for shell readiness.", file=sys.stderr)
                return 1
            time.sleep(args.post_pm_delay)

        expected_map = {
            "help": ["Available commands:"],
            "ver": ["MiniDOS Version 0.1"],
            "drives": ["Available drives:"],
            "dir": ["Directory of "],
            "mem": ["System Memory:"],
            "c:": ["Switched to drive"],
            "d:": ["Switched to drive"],
            "e:": ["Switched to drive"],
        }

        commands = list(args.commands)
        idx = 0
        if commands:
            # Send first command early so serial input is already pending.
            _send_line(proc, commands[0])
            idx = 1
            cmd = commands[0]
            accepted_patterns = [
                f"Command: {cmd}",
                "Bad command",
                "File not found",
                "Invalid drive",
            ]
            ok = _read_until(
                proc, accepted_patterns, args.cmd_timeout, echo=not args.quiet
            )
            if not ok:
                _send_line(proc, cmd)
                ok = _read_until(
                    proc, accepted_patterns, args.cmd_timeout, echo=not args.quiet
                )
                if not ok:
                    print(
                        f"\nERROR: timeout waiting for command acceptance: {cmd}",
                        file=sys.stderr,
                    )
                    return 1
            if not args.no_assert and cmd in expected_map:
                ok = _read_until(
                    proc,
                    expected_map[cmd],
                    args.cmd_timeout,
                    echo=not args.quiet,
                )
                if not ok:
                    print(
                        f"\nERROR: expected output not found for command: {cmd}",
                        file=sys.stderr,
                    )
                    return 1

        for cmd in commands[idx:]:
            _send_line(proc, cmd)
            accepted_patterns = [
                f"Command: {cmd}",
                "Bad command",
                "File not found",
                "Invalid drive",
            ]
            ok = _read_until(
                proc, accepted_patterns, args.cmd_timeout, echo=not args.quiet
            )
            if not ok:
                # Retry once in case the shell fell back to keyboard.
                _send_line(proc, cmd)
                ok = _read_until(
                    proc, accepted_patterns, args.cmd_timeout, echo=not args.quiet
                )
                if not ok:
                    print(
                        f"\nERROR: timeout waiting for command acceptance: {cmd}",
                        file=sys.stderr,
                    )
                    return 1
            if not args.no_assert and cmd in expected_map:
                ok = _read_until(
                    proc,
                    expected_map[cmd],
                    args.cmd_timeout,
                    echo=not args.quiet,
                )
                if not ok:
                    print(
                        f"\nERROR: expected output not found for command: {cmd}",
                        file=sys.stderr,
                    )
                    return 1

    finally:
        try:
            proc.terminate()
        except Exception:
            pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
