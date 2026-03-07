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
- `[int] IDT active, PIC remapped, IRQ0/IRQ1 enabled`
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
