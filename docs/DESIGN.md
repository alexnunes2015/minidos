# MiniDOS Architectural Design

## Overview
MiniDOS is a minimalist operating system inspired by the MS-DOS era. It targets the x86 16-bit real mode for bootstrapping and transitions to a minimal 32-bit protected mode (or stays in 16-bit) to allow C implementation without requiring a complex runtime.

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
- **32-bit Flat Mode (Planned/MVP)**: The current GCC toolchain targets `i386-elf`. To run this, the bootloader (or a stage 2) would switch to Protected Mode. In this MVP, we use `-m32 -ffreestanding` to generate code that can be easily ported, though a real switch to PM is ideally handled in assembly before calling `kernel_main`.

### 3. Drivers
- **Video**: Direct memory access to `0xB8000` is used. This is faster and more flexible than BIOS calls once outside of the bootloader.
- **Keyboard**: Polling I/O port `0x60` and `0x64`. While interrupts (IRQ1) are better, polling is sufficient for a single-tasking MVP shell.
- **File System**: FAT16 is implemented. It reads the root directory and directories, and loads files using cluster chains.

## Trade-offs
- **Polling vs Interrupts**: Polling was chosen for keyboard input to keep the kernel simple and avoid IDT (Interrupt Descriptor Table) setup in the first version.
- **Real Mode BIOS vs PM I/O**: The kernel code is structured to be extensible. Using Port I/O for keyboard allows it to work in Protected Mode, while the disk driver still expects a sector-reading bridge.
