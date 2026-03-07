#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time


def read_until(proc, patterns, timeout_s, max_buf=60000):
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
        if any(p in buf for p in patterns):
            return buf
    return None


def send_line(proc, line):
    proc.stdin.write((line + "\r").encode())
    proc.stdin.flush()


def run_cmd(proc, cmd, must_contain=None, timeout_s=8.0, wait_cycle=True):
    del wait_cycle
    send_line(proc, cmd)
    accepted = read_until(
        proc,
        [f"Command: {cmd}", "Bad command", "File not found", "Invalid drive"],
        timeout_s,
    )
    if not accepted:
        send_line(proc, cmd)
        accepted = read_until(
            proc,
            [f"Command: {cmd}", "Bad command", "File not found", "Invalid drive"],
            timeout_s,
        )
    if not accepted:
        raise RuntimeError(f"timeout waiting command acceptance: {cmd}")

    out = accepted
    if must_contain:
        if not all(token in out for token in must_contain):
            matched = read_until(proc, must_contain, timeout_s)
            if not matched:
                missing = [token for token in must_contain if token not in out]
                raise RuntimeError(f"missing token(s) `{missing}` for `{cmd}`")
            out += matched
    return out


def assert_not_contains(text, token, ctx):
    if token in text:
        raise RuntimeError(f"unexpected token `{token}` in {ctx}")


def run_host_cmd(cmd, input_bytes=None):
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE if input_bytes is not None else None,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    proc.communicate(input=input_bytes)
    if proc.returncode != 0:
        raise RuntimeError(f"host command failed: {' '.join(cmd)}")


def create_partitioned_disk(path, partitions):
    # partitions: [{"start":int,"size":int,"label":str,"file":str,"text":str}, ...]
    run_host_cmd(["dd", "if=/dev/zero", f"of={path}", "bs=1M", "count=128", "status=none"])

    lines = ["unit: sectors"]
    for idx, part in enumerate(partitions, start=1):
        lines.append(f"/dev/sda{idx}: start={part['start']}, size={part['size']}, Id=6")
    table = ("\n".join(lines) + "\n").encode("ascii")
    run_host_cmd(
        [
            "sfdisk",
            "--force",
            "--no-reread",
            "--no-tell-kernel",
            "--wipe",
            "always",
            path,
        ],
        input_bytes=table,
    )

    for part in partitions:
        offset = part["start"] * 512
        run_host_cmd(["mformat", "-i", f"{path}@@{offset}", "-v", part["label"], "::"])
        run_host_cmd(
            ["mcopy", "-i", f"{path}@@{offset}", "-", f"::/{part['file']}"],
            input_bytes=part["text"].encode("ascii"),
        )


def require_tools():
    tools = ["qemu-system-i386", "sfdisk", "mformat", "mcopy", "dd"]
    missing = [t for t in tools if not shutil.which(t)]
    if missing:
        raise RuntimeError("missing host tools: " + ", ".join(missing))


def main():
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    disk_a = os.path.join(root_dir, "minidos.img")
    if not os.path.exists(disk_a):
        print("ERROR: minidos.img not found. Run `make` first.", file=sys.stderr)
        return 2

    try:
        require_tools()
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="minidos-phase3-") as tmpdir:
        disk_b = os.path.join(tmpdir, "disk_b.img")

        create_partitioned_disk(
            disk_b,
            [
                {"start": 2048, "size": 32768, "label": "B_ONE", "file": "B1MARK.TXT", "text": "B1 data\n"},
                {"start": 36864, "size": 32768, "label": "B_TWO", "file": "B2MARK.TXT", "text": "B2 data\n"},
            ],
        )
        qemu_cmd = [
            "qemu-system-i386",
            "-drive",
            f"file={disk_a},format=raw,if=floppy,index=0",
            "-boot",
            "a",
            "-drive",
            f"file={disk_b},format=raw,if=ide,index=0",
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
            ready = read_until(proc, ["Entering main loop", "MiniDOS Shell Ready"], 30.0)
            if not ready:
                print("ERROR: timeout waiting for shell readiness", file=sys.stderr)
                return 1

            # Avoid relying on `drives` serial output timing; validate drives via per-drive directory reads.
            for idx, letter in enumerate(["A", "B", "C"]):
                run_cmd(proc, f"{letter}:", wait_cycle=(idx != 0))
                run_cmd(proc, "dir", [f"Directory of {letter}:\\"])

            out = run_cmd(proc, "A:", ["Switched to drive A:"])
            out = run_cmd(proc, "dir", ["Directory of A:\\", "README.TXT", "BOOTLOGO.DAT", "BOOTLOGO.PAL"])
            assert_not_contains(out, "B1MARK", "A: directory listing")

            out = run_cmd(proc, "B:", ["Switched to drive B:"])
            out = run_cmd(proc, "dir", ["Directory of B:\\", "B1MARK.TXT"])
            run_cmd(proc, "copy B1MARK.TXT BWORK.TXT", ["File copied"])
            out = run_cmd(proc, "dir", ["BWORK", "B1MARK"])

            # Cross-drive syntax should not be treated as valid copy target.
            out = run_cmd(proc, "copy B1MARK.TXT C:\\X.TXT")
            if "File copied" in out:
                raise RuntimeError("unexpected cross-drive copy success in B:")

            out = run_cmd(proc, "C:", ["Switched to drive C:"])
            out = run_cmd(proc, "dir", ["Directory of C:\\", "B2MARK.TXT"])
            assert_not_contains(out, "BWORK", "C: directory listing")
            run_cmd(proc, "mkdir C_TMP", ["Directory created"])
            run_cmd(proc, "rmdir C_TMP", ["Directory removed"])

            out = run_cmd(proc, "B:", ["Switched to drive B:"])
            out = run_cmd(proc, "dir", ["BWORK"])
            run_cmd(proc, "ren BWORK.TXT BREN.TXT", ["File moved"])
            out = run_cmd(proc, "dir", ["BREN"])
            run_cmd(proc, "del BREN.TXT", ["File deleted"])
            out = run_cmd(proc, "dir", ["Directory of B:\\"])
            assert_not_contains(out, "BREN", "B: post-delete directory listing")

            run_cmd(proc, "D:")
            out = run_cmd(proc, "dir", ["Directory of "])
            if "Directory of D:\\" in out:
                raise RuntimeError("unexpected switch to drive D:")
        finally:
            try:
                proc.terminate()
            except Exception:
                pass

    print("Phase 3 multi-disk stress test passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
