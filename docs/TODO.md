# TODO

## Execution order
1. Hard contracts, observability and failure handling (section 1) — finish auditing fatal and degraded paths against `docs/contracts.md`.
2. Architecture and ownership cleanup (section 2) — break oversized modules and keep subsystem authority unambiguous.
3. Documentation voice and product narrative (section 3) — keep roadmap/status language and operator-facing strings aligned with the real contract.
4. Build/tooling reproducibility (section 4) — remove avoidable environment branching and fragile post-link patching from the image pipeline.
5. Test determinism (section 5) — replace sleep/polling heuristics with marker-driven synchronization and broaden negative coverage.
6. Immediate actions (section 6) — keep the next tactical steps short, auditable, and tied to the “Rule of Gold”.

## 1. Hard contracts, observability and failure handling
- Keep `docs/contracts.md` as the source of truth and finish auditing `kernel_runtime_thread`, the scheduler bootstrap, storage init, and panic helpers so every fatal/degraded path maps to a contractual marker and documented recovery policy.
- Replace remaining implicit degraded paths (notably keyboard polling fallback and device-disable paths such as the PS/2 mouse busy case) with explicit markers plus documented degraded-mode behavior, or promote them to hard-fail when the subsystem contract requires it.
- Continue aligning `docs/DEBUGGING.md` and `docs/TEST_SCRIPTS.md` with the marker catalog whenever new IDs are added or old prose logs become contractual.

## 2. Architecture and ownership cleanup
- Split the remaining oversized critical modules (`src/kernel/shell/shell_apps.c`, `src/kernel/process/scheduler.c`, `src/kernel/video/video.c`, `src/boot/stage2.asm`) into smaller units with single responsibility and bounded validation impact.
- Keep `src/kernel/input/keyboard.c` as the authoritative keyboard implementation and prevent compatibility shims/headers from growing back into a second API surface.
- Consolidate low-level helpers (`inb/outb`, physical reads, shared polling/wait helpers) into shared infrastructure so unrelated files stop re-implementing the same primitives.

## 3. Documentation voice and product narrative
- Keep `docs/ROADMAP.md` on evidence-linked status labels only; do not reintroduce phase-heading language that implies stronger closure than the documented `validated` / `delivered` / `fragile` / `uncovered` state.
- Continue auditing build scripts, bundled image text, shell strings, and README/docs text so they do not regress to the old shell-thread or FAT12 parity narrative.
- Treat the current floppy/FAT12 story as settled only while the repository remains on the compatibility-only contract; if that changes, update `docs/contracts.md`, `docs/DESIGN.md`, and the operator-facing strings in the same task.

## 4. Build/tooling reproducibility
- Reduce the branching between `mtools` and `sudo` in `scripts/build_disk.sh`, or fail earlier and more explicitly when the required environment is unavailable.
- Replace the `stage2` metadata patch flow with a build-time source of truth for `kernel_sectors`, removing the current JSON/Python post-link binary patch step.
- Clean generated clutter from `build/`, `.venv/`, and `tests/__pycache__/` by tightening ignores and validating that `phase0-check` still proves the same baseline after cleanup.

## 5. Test determinism
- Refactor `tests/test_serial.py`, `tests/test_keyboard_irq.py`, `tests/test_mouse_ui.py`, and `tests/qemu_harness.py` to rely on contractual markers instead of `time.sleep` and ad-hoc polling loops.
- Add broader negative coverage for scheduler isolation, starvation/cleanup behavior, and syscall error handling; storage failure coverage already exists via `tests/test_storage_failure.py`.
- Keep the test invariants mirrored in `docs/DEVELOPMENT_PROTOCOL.md` and `docs/TEST_SCRIPTS.md` so every marker-driven wait condition is versioned and documented.

## 6. Immediate actions
1. Remove the remaining stale “done” language from docs when the same section still declares `fragile` or `uncovered` caveats.
2. Eliminate IRQ/polling degraded paths or make them fully contractual and testable.
3. Break the remaining oversized critical modules with clear owner boundaries.
4. Harden the image-build pipeline by reducing environment branching and removing fragile stage2 patching.
5. Convert the lingering sleep/poll loops in the harness/tests to marker-driven synchronization and expand the negative scheduler/syscall matrix.
