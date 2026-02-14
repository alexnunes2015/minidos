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
- **Page Fault Handling**: An IDT entry for vector 14 (`#PF`) is installed before enabling paging, with serial diagnostics (`CR2`, error code, `EIP`) and controlled panic halt.

### 3. Drivers
- **Video**: Direct memory access to `0xB8000` is used. This is faster and more flexible than BIOS calls once outside of the bootloader.
- **Keyboard**: Polling I/O port `0x60` and `0x64`. While interrupts (IRQ1) are better, polling is sufficient for a single-tasking MVP shell.
- **File System**: FAT16 is implemented. It reads the root directory and directories, and loads files using cluster chains.
- **ATA Disk**: PIO LBA read/write paths are implemented, currently limited to primary master (`disk_id` 0).

## Trade-offs
- **Polling vs Interrupts**: Polling was chosen for keyboard input to keep the kernel simple and avoid IDT (Interrupt Descriptor Table) setup in the first version.
- **Real Mode BIOS vs PM I/O**: The kernel code is structured to be extensible. Using Port I/O for keyboard allows it to work in Protected Mode, while the disk driver still expects a sector-reading bridge.

## Current Limitations
- ATA multi-disk support is not complete (non-zero `disk_id` returns error in the low-level disk path).
- Interrupt-driven input and exception handling are not yet the default runtime path.

## Next Technical Steps
- Expand ATA support beyond primary master and validate partition handling across devices.
- Evolve from polling-first input toward a stronger interrupt-based architecture.
