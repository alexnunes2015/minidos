## 1. Hard Contracts and Observability

- [x] 1.1 Add `docs/contracts.md` (or equivalent) with subsystem invariants, failure classes, and the initial marker catalog, and link it from `docs/DEVELOPMENT_PROTOCOL.md` (validate: `rg -n "Hard-fail|Degraded mode|marker" docs/contracts.md docs/DEVELOPMENT_PROTOCOL.md`)
- [x] 1.2 Replace the silent `run_auto_script` skip with an explicit degraded-mode marker and document the recovery policy in `docs/DEBUGGING.md` (validate: `make test-serial`)
- [x] 1.3 Normalize scheduler bootstrap failure markers and user-fault containment markers against the published contract (validate: `make test-phase5 && make test-user-isolation`)
- [x] 1.4 Remove synthetic `A:` drive creation outside isolated test mode and prove explicit storage-failure handling (validate: `make test-phase3`)

## 2. Architecture and Ownership Cleanup

- [x] 2.1 Publish the subsystem owner map and split targets for `kernel.c`, `shell_apps.c`, `shell_builtin.c`, and `fat16_dir.c` in the contract docs (validate: `rg -n "kernel.c|shell_apps.c|shell_builtin.c|fat16_dir.c" docs/contracts.md docs/DEVELOPMENT_PROTOCOL.md`)
- [x] 2.2 Split `src/kernel/shell/shell_builtin.c` and `src/kernel/shell/shell_apps.c` into narrower responsibility units without regressing shell app execution (validate: `make phase0-check && make test-phase4`)
- [x] 2.3 Split `src/kernel/storage/fat16_dir.c` and reduce `src/kernel/core/kernel.c` to orchestration-only startup flow (validate: `make test-phase3 && make test-phase5`)
- [x] 2.4 Collapse duplicate keyboard authority to one active path and centralize duplicated low-level helpers behind shared infrastructure (validate: `make test-keyboard-soft && make phase0-check`)

## 3. Documentation Voice and Runtime Narrative

- [x] 3.1 Replace elastic roadmap status language with binary, evidence-linked status labels and explicitly flag fragile or uncovered areas (validate: `rg -n "validated|fragile|uncovered|delivered" docs/ROADMAP.md`)
- [x] 3.2 Update the `ver` output and bundled `PTEST/README.TXT` text so they describe the scheduler-owned app runtime and BIOS-backed boot volume accurately (validate: `make test-ver && make test-phase4`)
- [x] 3.3 Publish one explicit FAT12/floppy policy and align `docs/DESIGN.md`, `docs/ROADMAP.md`, and the new contract doc with that choice (validate: `rg -n "FAT12|floppy" docs/DESIGN.md docs/ROADMAP.md docs/contracts.md`)

## 4. Build and Tooling Reproducibility

- [x] 4.1 Make `scripts/build_disk.sh` choose a supported image-build path explicitly and fail fast when prerequisites are missing (validate: `make verify-image`)
- [x] 4.2 Replace the current stage2 `kernel_sectors` scrape-and-patch flow with a structured metadata generation step in the build pipeline (validate: `make verify-image && make phase0-check`)
- [x] 4.3 Tighten generated-artifact hygiene for `build/`, `.venv/`, and `tests/__pycache__/` and confirm a clean rebuild keeps the same verification result (validate: `make clean && make verify-image && make phase0-check`)

## 5. Deterministic Validation Harness

- [x] 5.1 Refactor `tests/qemu_harness.py` and `tests/test_serial.py` to wait on documented serial markers instead of arbitrary sleeps wherever a marker exists (validate: `make test-serial && make test-phase5`)
- [x] 5.2 Refactor keyboard and mouse interaction suites to use the shared marker contract rather than timing heuristics (validate: `make test-keyboard-soft && make test-mouse`)
- [x] 5.3 Add negative coverage for scheduler isolation, syscall misuse, and storage-failure handling, including any new focused suite required by the contract (validate: `make test-phase3 && make test-user-isolation && make ci`)

## 6. Closure and Tiered Validation

- [x] 6.1 Re-audit docs, shell strings, and debug instructions for maturity drift after the implementation phases land (validate: `make verify-image && make phase0-check`)
- [x] 6.2 Run the full Tier 3 closeout for the hardening program and fix any remaining drift before archive (validate: `make ci`)
