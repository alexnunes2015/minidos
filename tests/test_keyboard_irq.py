#!/usr/bin/env python3
import argparse
import json
import os
import select
import socket
import sys
import tempfile
import time

import subprocess

from qemu_harness import (
    KBD_READY_MARKERS,
    build_floppy_qemu_cmd,
    read_until,
    resolve_disk_path,
    spawn_qemu,
    terminate_process,
    wait_for_marker_sequence,
    wait_for_shell_ready,
)

def _qmp_read_line(qmp_sock, timeout=2.0):
    deadline = time.time() + timeout
    data = b""
    while time.time() < deadline:
        ready, _, _ = select.select([qmp_sock], [], [], 0.1)
        if not ready:
            continue
        chunk = qmp_sock.recv(4096)
        if not chunk:
            break
        data += chunk
        if b"\n" in data:
            line, _, _ = data.partition(b"\n")
            return line.decode(errors="ignore")
    return None


def _qmp_exec(qmp_sock, cmd, args=None):
    msg = {"execute": cmd}
    if args:
        msg["arguments"] = args
    qmp_sock.sendall((json.dumps(msg) + "\n").encode())

    deadline = time.time() + 3.0
    while time.time() < deadline:
        line = _qmp_read_line(qmp_sock, timeout=0.5)
        if not line:
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "return" in obj:
            return obj["return"]
        if "error" in obj:
            raise RuntimeError(f"QMP command failed: {obj['error']}")
    raise RuntimeError(f"QMP timeout for command: {cmd}")


def _qmp_socket_root() -> str:
    return "/tmp"


def _qmp_connect(socket_path, timeout_s=10.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            qmp_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            qmp_sock.connect(socket_path)
            break
        except OSError:
            time.sleep(0.1)
    else:
        raise RuntimeError("could not connect to QMP socket")

    greeting = _qmp_read_line(qmp_sock, timeout=2.0)
    if not greeting:
        raise RuntimeError("no QMP greeting")
    _qmp_exec(qmp_sock, "qmp_capabilities")
    return qmp_sock


def _send_key(qmp_sock, key, *, duplicate_make=False):
    parts = key.split("-")
    modifiers = parts[:-1]
    base_key = parts[-1]
    events = []

    for modifier in modifiers:
        events.append(
            {
                "type": "key",
                "data": {"down": True, "key": {"type": "qcode", "data": modifier}},
            }
        )

    events.append(
        {
            "type": "key",
            "data": {"down": True, "key": {"type": "qcode", "data": base_key}},
        }
    )
    if duplicate_make:
        events.append(
            {
                "type": "key",
                "data": {"down": True, "key": {"type": "qcode", "data": base_key}},
            }
        )
    events.append(
        {
            "type": "key",
            "data": {"down": False, "key": {"type": "qcode", "data": base_key}},
        }
    )

    for modifier in reversed(modifiers):
        events.append(
            {
                "type": "key",
                "data": {"down": False, "key": {"type": "qcode", "data": modifier}},
            }
        )

    try:
        _qmp_exec(qmp_sock, "input-send-event", {"events": events})
    except RuntimeError:
        _qmp_exec(
            qmp_sock,
            "human-monitor-command",
            {"command-line": f"sendkey {key}"},
        )


def _send_text_as_keys(qmp_sock, text, key_delay, *, duplicate_make=False):
    key_map = {
        "\n": "ret",
        "\r": "ret",
        " ": "spc",
        "-": "minus",
        "=": "equal",
        ".": "dot",
        ",": "comma",
        "/": "slash",
    }
    for ch in text:
        if "a" <= ch <= "z" or "0" <= ch <= "9":
            key = ch
        elif "A" <= ch <= "Z":
            key = "shift-" + ch.lower()
        else:
            key = key_map.get(ch)
        if not key:
            raise RuntimeError(f"unsupported key for test: {ch!r}")
        _send_key(qmp_sock, key, duplicate_make=duplicate_make)
        time.sleep(key_delay)


def _extract_last_command_line(log_text):
    lines = log_text.splitlines()
    last = None
    for line in lines:
        if "Command:" in line:
            idx = line.find("Command:")
            candidate = line[idx + len("Command:") :].strip()
            if candidate:
                last = candidate
    return last


def _wait_for_command_result(proc, cmd, expected_patterns, timeout_s, *, echo):
    patterns = [f"Command: {cmd}"] + list(expected_patterns)
    log, matched = read_until(proc, patterns, timeout_s, echo=echo)
    if not matched:
        raise RuntimeError(f"timeout waiting for command acceptance: {cmd}")

    if not any(pattern in log for pattern in expected_patterns):
        extra, matched = read_until(proc, expected_patterns, timeout_s, echo=echo)
        if not matched:
            raise RuntimeError(f"timeout waiting for command output: {cmd}")
        log += extra

    last_cmd = _extract_last_command_line(log)
    if last_cmd is not None and last_cmd != cmd:
        raise RuntimeError(f"keyboard command mismatch (expected {cmd!r}, got {last_cmd!r})")

    for pattern in expected_patterns:
        if pattern not in log:
            raise RuntimeError(f"missing expected output {pattern!r} for command {cmd!r}")

    return log

def _wait_for_default_gui(proc, timeout_s, *, echo):
    log_after, matched = read_until(
        proc,
        ["APPIN001"],
        min(timeout_s, 1.0),
        echo=echo,
    )
    if not matched:
        return None
    return log_after


def main():
    parser = argparse.ArgumentParser(
        description="Validate keyboard IRQ path by injecting keys via QMP sendkey."
    )
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--ready-timeout", type=float, default=30.0)
    parser.add_argument("--cmd-timeout", type=float, default=8.0)
    parser.add_argument("--key-delay", type=float, default=0.05)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument(
        "--soft-skip-env",
        action="store_true",
        help="Return success when environment blocks QMP/socket setup (useful in restricted sandboxes).",
    )
    args = parser.parse_args()

    disk_path = resolve_disk_path(args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    qmp_dir = os.environ.get("TMPDIR_QMP", _qmp_socket_root())
    os.makedirs(qmp_dir, exist_ok=True)
    qmp_socket = os.path.join(qmp_dir, f"minidos-qmp-{os.getpid()}.sock")
    try:
        os.unlink(qmp_socket)
    except FileNotFoundError:
        pass

    qemu_cmd = build_floppy_qemu_cmd(
        disk_path,
        qemu=args.qemu,
        memory=args.memory,
        extra_args=["-qmp", f"unix:{qmp_socket},server=on,wait=off"],
    )
    proc = spawn_qemu(qemu_cmd, stdin=subprocess.DEVNULL)

    qmp_sock = None
    try:
        log, matched = wait_for_shell_ready(
            proc,
            args.ready_timeout,
            echo=not args.quiet,
        )
        if "Failed to bind socket" in log or "Operation not permitted" in log:
            if args.soft_skip_env:
                print("SKIP: QMP/socket blocked by environment restrictions", file=sys.stderr)
                return 0
            print("ERROR: QMP/socket blocked by environment restrictions", file=sys.stderr)
            return 1
        if not matched:
            print("ERROR: timeout waiting for shell readiness", file=sys.stderr)
            return 1

        try:
            qmp_sock = _qmp_connect(qmp_socket, timeout_s=10.0)
        except RuntimeError as exc:
            if args.soft_skip_env:
                print(f"SKIP: QMP unavailable in this environment ({exc})", file=sys.stderr)
                return 0
            raise

        kbd_log, kbd_matched = wait_for_marker_sequence(
            proc,
            [KBD_READY_MARKERS],
            args.ready_timeout,
            echo=not args.quiet,
        )
        if not kbd_matched:
            raise RuntimeError("timeout waiting for keyboard-ready marker")
        log += kbd_log

        if "APPIN001" in log or _wait_for_default_gui(proc, args.cmd_timeout, echo=not args.quiet) is not None:
            _send_key(qmp_sock, "esc")
            log_after, matched = read_until(
                proc,
                ["APPRET001"],
                max(args.cmd_timeout, 20.0),
                echo=not args.quiet,
            )
            if not matched:
                raise RuntimeError("timeout waiting for WIN95UI app return")

        _send_text_as_keys(qmp_sock, "top 200 1\n", args.key_delay)
        log_after, matched = read_until(
            proc,
            ["STATE"],
            max(args.cmd_timeout, 12.0),
            echo=not args.quiet,
        )
        if not matched:
            raise RuntimeError("timeout waiting for top table header")
        if "shell" not in log_after:
            extra, matched = read_until(
                proc,
                ["shell"],
                max(args.cmd_timeout, 12.0),
                echo=not args.quiet,
            )
            log_after += extra
            if not matched:
                raise RuntimeError("timeout waiting for top process lines")
        if "idle" not in log_after:
            extra, matched = read_until(
                proc,
                ["idle"],
                max(args.cmd_timeout, 12.0),
                echo=not args.quiet,
            )
            log_after += extra
            if not matched:
                raise RuntimeError("timeout waiting for top idle row")
        if "shell" not in log_after or "idle" not in log_after:
            raise RuntimeError("top output missing expected process rows")

        _send_text_as_keys(qmp_sock, "ver\n", args.key_delay, duplicate_make=True)
        _wait_for_command_result(
            proc,
            "ver",
            ["MiniDOS Version 0.1"],
            args.cmd_timeout,
            echo=not args.quiet,
        )

        _send_text_as_keys(qmp_sock, "dosshell\n", args.key_delay)
        _wait_for_command_result(
            proc,
            "dosshell",
            ["Executing DOSSHELL.ELF..."],
            args.cmd_timeout,
            echo=not args.quiet,
        )
        log_after, matched = read_until(
            proc,
            ["APPIN001"],
            max(args.cmd_timeout, 20.0),
            echo=not args.quiet,
        )
        if not matched:
            raise RuntimeError("timeout waiting for DOSSHELL app input readiness")
        _send_key(qmp_sock, "q")
        log_after, matched = read_until(
            proc,
            ["APPRET001"],
            max(args.cmd_timeout, 20.0),
            echo=not args.quiet,
        )
        if not matched:
            raise RuntimeError("timeout waiting for DOSSHELL app return")
        _send_text_as_keys(qmp_sock, "ver\n", args.key_delay)
        _wait_for_command_result(
            proc,
            "ver",
            ["MiniDOS Version 0.1"],
            args.cmd_timeout,
            echo=not args.quiet,
        )

        _send_text_as_keys(qmp_sock, "edit\n", args.key_delay)
        _wait_for_command_result(
            proc,
            "edit",
            ["Executing EDIT.ELF..."],
            args.cmd_timeout,
            echo=not args.quiet,
        )
        log_after, matched = read_until(
            proc,
            ["APPIN001"],
            max(args.cmd_timeout, 20.0),
            echo=not args.quiet,
        )
        if not matched:
            raise RuntimeError("timeout waiting for EDIT app input readiness")
        _send_key(qmp_sock, "esc")
        log_after, matched = read_until(
            proc,
            ["APPRET001"],
            max(args.cmd_timeout, 20.0),
            echo=not args.quiet,
        )
        if not matched:
            raise RuntimeError("timeout waiting for EDIT app return")
        _send_text_as_keys(qmp_sock, "ver\n", args.key_delay)
        _wait_for_command_result(
            proc,
            "ver",
            ["MiniDOS Version 0.1"],
            args.cmd_timeout,
            echo=not args.quiet,
        )

    finally:
        if qmp_sock:
            try:
                qmp_sock.close()
            except Exception:
                pass
        terminate_process(proc)
        try:
            if os.path.exists(qmp_socket):
                os.unlink(qmp_socket)
        except Exception:
            pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
