## ADDED Requirements

### Requirement: Image build path selection is explicit and fail-fast
The disk image build flow MUST choose its environment path explicitly and MUST fail with a clear reason when the required toolchain is unavailable instead of silently falling back between incompatible modes.

#### Scenario: Missing image-build prerequisites produce a clear failure
- **WHEN** neither the supported `mtools` path nor the supported privileged formatting path is available
- **THEN** the build exits with an explicit error explaining which prerequisite is missing and does not produce a partially trusted image

### Requirement: Boot metadata generation is a defined build step
Boot metadata required by stage2, including kernel size or sector count handoff, MUST be generated through a defined build step with structured inputs instead of ad-hoc text scraping plus binary patching.

#### Scenario: Kernel metadata is derived without fragile post-build scraping
- **WHEN** the build prepares stage2 metadata for the kernel image
- **THEN** the metadata is generated from a defined build artifact or manifest rather than from `awk` parsing and manual binary patch logic

### Requirement: Reproducible build hygiene is documented and verified
The repository MUST document which generated artifacts are disposable, which directories are intentionally ignored, and which validation command proves that a clean workspace produces the same image status.

#### Scenario: Clean workspace preserves image verification outcome
- **WHEN** generated clutter such as `build/`, `.venv/`, or `tests/__pycache__/` is removed according to the documented hygiene policy
- **THEN** the required image validation command reports the same expected status on the rebuilt artifacts
