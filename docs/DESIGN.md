# MiniDOS Architectural Design

## Overview
MiniDOS is a minimalist operating system inspired by the MS-DOS era. It starts in x86 16-bit Real Mode and transitions to 32-bit Protected Mode to run a freestanding C kernel.

## Architectural Decisions

### 1. Bootloader and Real Mode
The system starts in 16-bit Real Mode, which is the native state of an x86 CPU upon power-on. Assembly (NASM) is used because:
- BIOS interrupts (INT 13h for disk, INT 10h for video) are only available in Real Mode.
- We must respect the 512-byte limit of the boot sector.
- A BIOS Parameter Block (BPB) is required for FAT compatibility.

### 2. C Kernel and Memory Model
As requested, C++ is not directly used due to the lack of a standard runtime in real mode. The kernel is written in C with the following constraints:
- **No libc**: All functions like `print_char` and `shell_execute` are implemented from scratch.
- **Memory Mapping**: The kernel is loaded at absolute address `0x10000` (segment `0x1000:0000`). This avoids the first 64KB where the IVT and BIOS data reside.
- **Stage2 Kernel Loader (Current)**: The second-stage loader now advances the `ES` segment window when the kernel image crosses a 64KiB boundary, so larger kernels continue loading correctly from the floppy image.
- **32-bit Protected Mode (Current)**: The stage2 bootloader performs the PM transition before transferring control to `kernel_main`. The kernel is built with `-m32 -ffreestanding`.
- **Paging (Current)**: Minimal paging is active. The kernel installs an identity map for boot-critical low memory plus the VESA framebuffer region, then enables `CR0.PG` after serial/log init.
- **Interrupt Handling (Current)**: A runtime IDT/ISR path is active for CPU exceptions (`0-31`) and PIC IRQs (`32-47`), with serial diagnostics and controlled panic behavior for kernel exceptions.
- **Scheduler Prep (Current Baseline)**: A phase-5 scheduler base is present with process metadata (`PID`, state, saved `ESP`), PIT setup on IRQ0, cooperative context-switch self-test at boot, and an initial IRQ-return preemption hook driven by quantum ticks. This is groundwork for multitasking, not a claim that the runtime process model is complete.
- **Kernel Time Base (Current)**: Runtime delays, shell `sleep`, splash pacing, and panic grace periods now derive from the IRQ0 kernel tick instead of local PIT polling or CPU-speed-dependent busy loops.

### 3. Drivers
- **Video**: Direct memory access to `0xB8000` is used. This is faster and more flexible than BIOS calls once outside of the bootloader.
- **Keyboard**: IRQ1-driven input is enabled (PIC remapped + unmask IRQ1). The driver now reads the PS/2 controller translation bit to decode scan-code set 1 or set 2 correctly, while keeping a direct port-poll fallback when IRQ delivery is not enough on its own.
- **Mouse (Current)**: A PS/2 mouse path is active on IRQ12. The kernel enables the auxiliary device, decodes standard 3-byte packets, clamps the pointer to the current framebuffer size, and exposes mouse snapshots to external apps through the shell syscall surface.
- **File System**: The build image is still a FAT12 floppy for boot compatibility, but the runtime filesystem contract is asymmetric. The boot floppy is exposed as a BIOS-backed whole-disk volume when valid, while ATA-backed volumes use the active FAT16 path. There is no standalone FAT12 runtime driver today.
- **Boot Disk Access**: When the system boots from floppy media, the kernel uses a BIOS disk thunk to keep accessing the boot disk after entering protected mode.
- **ATA Disk**: PIO LBA read/write paths are implemented for secondary disks (`disk_id` 0..3 physical ATA targets, shifted when the boot media is a floppy).
- **Userland (Current)**: External ELF32 apps are loaded from FAT16 and executed in protected mode with a growing syscall ABI that now covers console input, graphics primitives, ticks, and mouse snapshots/waitable UI events.
- **Boot Media Direction (Target)**: The project still targets a floppy boot experience, but that currently means "boot image + BIOS-backed boot-volume access", not full parity between FAT12 floppy support and the ATA/FAT16 runtime stack.
- **Boot Media Gap (Current)**: The current build/runtime path supports the boot floppy through the BIOS thunk, but there is still no native FDC driver, no general support for non-boot floppy devices, and no restored FAT12 runtime layer. Secondary storage remains ATA-centric.

## Trade-offs
- **Polling vs Interrupts**: The kernel now uses interrupts as default runtime path. A small polling fallback remains in keyboard input as a compatibility bridge while IRQ-first behavior is stabilized.
- **Real Mode BIOS vs PM I/O**: The kernel code is structured to be extensible. Using Port I/O for keyboard allows it to work in Protected Mode, while the disk driver still expects a sector-reading bridge.

## Current Limitations
- ELF loader is intentionally minimal (ELF32 `ET_EXEC`, fixed low-memory window, no relocations/dynamic linking).
- Preemption hook is active, but runtime process population is still minimal and scheduler coverage is currently driven mostly by self-tests and kernel-managed tasks.
- The boot floppy path depends on BIOS INT 13h through the thunk. That is sufficient for the current product model, but it is not a substitute for a native floppy controller driver if hardware coverage becomes a goal.

## Next Technical Steps
- Expand syscall surface and isolate user/kernel memory domains as preparation for multitasking.
- Evolve phase-5 scheduler from cooperative validation to preemptive round-robin driven by IRQ0 quantum.
- Strengthen interrupt diagnostics and add targeted automated tests for exception/IRQ regressions.
- Keep build, test, and debug contracts synchronized with [docs/DEVELOPMENT_PROTOCOL.md](docs/DEVELOPMENT_PROTOCOL.md).
