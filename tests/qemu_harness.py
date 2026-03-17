import os
import select
import subprocess
import time


DEFAULT_SHELL_READY_MARKERS = [
    "Entering main loop",
    "MiniDOS Shell Ready.",
    "MiniDOS Shell Ready",
    "MiniDOS v0.1 Kernel Started",
]


def repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def resolve_disk_path(disk):
    if os.path.isabs(disk):
        return disk
    return os.path.join(repo_root(), disk)


def build_floppy_qemu_cmd(
    disk_path,
    *,
    qemu="qemu-system-i386",
    memory="16M",
    extra_args=None,
    serial="stdio",
    monitor="none",
    display="none",
):
    cmd = [
        qemu,
        "-drive",
        f"file={disk_path},format=raw,if=floppy,index=0",
        "-boot",
        "a",
        "-m",
        memory,
        "-serial",
        serial,
    ]

    if monitor is not None:
        cmd.extend(["-monitor", monitor])
    if display is not None:
        cmd.extend(["-display", display])
    if extra_args:
        cmd.extend(extra_args)
    return cmd


def spawn_qemu(cmd, *, stdin=subprocess.PIPE):
    return subprocess.Popen(
        cmd,
        stdin=stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def read_until(proc, patterns, timeout_s, *, echo=True, max_buf=40000):
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
            print(text, end="", flush=True)

        buf += text
        if len(buf) > max_buf:
            buf = buf[-max_buf:]

        for pat in patterns:
            if pat in buf:
                return buf, pat

    return buf, None


def wait_for_shell_ready(proc, timeout_s, *, echo=True, extra_markers=None):
    markers = list(DEFAULT_SHELL_READY_MARKERS)
    if extra_markers:
        markers.extend(extra_markers)
    return read_until(proc, markers, timeout_s, echo=echo, max_buf=60000)


def dismiss_default_gui(proc, initial_log, timeout_s, *, echo=True):
    log = initial_log or ""
    saw_gui_boot = any(marker in log for marker in ["GUI100", "GUI110", "Executing WIN95UI.ELF..."])

    if "APPIN001" not in log:
        deadline = time.time() + (timeout_s if saw_gui_boot else min(timeout_s, 1.0))

        while time.time() < deadline:
            remaining = max(0.1, deadline - time.time())
            extra, matched = read_until(
                proc,
                ["APPIN001", "GUI100", "GUI110", "GUI120", "GUI190", "Executing WIN95UI.ELF..."],
                remaining,
                echo=echo,
            )
            log += extra

            if matched == "APPIN001":
                break
            if matched in {"GUI100", "GUI110", "Executing WIN95UI.ELF..."}:
                if not saw_gui_boot:
                    deadline = time.time() + timeout_s
                saw_gui_boot = True
                continue
            if matched in {"GUI120", "GUI190"}:
                return log, 0
            if not matched:
                if saw_gui_boot:
                    raise RuntimeError("timeout waiting for default GUI input readiness")
                return log, 0

        if "APPIN001" not in log:
            if saw_gui_boot:
                raise RuntimeError("default GUI booted but never became ready for input")
            return log, 0

    if proc.stdin is None:
        raise RuntimeError("QEMU process has no stdin pipe")

    proc.stdin.write(b"\x1b")
    proc.stdin.flush()

    extra, matched = read_until(proc, ["APPRET001"], max(5.0, timeout_s), echo=echo)
    log += extra
    if not matched:
        raise RuntimeError("timeout waiting for default GUI to return")

    return log, 1


def send_line(proc, line):
    if proc.stdin is None:
        raise RuntimeError("QEMU process has no stdin pipe")
    proc.stdin.write((line + "\r").encode())
    proc.stdin.flush()


def terminate_process(proc):
    try:
        proc.terminate()
    except Exception:
        pass
