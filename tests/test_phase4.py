#!/usr/bin/env python3
import os
import select
import subprocess
import sys
import time


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
IMG = os.path.join(ROOT, "minidos.img")


def run_host(cmd):
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(cmd)}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def read_until(proc, patterns, timeout_s=10.0, max_buf=30000):
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
        sys.stdout.write(text)
        sys.stdout.flush()
        buf += text
        if len(buf) > max_buf:
            buf = buf[-max_buf:]
        for pat in patterns:
            if pat in buf:
                return buf
    raise RuntimeError(f"Timeout waiting for patterns: {patterns}")


def send_cmd(proc, cmd, patterns):
    proc.stdin.write((cmd + "\r").encode())
    proc.stdin.flush()
    read_until(proc, [f"Command: {cmd}"], timeout_s=8.0)
    return read_until(proc, patterns, timeout_s=8.0)


def main():
    if not os.path.exists(IMG):
        print("ERROR: minidos.img not found. Run make first.", file=sys.stderr)
        return 2

    run_host(["./external_apps/add_app.sh", "external_apps/templates/hello_elf.c", "HELLOELF"])
    run_host(["./external_apps/add_app.sh", "external_apps/templates/stat_elf.c", "STATELF"])

    qemu_cmd = [
        "qemu-system-i386",
        "-drive",
        f"file={IMG},format=raw,if=ide",
        "-m",
        "16M",
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
        read_until(proc, ["MiniDOS Shell Ready", "Entering main loop"], timeout_s=30.0)
        send_cmd(proc, "elfls", ["HELLOELF.ELF"])
        out = read_until(proc, ["STATELF"], timeout_s=8.0)
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
        try:
            proc.terminate()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
