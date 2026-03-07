#!/usr/bin/env python3
import argparse
import os
import select
import subprocess
import sys
import time


def read_until(proc, patterns, timeout_s, max_buf=40000):
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
                return True
    return False


def main():
    parser = argparse.ArgumentParser(description="Validate paging boot and #PF diagnostics")
    parser.add_argument("--disk", default="minidos.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--memory", default="16M")
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("--expect-fault", action="store_true")
    args = parser.parse_args()

    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    disk_path = args.disk if os.path.isabs(args.disk) else os.path.join(root_dir, args.disk)
    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}", file=sys.stderr)
        return 2

    cmd = [
        args.qemu,
        "-drive", f"file={disk_path},format=raw,if=floppy,index=0",
        "-boot", "a",
        "-m", args.memory,
        "-serial", "stdio",
        "-monitor", "none",
        "-display", "none",
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        if args.expect_fault:
            ok = read_until(proc, ["[paging] #PF detected", "[paging] CR2=0x00500000"], args.timeout)
            return 0 if ok else 1

        required = [
            "[paging] init",
            "[paging] enabled",
            "paging self-test OK",
            "Entering main loop",
        ]
        for marker in required:
            ok = read_until(proc, [marker], args.timeout)
            if not ok:
                print(f"\nERROR: marker not found: {marker}", file=sys.stderr)
                return 1
        return 0
    finally:
        try:
            proc.terminate()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
