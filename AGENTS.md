# Repository Guidelines

## Project Structure & Module Organization
- `src/boot/` holds the bootloader assembly (MBR + stage2).
- `src/kernel/` contains the 16/32-bit kernel in C and ASM (entry point, drivers, shell).
- `assets/bootlogo/` stores the optional boot logo inputs/outputs (e.g., `boot_logo.bmp`, `logo.raw`).
- `scripts/` includes disk image builders and helpers (`build_disk.sh`, `create_*_disk.sh`).
- `tests/` contains QEMU-based shell scripts and an `expect` script for automated runs.
- `docs/` captures design notes, fixes, and troubleshooting.
- `build/` is generated output (objects, `kernel.bin`) and is safe to delete.

## Build, Test, and Development Commands
- `make` builds `minidos.img` (bootloader + kernel + disk image).
- `make run` boots the image in QEMU (`qemu-system-i386`).
- `make clean` removes `build/` and `minidos.img`.
- `make test` runs the default test trio (`help`, `ver`, `drives`).
- `make test-ver` / `make test-dir` / `make test-drives` run targeted tests.
- `make test-serial` runs a serial-driven smoke test (`ver`, `drives`).
- `./tests/test_runner.sh "ver" "drives"` runs custom command sequences.

Dependencies used by the build/tests include `gcc` (with `-m32`), `ld`, `nasm`, `qemu-system-i386`, and either `sudo` (loop-mount formatting) or `mtools` (`mformat`, `mcopy`).

## Coding Style & Naming Conventions
- Indentation is 4 spaces; braces are K&R style (`if (...) {` on one line).
- C symbols are lowercase with underscores (`kernel_main`, `serial_print`).
- Macros and constants are uppercase (`SERIAL_SHELL`).
- Assembly files are `.asm`, object outputs go to `build/`.
- No formatter/linter is configured; follow nearby file style.

## Testing Guidelines
- Tests are QEMU-driven shell scripts in `tests/` (plus `tests/test_shell.expect`).
- Use `make test` for the basic smoke suite, or `./tests/test.sh <name>` for specific cases.
- Test scripts expect `minidos.img` to exist; run `make` first.
- For serial-driven testing, wait for the serial log to say `Entering main loop` before sending commands.
- Use `make test-serial` (or `python3 tests/test_serial.py "ver" "drives"`) to send commands over COM1 and verify acceptance via the `Command:` serial log.

## Commit & Pull Request Guidelines
- History shows short, imperative messages; one commit uses `feat:`. Use a concise summary (optionally `type:` prefix) and avoid long paragraphs.
- PRs should include: a brief summary, commands run (e.g., `make`, `make test`), and any QEMU output notes. If behavior changes are visible in the boot logo or shell, include a screenshot.

## Debugging Notes
- The OS prints real-time debug output over the serial port (COM1) during boot and runtime. Use QEMU serial output (`-serial stdio`) or the test scripts to capture it.

## Known Issues & Next Steps
- Protected Mode transition currently boots into the kernel in serial smoke tests; keep using serial checkpoints to detect regressions early.
- Disk write path is implemented (`disk_write_lba` / `disk_write_lba_from_disk` in `src/kernel/disk.c`).
- ATA PIO access is validated for `disk_id` 0..3 in phase regression (`make test-phase3`).
- Next steps: complete process kernel/user stack separation (Fase 5), strengthen interrupt handling, and continue memory-management evolution (see `docs/DESIGN.md` and `docs/ROADMAP.md`).

## Security & Configuration Tips
- Disk formatting may require `sudo` for loop devices, or use `mtools` instead. See `scripts/build_disk.sh` for the flow.

## Agent Hygiene
- Before editing any file, check local changes since the last commit with `git status -sb` and `git diff --name-only HEAD`.
- If the target file already has local changes, review them with `git diff HEAD -- <file>` and preserve them.
- Do not revert or rewrite unrelated local changes; ignore them unless asked.
- If unexpected changes appear, stop and ask how to proceed.
