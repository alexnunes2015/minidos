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
- **Paging (Current)**: Minimal paging is active. The kernel installs an identity map for the first `8 MiB` of physical memory plus the VESA framebuffer region, then enables `CR0.PG` after serial/log init.
- **Graphics Scratch Buffer (Current)**: The software backbuffer no longer lives in the kernel `.bss`. It uses a dedicated scratch window starting at `0x00400000`, which keeps the early boot stack and the app load window (`0x00200000..0x00300000`) separate from large GUI frame buffers while staying inside the initial identity map. The reserved window remains in place, but the fast-present path is currently disabled while the preemptive renderer is hardened.
- **Text Grid Scratch Buffer (Current)**: The graphics-mode text backing store also no longer lives in the flat kernel `.bss`. It now uses a fixed scratch window at `0x00380000`, which keeps the linked kernel image below the legacy VGA aperture (`0xA0000..0xBFFFF`) and avoids colored artefacts caused by banked VGA memory aliasing runtime globals.
- **Interrupt Handling (Current)**: A runtime IDT/ISR path is active for CPU exceptions (`0-31`) and PIC IRQs (`32-47`), with serial diagnostics and controlled panic behavior for kernel exceptions.
- **Scheduler Runtime (Current)**: Phase 5 now boots a real scheduler runtime on top of IRQ0. The scheduler keeps the bootstrap thread plus an idle thread, can spawn additional workers from fabricated IRQ-return frames, supports blocking-by-tick plus voluntary yield through a software interrupt, keeps every scheduler-managed kernel stack behind an unmapped guard page in a reserved arena at `0x00600000`, and updates a TSS `esp0` so ring3 app traps return to the correct kernel stack.
- **Kernel Time Base (Current)**: Runtime delays, shell `sleep`, splash pacing, and panic grace periods now derive from the IRQ0 kernel tick instead of local PIT polling or CPU-speed-dependent busy loops.

### 3. Drivers
- **Video**: Direct memory access to `0xB8000` is used for text mode. In graphics mode, the kernel now keeps a software backbuffer in the reserved scratch window and flushes completed frames to the VESA linear framebuffer through the C present path. The old ASM fast-present route remains disabled while it is hardened for the preemptive scheduler.
- **Keyboard**: IRQ1-driven input is enabled (PIC remapped + unmask IRQ1). The driver now reads the PS/2 controller translation bit to decode scan-code set 1 or set 2 correctly, while keeping a direct port-poll fallback when IRQ delivery is not enough on its own.
- **Mouse (Current)**: A PS/2 mouse path is active on IRQ12. The kernel enables the auxiliary device, decodes standard 3-byte packets, clamps the pointer to the current framebuffer size, and exposes mouse snapshots to external apps through the shell syscall surface.
- **File System**: The build image is still a FAT12 floppy for boot compatibility, but the runtime filesystem contract is asymmetric. The boot floppy is exposed as a BIOS-backed whole-disk volume when valid, while ATA-backed volumes use the active FAT16 path. There is no standalone FAT12 runtime driver today.
- **Boot Disk Access**: When the system boots from floppy media, the kernel uses a BIOS disk thunk to keep accessing the boot disk after entering protected mode.
- **ATA Disk**: PIO LBA read/write paths are implemented for secondary disks (`disk_id` 0..3 physical ATA targets, shifted when the boot media is a floppy).
- **Kernel Layout (Current)**: Kernel code is now grouped by subsystem under `src/kernel/` (`core`, `video`, `storage`, `input`, `time`, `debug`, `memory`, `process`, `shell`) so build paths and ownership lines follow runtime responsibilities instead of a flat directory.
- **Storage Boundary (Current)**: The FAT16 runtime is no longer concentrated in one implementation file. `storage/fat16.c` now owns BPB/FAT parsing and cluster-chain state, `storage/fat16_dir.c` owns directory traversal and entry lifecycle, and `storage/fat16_file.c` owns file read/write/copy flows. That reduces coupling between metadata mutations and data-path I/O without changing the external shell contract.
- **Shell Boundary (Current)**: The shell subsystem now lives under `src/kernel/shell/`. Built-in command parsing and shared shell state remain in `shell/shell.c`, while system/UI built-ins route through `shell/shell_builtin.c`, the external app ABI and loaders live in `shell/shell_apps.c`, and FAT-oriented built-ins (`dir`, `cd`, `type`, `mkdir`, `rmdir`, `del`, `copy`, `move`, `elfls`) are dispatched through `shell/shell_fs.c`. That keeps the userland contract and filesystem command surface isolated from the main shell dispatcher.
- **Userland (Current)**: External ELF32 apps and legacy flat `.COM` apps are loaded into per-app 1 MiB slots, mapped into ring3 with `U/S` page permissions, and entered through a real `iret` handoff with dedicated user stacks plus an `int 0x80` syscall gate. Foreground and background launches both run as scheduler-managed user tasks; child threads inherit the same app image and leader PID; `kill <pid>` terminates the whole app group; and direct user faults now terminate only the offending app instead of panicking the kernel.
- **Boot Media Direction (Target)**: The project still targets a floppy boot experience, but that currently means "boot image + BIOS-backed boot-volume access", not full parity between FAT12 floppy support and the ATA/FAT16 runtime stack.
- **Boot Media Gap (Current)**: The current build/runtime path supports the boot floppy through the BIOS thunk, but there is still no native FDC driver, no general support for non-boot floppy devices, and no restored FAT12 runtime layer. Secondary storage remains ATA-centric.

## Trade-offs
- **Polling vs Interrupts**: The kernel now uses interrupts as default runtime path. A small polling fallback remains in keyboard input as a compatibility bridge while IRQ-first behavior is stabilized.
- **Real Mode BIOS vs PM I/O**: The kernel code is structured to be extensible. Using Port I/O for keyboard allows it to work in Protected Mode, while the disk driver still expects a sector-reading bridge.

## Current Limitations
- ELF loader is intentionally minimal (ELF32 `ET_EXEC`, fixed low-memory window, no relocations/dynamic linking).
- The ELF loader is still intentionally small: ELF32 `ET_EXEC` only, fixed 1 MiB user slot per app, no relocations, and no demand paging.
- The COM loader is still a raw flat-binary path pinned at `0x00200000`, with no relocations or metadata beyond the file length and the shared runtime block injected by the kernel.
- The current isolation model still clones the kernel's low-memory identity map into every app directory. User code is fenced by `U/S` permissions and syscall pointer validation, but the kernel does not yet have a fully separate higher-half address space or per-process virtual memory layout.
- The boot floppy path depends on BIOS INT 13h through the thunk. That is sufficient for the current product model, but it is not a substitute for a native floppy controller driver if hardware coverage becomes a goal.

## Next Technical Steps
- Replace the fixed per-app slot model with richer per-process virtual memory management and stronger accounting than the current kernel-stack-only `MEM` field.
- Strengthen interrupt diagnostics and add targeted automated tests for exception/IRQ regressions.
- Keep build, test, and debug contracts synchronized with [docs/DEVELOPMENT_PROTOCOL.md](docs/DEVELOPMENT_PROTOCOL.md).
