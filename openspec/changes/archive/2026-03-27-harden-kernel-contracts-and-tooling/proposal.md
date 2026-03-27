## Why

`docs/TODO.md` consolidates the hardening backlog that emerged after the latest architectural review, but it still lives as an informal list spread across code, docs, build scripts, and tests. The repository now needs an explicit change contract that turns that backlog into an implementation-ready program with auditable outcomes, clear ownership boundaries, and deterministic validation expectations.

Without that contract, the project keeps the current mismatch between what the documentation says is "done" and what the codebase actually proves. The result is recurring ambiguity around failure handling, subsystem ownership, floppy/FAT12 support, build reproducibility, and test determinism.

## What Changes

- Treat the hardening backlog as a `docs/tooling`-owned program that defines the required contracts, sequencing, and acceptance markers before implementation work starts.
- Add explicit requirements for critical-path failure handling, deterministic serial markers, and degraded-mode policy across `kernel/core`, `storage`, `process`, and shell startup.
- Define the refactor contract for oversized modules, duplicate keyboard paths, and ownership boundaries so implementation can be split into subsystem-scoped follow-up work.
- Align roadmap language, shell/build strings, and floppy/FAT12 documentation with the runtime behavior that the kernel actually supports today.
- Define reproducible image-build expectations so `scripts/build_disk.sh` stops relying on ambiguous fallback paths and fragile post-build patching.
- Define deterministic validation requirements for the QEMU harness and serial marker protocol, including negative coverage for scheduler, syscall, and storage-failure paths.

## Non-goals

- This change does not implement every kernel, storage, shell, and test refactor in one patch.
- This change does not redefine the userland ABI beyond what is needed to document and validate existing runtime contracts.
- This change does not introduce new product features; it hardens and clarifies the behavior already claimed by the repository.

## Capabilities

### New Capabilities

- `kernel-failure-contracts`: explicit invariants, failure responses, and deterministic serial observability for critical kernel paths.
- `kernel-module-ownership`: audited ownership boundaries and required refactor outcomes for oversized or duplicated kernel modules.
- `runtime-contract-narrative`: documentation and operator-facing strings that describe the actual runtime, floppy, FAT12, and maturity contract.
- `deterministic-image-build`: reproducible image-build requirements that remove ambiguous environment fallbacks and fragile metadata patching.
- `deterministic-validation-harness`: deterministic test-harness requirements driven by versioned serial markers instead of timing heuristics.

### Modified Capabilities

- None.

## Impact

- Affected docs: `docs/TODO.md`, `docs/HARD_CRITICS.md`, `docs/DEVELOPMENT_PROTOCOL.md`, `docs/ROADMAP.md`, `docs/DEBUGGING.md`, `docs/TEST_SCRIPTS.md`
- Affected code areas to be covered by follow-up implementation: `src/kernel/core/`, `src/kernel/process/`, `src/kernel/storage/`, `src/kernel/input/`, `src/kernel/shell/`, `scripts/build_disk.sh`, `tests/`
- Primary owner for this change contract: `docs/tooling`
- Expected validation tier for implementation spawned from this change: Tier 2 for subsystem-scoped hardening work, escalating to Tier 3 for cross-cutting closure
