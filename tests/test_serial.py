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

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    qemu_cmd = build_floppy_qemu_cmd(
        disk_path,
        qemu=args.qemu,
        memory=args.memory,
    )
    proc = spawn_qemu(qemu_cmd)

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
        ready_deadline = time.time() + args.ready_timeout
        ready_log, matched = wait_for_shell_ready(
            proc,
            args.ready_timeout,
            echo=not args.quiet,
            extra_markers=kernel_ready,
        )
        if not matched:
            # Fallback: if kernel messages never show, try after PM.
            ready_log, matched = read_until(
                proc,
                [pm_marker],
                args.ready_timeout,
                echo=not args.quiet,
            )
            if not matched:
                print("\nERROR: timeout waiting for shell readiness.", file=sys.stderr)
                return 1
            time.sleep(args.post_pm_delay)
        elif matched != "Entering main loop":
            remaining = max(0.1, ready_deadline - time.time())
            extra_log, matched = read_until(
                proc,
                ["Entering main loop"],
                remaining,
                echo=not args.quiet,
            )
            if not matched:
                print(
                    "\nERROR: shell banner appeared before the main loop became ready.",
                    file=sys.stderr,
                )
                return 1
            ready_log += extra_log

        dismiss_default_gui(
            proc,
            ready_log,
            args.cmd_timeout,
            echo=not args.quiet,
        )

        expected_map = {
            "help": ["Available commands:"],
            "ver": ["MiniDOS Version 0.1"],
            "drives": ["Command: drives", "Available drives:"],
            "dir": ["Directory of "],
            "mem": ["System Memory:"],
            "ps": ["STATE", "shell", "idle"],
            "top 200 1": ["STATE", "shell", "idle"],
            "a:": ["Switched to drive"],
            "A:": ["Switched to drive"],
            "c:": ["Switched to drive"],
            "d:": ["Switched to drive"],
            "e:": ["Switched to drive"],
            "C:": ["Switched to drive"],
            "D:": ["Switched to drive"],
            "E:": ["Switched to drive"],
            "mkdir TESTDIR": ["Directory created"],
            "rmdir TESTDIR": ["Directory removed"],
        }

        commands = list(args.commands)
        idx = 0
        if commands:
            # Send first command early so serial input is already pending.
            send_line(proc, commands[0])
            idx = 1
            cmd = commands[0]
            accepted_patterns = [
                f"Command: {cmd}",
                "Bad command",
                "File not found",
                "Invalid drive",
            ]
            output, matched = read_until(
                proc,
                accepted_patterns,
                args.cmd_timeout,
                echo=not args.quiet,
            )
            if not matched:
                send_line(proc, cmd)
                output, matched = read_until(
                    proc,
                    accepted_patterns,
                    args.cmd_timeout,
                    echo=not args.quiet,
                )
                if not matched:
                    print(
                        f"\nERROR: timeout waiting for command acceptance: {cmd}",
                        file=sys.stderr,
                    )
                    return 1
            if not args.no_assert and cmd in expected_map:
                if not any(pat in output for pat in expected_map[cmd]):
                    _, expected = read_until(
                        proc,
                        expected_map[cmd],
                        args.cmd_timeout,
                        echo=not args.quiet,
                    )
                    if not expected:
                        print(
                            f"\nERROR: expected output not found for command: {cmd}",
                            file=sys.stderr,
                        )
                        return 1

        for cmd in commands[idx:]:
            send_line(proc, cmd)
            accepted_patterns = [
                f"Command: {cmd}",
                "Bad command",
                "File not found",
                "Invalid drive",
            ]
            output, matched = read_until(
                proc,
                accepted_patterns,
                args.cmd_timeout,
                echo=not args.quiet,
            )
            if not matched:
                # Retry once in case the shell fell back to keyboard.
                send_line(proc, cmd)
                output, matched = read_until(
                    proc,
                    accepted_patterns,
                    args.cmd_timeout,
                    echo=not args.quiet,
                )
                if not matched:
                    print(
                        f"\nERROR: timeout waiting for command acceptance: {cmd}",
                        file=sys.stderr,
                    )
                    return 1
            if not args.no_assert and cmd in expected_map:
                if not any(pat in output for pat in expected_map[cmd]):
                    _, expected = read_until(
                        proc,
                        expected_map[cmd],
                        args.cmd_timeout,
                        echo=not args.quiet,
                    )
                    if not expected:
                        print(
                            f"\nERROR: expected output not found for command: {cmd}",
                            file=sys.stderr,
                        )
                        return 1

    finally:
        terminate_process(proc)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
