## ADDED Requirements

### Requirement: Each critical subsystem has one authoritative implementation path
The repository MUST define one authoritative implementation path and one declared owner for each critical subsystem. Duplicate active implementations of the same subsystem MUST be removed or clearly demoted to inactive compatibility code.

#### Scenario: Keyboard authority is reduced to one path
- **WHEN** the keyboard subsystem ownership is audited
- **THEN** the repository identifies one authoritative keyboard implementation and documents the status of any legacy path as removed or inactive

### Requirement: Oversized critical modules have explicit decomposition targets
Critical files whose size or mixed responsibility impairs auditability MUST have explicit decomposition targets that split behavior by responsibility, API boundary, and owning subsystem.

#### Scenario: Large shell and storage modules receive split targets
- **WHEN** the hardening plan covers `shell_apps.c`, `shell_builtin.c`, `fat16_dir.c`, or `kernel.c`
- **THEN** the plan defines which responsibilities move out of each file and which module becomes the new owner of each responsibility

### Requirement: Shared low-level helpers are centralized
Reusable low-level helpers such as port I/O wrappers, physical-memory access helpers, or similar cross-cutting primitives MUST live in shared infrastructure rather than being reimplemented independently in multiple files.

#### Scenario: Shared hardware helper replaces local duplicates
- **WHEN** multiple kernel files require the same low-level helper behavior
- **THEN** they consume a shared helper interface instead of preserving duplicated local implementations
