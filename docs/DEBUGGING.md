# MiniDOS Debugging Guide

This guide is for fast diagnosis in an AI-agent-first workflow. Use the shortest loop that can prove or localize the problem.

## Fast Commands

```sh
make verify-image
make phase0-check
make run-no-reboot
make run-trace
make run-gdb
make gdb-kernel
```

## What Each Command Is For

- `make verify-image`: validates the floppy image layout, BPB, patched kernel sector count, and expected files inside the FAT volume.
- `make phase0-check`: clean rebuild plus serial smoke test.
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
- `[sched] phase5 context-switch self-test OK`
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
- serial output reaches the PM checkpoints

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

### Shell is up but commands fail

Run:

```sh
make phase0-check
python3 tests/test_serial.py "ver" "drives" "dir"
```

Check:

- `Entering main loop` appears before the harness starts injecting serial commands
- `Command:` serial markers
- current drive enumeration
- FAT init logs

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
break scheduler_phase5_self_test
```

## Operator Rule

When a change alters boot flow, image layout, or serial markers, update this file and `docs/TEST_SCRIPTS.md` in the same task.
