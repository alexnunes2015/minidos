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
- **32-bit Protected Mode (Current)**: The stage2 bootloader performs the PM transition before transferring control to `kernel_main`. The kernel is built with `-m32 -ffreestanding`.
- **Paging (Current)**: Minimal paging is active. The kernel installs an identity map for boot-critical low memory plus the VESA framebuffer region, then enables `CR0.PG` after serial/log init.
- **Interrupt Handling (Current)**: A runtime IDT/ISR path is active for CPU exceptions (`0-31`) and PIC IRQs (`32-47`), with serial diagnostics and controlled panic behavior for kernel exceptions.
- **Scheduler Prep (Current)**: A phase-5 scheduler base is present with process metadata (`PID`, state, saved `ESP`), PIT setup on IRQ0, cooperative context-switch self-test at boot, and an initial IRQ-return preemption hook driven by quantum ticks.

### 3. Drivers
- **Video**: Direct memory access to `0xB8000` is used. This is faster and more flexible than BIOS calls once outside of the bootloader.
- **Keyboard**: IRQ1-driven input is enabled (PIC remapped + unmask IRQ1), with temporary fallback polling in the keyboard path for transitional robustness.
- **File System**: FAT16 is implemented. It reads the root directory and directories, and loads files using cluster chains.
- **ATA Disk**: PIO LBA read/write paths are implemented for `disk_id` 0..3 (primary/secondary, master/slave).
- **Userland (Current)**: External ELF32 apps are loaded from FAT16 and executed in protected mode with a minimal syscall ABI (`puts`, `get_char`, `file_size`) exposed by the shell runtime contract.

## Trade-offs
- **Polling vs Interrupts**: The kernel now uses interrupts as default runtime path. A small polling fallback remains in keyboard input as a compatibility bridge while IRQ-first behavior is stabilized.
- **Real Mode BIOS vs PM I/O**: The kernel code is structured to be extensible. Using Port I/O for keyboard allows it to work in Protected Mode, while the disk driver still expects a sector-reading bridge.

## Current Limitations
- ELF loader is intentionally minimal (ELF32 `ET_EXEC`, fixed low-memory window, no relocations/dynamic linking).
- Preemption hook is active, but runtime process population is still minimal (kernel-only path by default), and user/kernel stack split is not enabled yet.

## Next Technical Steps
- Expand syscall surface and isolate user/kernel memory domains as preparation for multitasking.
- Evolve phase-5 scheduler from cooperative validation to preemptive round-robin driven by IRQ0 quantum.
- Strengthen interrupt diagnostics and add targeted automated tests for exception/IRQ regressions.
