## ADDED Requirements

### Requirement: Status labels map to verifiable repository state
The project documentation MUST use status labels that map to verifiable repository state instead of narrative labels that rely on historical or subjective interpretation.

#### Scenario: Roadmap phase status distinguishes proof from intent
- **WHEN** a reader checks the roadmap entry for a subsystem phase
- **THEN** the entry distinguishes delivered work, validated behavior, known fragility, and uncovered areas using labels tied to current evidence

### Requirement: Operator-facing strings describe actual runtime behavior
Shell strings, embedded README text, and operator-facing build messages MUST describe the current runtime behavior that the repository actually supports.

#### Scenario: Runtime version string reflects current storage model
- **WHEN** an operator runs `ver` or reads the generated test image notes
- **THEN** the text describes the BIOS-backed boot floppy path and the FAT16/ATA runtime model without claiming a stronger or different storage contract

#### Scenario: Bundled documentation reflects current app runtime
- **WHEN** build scripts generate bundled README content for the test image
- **THEN** the text does not claim that ELF apps still run on the shell thread if the scheduler owns their lifecycle

### Requirement: Floppy and FAT12 policy is explicit
The repository MUST document whether FAT12/floppy support is a compatibility-only handshake or a first-class storage capability, and the stated policy MUST match the implemented ownership and test coverage.

#### Scenario: Compatibility-only floppy policy is documented consistently
- **WHEN** FAT12 remains limited to BIOS-backed boot compatibility
- **THEN** the docs explicitly describe that limited contract and do not imply a standalone FAT12/FDC stack

#### Scenario: First-class FAT12 support requires explicit ownership
- **WHEN** the project promotes FAT12 to a first-class capability
- **THEN** the docs and tests identify the responsible module and the dedicated validation coverage for that support
