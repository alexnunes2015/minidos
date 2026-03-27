## Context

`docs/TODO.md` captures a broad hardening backlog that spans kernel failure handling, subsystem boundaries, operator-facing documentation, build reproducibility, and test determinism. The backlog is intentionally ordered: contracts and observability first, structural cleanup second, documentation and narrative alignment third, reproducible build behavior fourth, and deterministic validation last.

That breadth conflicts with the repository's own rule that one implementation task must have one primary subsystem owner. The design therefore has to do two things at once: preserve the full backlog as one coherent change contract, while forcing any eventual implementation to move through bounded follow-up work that stays auditable and testable.

The current system is technically ahead of what the repository can currently prove. Phase 5 is marked complete, but `docs/HARD_CRITICS.md` still calls out silent degradation, ambiguous floppy/FAT12 ownership, fragile build steps, duplicate keyboard authority, and tests that infer readiness through sleeps and output timing. This change exists to turn those criticisms into explicit system requirements rather than informal guidance.

## Goals / Non-Goals

**Goals:**
- Establish one authoritative hardening contract that covers every item in `docs/TODO.md`.
- Define how the backlog is split into implementation phases without violating the one-owner-per-task protocol.
- Make failure policy, serial observability, documentation language, build behavior, and test markers part of the same auditable contract.
- Force future implementation to update docs, tests, and operator guidance in the same step as any behavioral change.

**Non-Goals:**
- Landing the full backlog as one code patch.
- Redesigning unrelated kernel areas such as new memory models, new filesystems, or new graphics features.
- Committing to a full native FAT12/FDC stack in this change before the project explicitly chooses that direction.

## Decisions

### 1. Represent the backlog as one coordinating change, but phase implementation by subsystem owner

This OpenSpec change remains a single umbrella because the user asked for "everything in TODO.md" and because the backlog items depend on each other in a strict order. Actual implementation work, however, must be emitted as bounded follow-up changes or commits that each keep one primary owner (`docs/tooling`, `paging/interrupts`, `disk/FAT`, `shell`, `scheduler`, or `tests/tooling`) and run the matching validation tier.

- **Why:** it keeps one canonical backlog contract while still respecting the repository's change-boundary rule.
- **Alternative considered:** split the proposal itself into five unrelated changes immediately. Rejected because it would lose the dependency ordering encoded in `docs/TODO.md` and make it easier to "finish" later phases without closing the earlier contract gaps.

### 2. Introduce a contract stack with separate roles for protocol, subsystem contracts, and operator docs

`docs/DEVELOPMENT_PROTOCOL.md` remains the process-level rulebook, while a new or expanded subsystem contract document (`docs/contracts.md` or equivalent) becomes the source of truth for invariants, degraded-mode policy, and deterministic marker IDs. Operator-facing docs (`docs/DEBUGGING.md`, `docs/ROADMAP.md`, `docs/TEST_SCRIPTS.md`) then consume that contract instead of free-form prose.

- **Why:** the current protocol doc is too high level to carry runtime invariants, but the runtime also lacks a single technical contract document.
- **Alternative considered:** keep expanding `docs/DEVELOPMENT_PROTOCOL.md` until it also contains marker catalogs and subsystem fault policy. Rejected because it mixes workflow governance with runtime contracts and makes drift harder to spot.

### 3. Classify every fallback as hard-fail, auditable degraded mode, or decorative fallback

Each currently ambiguous behavior must move into one of three explicit buckets:
- **Hard-fail:** critical-path violations such as fabricated storage topology or corrupted boot metadata.
- **Auditable degraded mode:** startup continues, but a stable marker and documented reason are emitted, for example when `AUTOEXEC.AUT` is skipped because storage is unavailable.
- **Decorative fallback:** non-critical UX behavior can fall back safely, for example a missing wallpaper, but the fallback must still be documented.

- **Why:** the code currently mixes these categories and sometimes invents plausible runtime state, which hides failures.
- **Alternative considered:** fix silent skips case-by-case during implementation. Rejected because it would keep the project without a consistent failure policy.

### 4. Freeze ownership before deep refactors, then extract shared primitives

The architecture cleanup phase will first define which module owns each domain, then remove duplicate authority, then split oversized files and centralize reused helpers such as port I/O and fixed physical-memory access wrappers. The order matters: if the repository starts splitting large files before deciding authority, it risks copying ambiguity into more files.

- **Why:** duplicate keyboard paths and oversized shell/storage/core files are auditability problems, not only style problems.
- **Alternative considered:** refactor the largest files first and decide ownership later. Rejected because the refactor would likely preserve multiple partial truths.

### 5. Make build selection and metadata generation explicit, then bind tests to the same observable contract

`scripts/build_disk.sh` must stop silently drifting between `mtools` and `sudo` flows and must fail clearly when prerequisites are missing. The `kernel_sectors` handoff also needs to move from brittle text scraping and binary patching into an explicit build step with reproducible inputs. Once build behavior is deterministic, the QEMU harness can stop relying on `time.sleep` and bind readiness checks to the same versioned serial markers defined in the contract docs.

- **Why:** build determinism and test determinism are coupled. A flaky image pipeline weakens every test that follows.
- **Alternative considered:** harden the harness first while leaving the image pipeline as-is. Rejected because tests would still be validating artifacts produced through ambiguous paths.

## Risks / Trade-offs

- **[Risk]** The umbrella change may tempt future implementation to become too broad. -> **Mitigation:** tasks explicitly require subsystem-bounded follow-up work and validation by tier.
- **[Risk]** Replacing silent degradation with explicit failure could break current demos or smoke tests. -> **Mitigation:** define degraded-mode rules up front and add negative tests in the same phase.
- **[Risk]** A marker catalog can drift from code if it is treated as documentation only. -> **Mitigation:** require tests and operator docs to reference the same IDs and treat drift as a contract violation.
- **[Trade-off]** Hardening docs and tooling first delays feature work. -> **Accepted** because the backlog exists specifically to stop shipping features on top of ambiguous contracts.

## Migration Plan

1. Land the contract artifacts that define subsystem invariants, failure classes, marker IDs, and implementation sequencing.
2. Execute the failure-handling phase against `kernel/core`, scheduler bootstrap, shell startup, and `storage/drive.c`, replacing silent or synthetic behavior with explicit outcomes.
3. Execute ownership cleanup as subsystem-scoped refactors, starting with keyboard authority and the most audit-sensitive oversized modules.
4. Align roadmap language, shell/build strings, and floppy/FAT12 operator docs with the contract established in the first phase.
5. Rework `scripts/build_disk.sh` and related metadata generation so the image pipeline becomes explicit and reproducible.
6. Refactor the QEMU harness and targeted suites to consume the versioned marker catalog, then close the program with Tier 3 validation.

Rollback strategy: revert the latest subsystem-scoped implementation change if it destabilizes boot, storage, or tests, but keep the contract docs only if they still describe the reverted state. If the docs no longer match reality, revert the corresponding contract update in the same rollback.

## Open Questions

- Should FAT12 remain a compatibility-only boot path, or does the project want to promote it to a first-class, separately owned storage capability?
- Should the stage2 `kernel_sectors` handoff be solved by linker-generated metadata, assembler include generation, or another build-time manifest step?
- Which existing tests currently depend on the synthetic `A:` drive or other soft-failure behavior, and do they need explicit fixture images before those paths are removed?
