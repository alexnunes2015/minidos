# MiniDOS Debugging Guide

This guide is for fast diagnosis in an AI-agent-first workflow. Use the shortest loop that can prove or localize the problem.

## Fast Commands

```sh
make verify-image
make phase0-check
make test-phase5
make test-large-elf
make test-multitask-com
make test-scheduler-fairness
make test-syscall-matrix
make test-user-isolation
make test-user-cleanup
make test-ui-runtime
make test-graphics
make test-mouse
make test-mouse-failure
make ci
make run-no-reboot
make run-trace
make run-gdb
make gdb-kernel
```

## What Each Command Is For

- `make verify-image`: validates the disk image layout, BPB, patched kernel sector count, and expected files inside the FAT volume.
- `make phase0-check`: clean rebuild plus serial smoke test.
- `make test-phase5`: validates the phase-5 scheduler runtime plus the negative guard-page fault path.
- `make test-large-elf`: validates the dynamic userland slot path with an ELF whose mapped memory exceeds the old 1 MiB limit.
- `make test-multitask-com`: validates background `.COM` apps on the same scheduler/user-mode runtime used by ELFs.
- `make test-scheduler-fairness`: runs two mixed-workload rounds and confirms CPU-bound, blocked, and child-thread apps all complete without starvation.
- `make test-syscall-matrix`: exercises invalid pointers, ranges, formats, and unknown syscall numbers in both ELF and COM, then confirms the shell resumes.
- `make test-user-isolation`: validates ring3 ELF/COM containment for bad syscall pointers, stale low-memory app VAs, and direct user-mode page faults.
- `make test-user-cleanup`: repeats background ELF/COM faults, validates PID/physical-slot reuse, and checks that a faulted leader plus child group disappears from `top`.
- `make test-ui-runtime`: compiles/runs the host-side UI runtime contract tests, including rectangle batching/fallback, packed surface layout/run fallback, and generic clipped-text dispatch.
- `make test-graphics`: rebuilds the image and validates video startup, the per-worker `PTVIDEO200` metric schema, stress completion, and shell responsiveness after stress.
- `make test-mouse`: exercises the visible `STARTUI` mouse/input path in QEMU after graphics/runtime changes.
- `make test-mouse-failure`: rebuilds with `MOUSE_TEST_FORCE_FAIL` and verifies `MOUSE201`, no false mouse-ready marker, and continued shell startup.
- `make ci`: runs the repository-wide Tier 3 validation gate; graphics changes must also run the focused graphics and mouse suites because they are not currently part of this target.
- `make test-storage-failure`: exercises invalid FAT metadata so the boot reports `DISK021` and stops with `STOP 0x00000004`.
- `make run-no-reboot`: keeps QEMU from rebooting on panic/triple-fault style failures so the last serial output stays visible.
- `make run-trace`: writes a QEMU trace to `build/qemu-trace.log` for low-level fault analysis.
- `make run-gdb`: starts QEMU paused with a GDB stub on `localhost:1234`.
- `make gdb-kernel`: opens GDB with `build/kernel.elf` symbols and connects to the paused VM.

## Serial Markers To Watch

- `[Stage2] Started`
- `[Stage2] Entering PM...`
- `[paging] init`
- `[paging] enabled`
- `paging self-test OK`
- `[video] init fb=... w=... h=... pitch=... bpp=... bb=... bp=... fast=...`
- `PTVIDEO100 start workers=... iterations=... started=...` when the `videostress` command launches worker threads
- `PTVIDEO110 limited by scheduler slots` when the stress test could not start all requested workers
- `PTVIDEO200 worker=0x........ ticks=0x........ iterations=0x........` once per completed stress worker; each field is an uppercase, zero-padded 32-bit hexadecimal value
- `PTBIG100` / `PTBIG190` / `PTBIG900` / `PTBIG901` when validating the large-ELF dynamic slot path
- `[kbd] scan set 1` or `[kbd] scan set 2`
- `KBD200` if a key was consumed through the direct polling compatibility fallback; the normal IRQ regression must not emit it
- `[mouse] PS/2 mouse enabled on IRQ12`
- `MOUSE201` / `MOUSE202` / `MOUSE203` / `MOUSE204` if mouse initialization entered a degraded mode; no mouse marker should appear in the normal mouse regression
- `[mouse] first packet received`
- `[int] IDT active, PIC remapped, IRQ0/IRQ1/IRQ12 enabled`
- `DISK021`
- `APPFLT900`
- `SCHED100`
- `SCHED110`
- `SCHED120`
- `SCHED190`
- `SCHED900`
- `STOP 0x00000006`
- `STOP 0x00000007`
- `[sched] phase5 context-switch self-test OK`
- `BOOT100`
- `BOOT110`
- `BOOT190`
- `BOOT300`
- `MiniDOS Shell Ready.`
- `[INFO][kernel] Entering main loop`
- `SHELL100` (immediately after `Entering main loop`, indicating the interactive command loop is ready for deterministic injection)

## Failure Playbooks

### Boot fails before Protected Mode

Run:

```sh
make verify-image
make run-no-reboot
```

Check:

- boot signature and BPB are valid
- stage2 still fits in the fixed sector budget
- patched `kernel_sectors` still matches the built kernel
- if the kernel crossed `64 KiB`, stage2 now advances the `ES` window while loading sectors
- if you see a tiny colored streak or random pixels near the top of the shell, inspect `nm -n build/kernel.elf | tail` and confirm `__bss_end < 0x000A0000`; the graphics text grid now lives in a scratch window at `0x00380000` specifically to keep runtime globals out of the VGA aperture
- check the `[video] init ... fast=...` marker to confirm whether the preferred `640x480` mode was selected or if stage2 had to fall back to a larger VBE mode, and whether the ASM fast-present path is active
- serial output reaches the PM checkpoints
- `BOOT100` appears before disk/drive probing
- `BOOT110` appears only if the runtime logo files were loaded successfully
- before `BOOT110`, the graphics placeholder is now a black screen with a blinking cursor
- `BOOT190` appears after the 5-second logo window counted from `BOOT110` and immediately before the shell takes over the screen
- `BOOT300` means the shell entered degraded mode because storage was unavailable for `AUTOEXEC.AUT`; the shell should still continue into `Entering main loop`

### Paging or exception failure

Run:

```sh
make test-paging
make run-trace
```

Check:

- `CR2`, `error`, and `eip` in serial output
- `build/qemu-trace.log`
- whether the fault happens before or after `paging self-test OK`

### Scheduler or stack-guard failure

Run:

```sh
make test-phase5
make run-no-reboot
```

Check:

- `SCHED100`, `SCHED110`, and `SCHED120` appear in order
- `SCHED190` appears in the positive path before `Entering main loop`
- `SCHED900` appears in the negative path before `[paging] #PF detected`
- `STOP 0x00000006` / `STOP 0x00000007` appear before the BSOD if scheduler bootstrap or the phase-5 self-test hard-fails
- `CR2` matches the scheduler stack arena (`0x0060xxxx`) when the guard page trips

### Shell is up but commands fail

Run:

```sh
make phase0-check
python3 tests/test_serial.py "ver" "drives" "dir"
python3 tests/test_serial.py "ps" "top 200 1"
```

Check:

- `Entering main loop` appears before the harness starts injecting serial commands
- `Command:` serial markers
- shell prompt shows a blinking block cursor in graphics mode; if it stays missing, check PIT ticks and the video cursor helpers
- current drive enumeration
- FAT init logs
- `ps` and `top` print a plain task table with `PID`, `NAME`, `STATE`, `MEM`, `CPU`, and `EXE`
- `MEM` is still scheduler kernel-stack reserve only; it does not include the dynamic user span mapped for ring3 apps

### Large ELF or app memory regressions

Run:

```sh
make test-large-elf
make run-no-reboot
```

Check:

- `PTBIG100` appears after `ptbig` is launched from `A:\PTEST`
- `APPBG100 ... bytes=...` shows a dynamic slot larger than the old 1 MiB ceiling for large apps
- `PTBIG190` appears before `APPRET001`
- `Invalid ELF or load error` usually means the ELF preflight rejected the segment range, file size, or arena capacity
- `No app memory slot available` means the contiguous physical arena below `0x00B00000` is fragmented or already consumed by background app groups

### Background ELF multitask regressions

Run:

```sh
make test-multitask
```

Check:

- `runbg ptcpu`, `runbg ptwait`, and `runbg ptthrd` each print `Started background app ... pid=...`
- `APPTH100` appears when `PTTHRD` spawns its child worker
- `top 200 1` shows the app leaders plus the `worker` child thread with the correct `EXE`
- `kill <pid>` prints `Background app group stopped` and removes the leader plus its children from the next `top`

### Background COM multitask regressions

Run:

```sh
make test-multitask-com
```

Check:

- `runbg cmcpu`, `runbg cmwait`, and `runbg cmthrd` each print `Started background app ... pid=...`
- `APPTH100` appears when `CMTHRD` spawns its child worker
- `top 200 1` shows the app leaders plus the `worker` child thread with `CM*.COM` in `EXE`
- `kill <pid>` prints `Background app group stopped` and removes the leader plus its children from the next `top`

### User isolation regressions

Run:

```sh
make test-user-isolation
```

Check:

- `BADP190` appears after both `BADPTR` and `BADCOM` try to pass a kernel pointer through `int 0x80`
- `OLDMAP` and `OLDCOM` emit `[paging] #PF detected`, `CR2=0x00200000`, `mode=user`, and `APPFLT900`, proving the old low-memory app window is no longer user-mapped
- `USRFAULT` and `USRFCOM` emit `[paging] #PF detected`, `CR2=0x00010000`, `mode=user`, and `APPFLT900`
- `APPRET001` appears after each user fault, proving the shell regained control without a reboot
- `ver` still works after all negative cases

### Keyboard IRQ regressions

Run:

```sh
make test-keyboard-soft
```

If environment allows QMP sockets, prefer:

```sh
make test-keyboard
```

That suite now covers keyboard-only entry into the shell plus keyboard exit paths for `DOSSHELL` and `EDIT`.
The normal boot now lands directly in the shell. The harness still tolerates a future boot-launched `STARTUI` by dismissing it with `ESC` before continuing.
It waits for `APPIN001` before sending keys into GUI apps and for `APPRET001` before asserting that the shell resumed, so app-input races are easier to localize from serial logs.
The manual/demo layout now keeps `STARTUI.ELF` plus its companion resources under `A:\AIOS`, so the normal launch flow is `cd aios`, then `startui`.
`STARTUI` now builds from all `external_apps/apps/win95_demo/*.c` siblings, so a compile break in that app can come from any extracted UI module, not just `win95_demo.c`.

### Mouse / GUI IRQ12 regressions

Run:

```sh
make test-mouse
```

Check:

- `[mouse] PS/2 mouse enabled on IRQ12`
- `APPIN001` after `STARTUI` starts
- `APPRET001` after the click closes `STARTUI` or after `q` / `ESC` return from the other apps
- `[mouse] first packet received` after QMP moves the pointer
- the test now derives the click path from the active `[video] init` geometry, so a failure here usually means a real GUI layout/input regression rather than a hardcoded-resolution mismatch
- no debug extra da GUI na serial while the GUI runs
- shell accepts `ver` after the test clicks `Cancel`
- `DOSSHELL` and `EDIT` still exit normally after mouse movement inside the app

### Graphics ABI / Win95Ui rendering regressions

Run:

```sh
make test-ui-runtime
make test-graphics
make test-mouse
```

Check:

- a rectangle batch contains 1 through 64 entries; any invalid count, user range, or rectangle rejects the complete syscall before pixels or damage change
- the runtime flushes queued rectangles before text, surface, clear, and present operations; if syscall 35 fails, it replays the saved rectangles through the legacy single-rectangle call in the same order
- surface format 0 is the legacy byte sequence `[X,R,G,B]`; format 1 is a little-endian packed `0x00RRGGBB` word, and format 2 is straight-alpha `0xAARRGGBB`
- wrong red/blue channels usually mean a legacy byte stream was mislabeled as packed format 1 (or vice versa)
- ARGB alpha 0 preserves the destination, alpha 255 replaces it, and partial alpha uses source-over with nearest-integer rounding
- syscall 36 clips text inside the requested rectangle; `APP_GFX_TEXT_TRANSPARENT_BG` leaves zero glyph bits untouched, while `flags == 0` remains opaque; the generic runtime helper tries this path first
- Win95Ui transparent labels deliberately use clipped `ui_font_8x8` foreground runs through rectangle batching, rather than syscall 36, to keep their glyph pixels unchanged
- packed formats 1/2, including ARGB Win95Ui icons, require runtime and kernel lockstep; only format 0 preserves the older byte-stream ABI
- if a packed wallpaper, list-row, or ARGB icon surface call explicitly fails, the runtime should emit its documented clipped, ordered runs; a kernel that returns success while ignoring `format` cannot be detected by this fallback
- with `DIRTY_RECT_DEBUG`, repeated contained rectangles should not grow the pending list, and touching/overlapping strips merge only when their full horizontal or vertical span matches
- `videostress 2 60` emits two distinct `PTVIDEO200` lines whose `iterations` field is `0x0000003C`; `ticks` is recorded for comparison but has no fixed pass/fail ceiling
- after the stress workers finish, `ver` still responds, proving the video lock was released

For a release-ready or cross-cutting graphics change, finish with `make ci` after the focused suites pass.

### FAT or multi-disk regressions

Run:

```sh
make test-phase3
```

Check:

- `DISK021` appears only when no validated boot volume or partitions were detected; in that state the kernel must not invent a valid `A:` entry
- drive switching `A:`..`C:`
- negative cases such as invalid cross-drive operations
- file isolation between volumes

## GDB Notes

- The kernel symbols come from `build/kernel.elf`.
- The kernel is linked at physical address `0x10000`, so kernel symbols line up once the bootloader jumps into protected mode.
- Useful breakpoints:

```gdb
break kernel_main
break paging_init
break interrupts_init
break scheduler_runtime_init
break scheduler_phase5_self_test
```

## Operator Rule

When a change alters boot flow, image layout, or serial markers, update this file and `docs/TEST_SCRIPTS.md` in the same task.
