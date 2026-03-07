# MiniDOS Troubleshooting

Use this file for symptom-oriented diagnosis. For the full operator workflow, see [docs/DEBUGGING.md](docs/DEBUGGING.md).

## First Checks

Before debugging behavior, confirm that the image and baseline are healthy:

```sh
make verify-image
make phase0-check
```

If either command fails, fix that first.

## Symptom: Boot Stops Before The Kernel

Run:

```sh
make verify-image
make run-no-reboot
```

What to inspect:

- serial output up to `[Stage2] Entering PM...`
- boot signature `0x55AA`
- FAT12 floppy BPB values
- patched `kernel_sectors` in `stage2.bin`

Common causes:

- `boot.bin` no longer fits 512 bytes
- `stage2.bin` no longer fits the fixed 4-sector budget
- `kernel.bin` exceeds the reserved boot area
- image layout changed without updating build or docs

## Symptom: Panic Or Silent Reset After Protected Mode

Run:

```sh
make run-no-reboot
make run-trace
```

What to inspect:

- `[paging]` markers
- exception logs with `CR2`, `error`, `eip`, `cs`, `eflags`
- `build/qemu-trace.log`

If the failure is paging-specific, run:

```sh
make test-paging
```

## Symptom: Shell Appears But Commands Fail

Run:

```sh
python3 tests/test_serial.py "ver" "drives" "dir"
```

What to inspect:

- `Command:` serial markers
- FAT initialization logs
- drive enumeration and current drive

## Symptom: Keyboard Input Regressed

Run:

```sh
make test-keyboard-soft
```

If the environment supports QMP sockets:

```sh
make test-keyboard
```

What to inspect:

- whether the shell reaches `MiniDOS Shell Ready.`
- whether the injected command is accepted exactly once
- whether IRQ1 markers and shell command markers still appear in order

## Symptom: FAT Or Multi-Disk Behavior Broke

Run:

```sh
make test-phase3
```

What to inspect:

- `A:` boot floppy still mounts as a whole-disk volume
- `B:` and `C:` enumerate correctly from ATA-backed partitions
- negative cases still fail cleanly

## Symptom: ELF Apps No Longer Run

Run:

```sh
make test-phase4
```

What to inspect:

- app installation into the image
- `elfls` output
- return to shell after app execution

## VirtualBox Note

The current official debug and validation target is QEMU. The boot image is a `1.44MB` FAT12 floppy, and the kernel keeps boot-floppy access alive through a BIOS disk thunk after entering protected mode. If another emulator behaves differently, validate first in QEMU before diagnosing emulator-specific behavior.
