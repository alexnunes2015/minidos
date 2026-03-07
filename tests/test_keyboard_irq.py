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
    build_floppy_qemu_cmd,
    read_until,
    resolve_disk_path,
    spawn_qemu,
    terminate_process,
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


def _send_key(qmp_sock, key):
    _qmp_exec(
        qmp_sock,
        "human-monitor-command",
        {"command-line": f"sendkey {key}"},
    )


def _send_text_as_keys(qmp_sock, text, key_delay):
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
        _send_key(qmp_sock, key)
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

    qmp_socket = os.path.join(tempfile.gettempdir(), f"minidos-qmp-{os.getpid()}.sock")
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
        ready_markers = [
            "Entering main loop",
            "MiniDOS Shell Ready.",
            "Failed to bind socket",
            "Operation not permitted",
        ]
        log, matched = read_until(proc, ready_markers, args.ready_timeout, echo=not args.quiet)
        if not matched:
            print("ERROR: timeout waiting for shell readiness", file=sys.stderr)
            return 1
        if "Failed to bind socket" in log or "Operation not permitted" in log:
            if args.soft_skip_env:
                print("SKIP: QMP/socket blocked by environment restrictions", file=sys.stderr)
                return 0
            print("ERROR: QMP/socket blocked by environment restrictions", file=sys.stderr)
            return 1

        try:
            qmp_sock = _qmp_connect(qmp_socket, timeout_s=10.0)
        except RuntimeError as exc:
            if args.soft_skip_env:
                print(f"SKIP: QMP unavailable in this environment ({exc})", file=sys.stderr)
                return 0
            raise

        _send_text_as_keys(qmp_sock, "ver\n", args.key_delay)

        log_after, matched = read_until(
            proc,
            ["Command:", "MiniDOS Version 0.1"],
            args.cmd_timeout,
            echo=not args.quiet,
        )
        if not matched:
            print("ERROR: timeout waiting for keyboard command output", file=sys.stderr)
            return 1

        extra, _ = read_until(
            proc,
            ["MiniDOS Version 0.1"],
            args.cmd_timeout,
            echo=not args.quiet,
        )
        combined = (log_after or "") + (extra or "")

        last_cmd = _extract_last_command_line(combined)
        if last_cmd != "ver":
            print(
                f"ERROR: keyboard command mismatch (expected 'ver', got {last_cmd!r})",
                file=sys.stderr,
            )
            return 1

        if "MiniDOS Version 0.1" not in combined:
            print("ERROR: version output not found after keyboard input", file=sys.stderr)
            return 1

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
