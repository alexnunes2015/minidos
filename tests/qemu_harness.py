import os
import select
import subprocess
import time


STAGE2_READY_MARKERS = [
    "[Stage2] Entering PM...",
]

BOOT_READY_MARKERS = [
    "BOOT100",
    "BOOT110",
    "BOOT190",
    "BOOT300",
]

SCHED_READY_MARKERS = [
    "SCHED190",
]

SHELL_READY_MARKERS = [
    "SHELL100",
    "Entering main loop",
]

KBD_READY_MARKERS = ["[kbd] scan set 1", "[kbd] scan set 2"]

MOUSE_READY_MARKERS = ["[mouse] PS/2 mouse enabled on IRQ12", "[mouse] first packet received"]

SHELL_PROMPT_MARKERS = ["MiniDOS Shell Ready.", "MiniDOS Shell Ready"]


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


def wait_for_marker_sequence(proc, marker_groups, timeout_s, *, echo=True, max_buf=60000):
    log = ""
    deadline = time.time() + timeout_s
    last_matched = None

    for group in marker_groups:
        remaining = deadline - time.time()
        if remaining <= 0:
            return log, None
        chunk, matched = read_until(
            proc,
            list(group),
            remaining,
            echo=echo,
            max_buf=max_buf,
        )
        log += chunk
        if not matched:
            return log, None
        last_matched = matched

    return log, last_matched


def wait_for_marker(proc, marker, timeout_s, *, echo=True, max_buf=40000):
    log, matched = read_until(proc, [marker], timeout_s, echo=echo, max_buf=max_buf)
    if not matched:
        raise RuntimeError(f"timeout waiting for marker: {marker}")
    return log


def wait_for_shell_ready(proc, timeout_s, *, echo=True, extra_markers=None):
    def _wait_group(group):
        nonlocal total_log
        remaining = deadline - time.time()
        if remaining <= 0:
            return None
        chunk, matched = read_until(proc, list(group), remaining, echo=echo, max_buf=60000)
        total_log += chunk
        return matched

    total_log = ""
    deadline = time.time() + timeout_s

    for group in (STAGE2_READY_MARKERS, BOOT_READY_MARKERS, SCHED_READY_MARKERS):
        matched = _wait_group(group)
        if not matched:
            return total_log, None

    shell_log, seen = _collect_markers(proc, SHELL_READY_MARKERS, deadline, echo=echo, max_buf=60000)
    total_log += shell_log
    if not all(marker in seen for marker in SHELL_READY_MARKERS):
        return total_log, None

    prompt_marker = next((marker for marker in SHELL_PROMPT_MARKERS if marker in total_log), None)
    if not prompt_marker:
        remaining = max(0.1, deadline - time.time())
        prompt_chunk, prompt_marker = read_until(proc, SHELL_PROMPT_MARKERS, remaining, echo=echo, max_buf=60000)
        total_log += prompt_chunk
        if not prompt_marker:
            return total_log, None

    final_marker = prompt_marker

    if extra_markers:
        matched = _wait_group(extra_markers)
        if not matched:
            return total_log, None
        final_marker = matched

    return total_log, final_marker


def _collect_markers(proc, markers, deadline, *, echo=True, max_buf=60000):
    log = ""
    found = set()

    while time.time() < deadline and len(found) < len(markers):
        remaining = deadline - time.time()
        chunk, _ = read_until(proc, markers, max(0.1, remaining), echo=echo, max_buf=max_buf)
        log += chunk
        for marker in markers:
            if marker in log:
                found.add(marker)
        if not chunk:
            break

    for marker in markers:
        if marker in log:
            found.add(marker)

    return log, found


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
