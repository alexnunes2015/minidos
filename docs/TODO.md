-# TODO

## Execution order
1. Hard contracts, observability and failure handling (section 1) — ensures every critical subsystem emits deterministic markers and fails safely before anything else.
2. Architecture and ownership cleanup (section 2) — breaks oversized modules and aligns subsystem ownership so deeper changes remain testable.
3. Documentation voice and product narrative (section 3) — updates roadmap, strings and floppy/FAT12 narrative so the story matches reality while contracts settle.
4. Build/tooling reproducibility (section 4) — locks the image pipeline so subsequent tests and docs truly reflect deterministic artifacts.
5. Test determinism (section 5) — hardens the validation suite once build and docs can supply reliable inputs.
6. Immediate actions (section 6) — orchestrate the “Rule of Gold” hygiene sweep, cleaning artifacts and saturating the TODO list.

## 1. Hard contracts, observability and failure handling
- Write a new `docs/contracts.md` or extend `docs/DEVELOPMENT_PROTOCOL.md` with explicit invariants for each critical subsystem (`kernel/core`, `scheduler`, `storage`, `shell`) and describe expected responses for invalid inputs, timeouts, and hardware faults referenced in `docs/HARD_CRITICS.md:1`.
- Audit `kernel_runtime_thread` (`src/kernel/core/kernel.c`), the scheduler bootstrap (`src/kernel/process/scheduler.c`), `drive.c`, and the boot panic helpers so that every panic path emits deterministic serial markers and the Recovery policy described in `docs/HARD_CRITICS.md:7` is explicitly documented.
- Replace silent skips (e.g., `run_auto_script` when FAT16 is missing and the artificial volume creation in `src/kernel/storage/drive.c`) by raising auditable errors or a well-defined degraded mode and log the change in `docs/DEBUGGING.md`.

## 2. Architecture and ownership cleanup
- Split the largest modules (`src/kernel/shell/shell_apps.c`, `src/kernel/shell/shell_builtin.c`, `src/kernel/storage/fat16_dir.c`, `src/kernel/core/kernel.c`) into smaller, testable units with single responsibility and record the ownership map in `docs/DEVELOPMENT_PROTOCOL.md` (section on subsystem ownership) as prompted by `docs/HARD_CRITICS.md:2`.
- Remove duplicate keyboard implementations (`src/kernel/keyboard.c` vs `src/kernel/input/keyboard.c`) and decide on one authoritative path; note the chosen owner in `docs/DEVELOPMENT_PROTOCOL.md`.
- Consolidate low-level helpers (`inb/outb`, physical reads, etc.) into shared infrastructure so multiple files stop re-implementing the same helpers before finishing the refactor.

## 3. Documentation voice and product narrative
- Update `docs/ROADMAP.md` to use binary, verifiable status labels instead of the current “concluída” definition and call out phases that still depend on “self-test markers” as partial (`docs/HARD_CRITICS.md:3` and `:8`).
- Audit build scripts and shell strings: replace the stale `PTEST/README.TXT` claim that ELFs still run on the shell thread and the `ver` output (“boot floppy FAT12 + FAT16 runtime”) with explanations that reflect the actual FAT16/ATA runtime plus BIOS-backed boot volume.
- Document the policy for `floppy-first` support—either restore a full FAT12 stack or explain the compatibility-only handshake as described in `docs/HARD_CRITICS.md:4`.

## 4. Build/tooling reproducibility
- Reduce the branching between `mtools`/`sudo` paths in `scripts/build_disk.sh`, or automatically fail if neither environment is available so that the script never silently chooses a fallback, per `docs/HARD_CRITICS.md:5`.
- Replace the `awk`/Python patch (lines 80‑120) and move the `kernel_sectors` metadata into the build pipeline itself, eliminating fragile manual binary patching referenced in the same section.
- Clean generated clutter from `build/`, `.venv/`, and `tests/__pycache__/` by tightening `.gitignore` and confirming `docs/ROADMAP.md`’s `phase0-check` instructions deliver the same status after cleanup.

## 5. Test determinism
- Refactor `tests/test_serial.py`, `tests/test_keyboard_irq.py`, `tests/test_mouse_ui.py`, and `tests/qemu_harness.py` to rely on serial markers instead of `time.sleep` and polling loops; codify the new marker list in `docs/TEST_SCRIPTS.md` with deterministic IDs (`docs/HARD_CRITICS.md:6`).
- Add negative tests for scheduler isolation, syscall errors, and storage failures so the current “self-test-driven” confidence in Phase 5 is backed by real contract coverage (see `docs/HARD_CRITICS.md:8`).
- Track the new test invariants in `docs/DEVELOPMENT_PROTOCOL.md` under “Observability Rules” so every marker is versioned and documented.

## 6. Immediate actions
1. Update docs/strings that still advertise a maturity this repo does not have.
2. Remove synthetic drive creation outside isolated tests (and replace it with explicit failure handling).
3. Break oversized modules and fix duplicate keyboard paths with owners.
4. Clarify FAT12/floppy contract and correct drift in `scripts/build_disk.sh`.
5. Harden the build pipeline and document the deterministic marker protocol.
6. Clean duplicate/inferred code and artifact noise (`build/`, `.venv/`, `tests/__pycache__`).
7. Ensure every change obeys the “Rule of Gold” question from `docs/HARD_CRITICS.md`.
