# MiniDOS Debugging Guide

This guide is for fast diagnosis in an AI-agent-first workflow. Use the shortest loop that can prove or localize the problem.

## Fast Commands

```sh
make verify-image
make phase0-check
make test-phase5
make test-multitask-com
make test-user-isolation
make run-no-reboot
make run-trace
make run-gdb
make gdb-kernel
```

## What Each Command Is For

- `make verify-image`: validates the floppy image layout, BPB, patched kernel sector count, and expected files inside the FAT volume.
- `make phase0-check`: clean rebuild plus serial smoke test.
- `make test-phase5`: validates the phase-5 scheduler runtime plus the negative guard-page fault path.
- `make test-multitask-com`: validates background `.COM` apps on the same scheduler/user-mode runtime used by ELFs.
- `make test-user-isolation`: validates ring3 ELF/COM containment for bad syscall pointers, stale low-memory app VAs, and direct user-mode page faults.
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
- `[kbd] scan set 1 selected (translation on)` or `[kbd] scan set 2 selected (translation off)`
- `[mouse] PS/2 mouse enabled on IRQ12`
- `[mouse] first packet received`
- `[int] IDT active, PIC remapped, IRQ0/IRQ1/IRQ12 enabled`
- `APPFLT900`
- `SCHED100`
- `SCHED110`
- `SCHED120`
- `SCHED190`
- `SCHED900`
- `[sched] phase5 context-switch self-test OK`
- `BOOT100`
- `BOOT110`
- `BOOT190`
- `MiniDOS Shell Ready.`
- `[INFO][kernel] Entering main loop`

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
- serial output reaches the PM checkpoints
- `BOOT100` appears before disk/drive probing
- `BOOT110` appears only if the runtime logo files were loaded successfully
- before `BOOT110`, the graphics placeholder is now a black screen with a blinking cursor
- `BOOT190` appears after the 5-second logo window counted from `BOOT110` and immediately before the shell takes over the screen

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
- `MEM` is still scheduler kernel-stack reserve only; it does not include the user slot mapped for ring3 apps

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
The normal boot now lands directly in the shell. The harness still tolerates a future boot-launched `WIN95UI` by dismissing it with `ESC` before continuing.
It waits for `APPIN001` before sending keys into GUI apps and for `APPRET001` before asserting that the shell resumed, so app-input races are easier to localize from serial logs.

### Mouse / GUI IRQ12 regressions

Run:

```sh
make test-mouse
```

Check:

- `[mouse] PS/2 mouse enabled on IRQ12`
- `APPIN001` after `WIN95UI` starts
- `APPRET001` after the click closes `WIN95UI` or after `q` / `ESC` return from the other apps
- `[mouse] first packet received` after QMP moves the pointer
- no `[win95ui]` serial debug lines while the GUI runs
- shell accepts `ver` after the test clicks `Cancel`
- `DOSSHELL` and `EDIT` still exit normally after mouse movement inside the app

### FAT or multi-disk regressions

Run:

```sh
make test-phase3
```

Check:

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
