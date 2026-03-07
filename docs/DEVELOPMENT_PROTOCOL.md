# MiniDOS Development Protocol

This project is optimized for an AI-agent-first workflow. The human sets direction, constraints, and acceptance criteria. The agent reads, edits, runs the tooling, and reports what changed.

## Core Model

- The human defines the goal, invariants, and stop conditions.
- The agent owns implementation, local validation, and documentation updates.
- Every non-trivial change must leave the repository in a more observable and more reproducible state than before.

## Non-Negotiable Rules

- One task must have one primary subsystem owner: `boot`, `disk image`, `paging/interrupts`, `disk/FAT`, `shell`, `scheduler`, `userland`, `docs/tooling`.
- Do not mix unrelated critical subsystems in one change. Split first.
- Any change that modifies observable behavior must update at least one automated test or one explicit validation command.
- Any change that modifies architecture, image layout, syscall surface, debug markers, or boot flow must update the corresponding docs in the same task.
- A task is not done until the agent reports the exact commands run and whether they passed.

## Required Workflow Per Task

1. Re-read local changes with `git status -sb` and `git diff --name-only HEAD`.
2. Identify the subsystem and the acceptance contract.
3. Make the smallest coherent change that satisfies the contract.
4. Run the minimum validation tier required for that subsystem.
5. Update docs if the task changed behavior, assumptions, or operator workflow.
6. Report outcome, commands run, and any residual risk.

## Validation Tiers

### Tier 0: Docs or comments only

- `make verify-image` is not required unless the task touches image/build docs.
- Run a syntax or consistency check when applicable.

### Tier 1: Shell, UX, non-critical kernel code

- `make phase0-check`
- Add targeted regression coverage when behavior changed.

### Tier 2: Boot, image layout, disk access, paging, interrupts, scheduler

- `make verify-image`
- `make phase0-check`
- Relevant focused suites:
- `make test-paging`
- `make test-keyboard-soft`
- `make test-phase3`
- `make test-phase4`

### Tier 3: Cross-cutting or release-ready changes

- `make ci`

## Done Criteria

A task is complete only when all of the following are true:

- The intended behavior exists.
- Required validation tier passed.
- Docs and debug instructions match reality.
- No known drift remains between build output, tests, and architecture docs.
- The agent can explain the change in one short paragraph without hand-waving.

## Observability Rules

- Every new critical path must emit stable serial markers.
- Prefer deterministic markers over prose. Good: `BOOT012`, `DISK021`, `IRQ001`.
- Panics, faults, and exception paths must print enough context to localize failure quickly.
- If a failure cannot be localized from serial output in under a minute, the debug surface is insufficient.

## Test Design Rules

- Reuse the shared QEMU harness in `tests/qemu_harness.py`.
- Avoid each script inventing its own shell-ready heuristics.
- Prefer explicit acceptance markers over timing sleeps.
- Add at least one negative test for new disk, parsing, or exception behavior.
- If a test is flaky twice, stop feature work and fix the test contract first.

## Change Boundaries

Avoid combining these in one task unless the task is explicitly a refactor of contracts/tooling:

- Boot sector + paging
- Paging + scheduler
- FAT write path + ELF loader
- Disk layout + drive enumeration
- Keyboard IRQ path + shell parser

## Preferred Debug Order

1. `make verify-image`
2. `make phase0-check`
3. `make run-no-reboot`
4. `make run-trace`
5. `make run-gdb` and `make gdb-kernel`

## Human Review Role

The human should mostly validate:

- The task chosen was the right one.
- The acceptance contract was correct.
- The final behavior matches intent.
- The tradeoff is acceptable.

The human should not need to manually reconstruct the boot flow, disk layout, or test logic for normal changes.
