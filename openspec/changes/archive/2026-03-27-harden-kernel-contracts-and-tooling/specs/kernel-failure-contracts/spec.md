## ADDED Requirements

### Requirement: Critical subsystems publish explicit failure contracts
The repository MUST define explicit invariants and failure responses for critical boot and runtime subsystems, including `kernel/core`, scheduler bootstrap, storage enumeration, and shell startup. Each contract MUST describe the expected response for invalid inputs, timeouts, missing hardware, and unrecoverable faults.

#### Scenario: Subsystem contract documents invalid input and fault handling
- **WHEN** a maintainer reviews the hardening contract for a critical subsystem
- **THEN** the contract states the subsystem invariants, the invalid-input policy, the timeout policy, and the hardware-fault response in one authoritative place

### Requirement: Critical failure paths emit deterministic serial markers
Every panic path, unrecoverable startup failure, and explicit degraded-mode transition in a critical subsystem MUST emit a stable serial marker before control is halted, returned to the shell, or downgraded into degraded mode.

#### Scenario: Panic path emits a stable marker
- **WHEN** a critical subsystem hits an unrecoverable fault during boot or runtime
- **THEN** the serial log contains a deterministic marker that identifies the subsystem and failure class before the system stops or recovers

#### Scenario: Degraded mode emits a stable marker
- **WHEN** a critical subsystem remains available only in a documented degraded mode
- **THEN** the serial log contains a deterministic degraded-mode marker that explains why normal behavior was not entered

### Requirement: Silent skips and synthetic critical resources are forbidden
The kernel MUST NOT silently skip critical-path work or fabricate critical runtime resources in order to appear functional. If a critical dependency is missing, the system MUST either fail explicitly or enter a documented degraded mode with an auditable marker.

#### Scenario: Missing startup script storage is handled explicitly
- **WHEN** `AUTOEXEC.AUT` cannot be processed because the required storage path is unavailable
- **THEN** the kernel emits an auditable error or degraded-mode marker instead of silently skipping the path

#### Scenario: Drive enumeration does not invent a boot volume
- **WHEN** storage enumeration cannot find a valid boot floppy volume or partitioned drive set
- **THEN** the kernel reports the absence explicitly and does not synthesize a plausible `A:` drive outside an isolated test mode
