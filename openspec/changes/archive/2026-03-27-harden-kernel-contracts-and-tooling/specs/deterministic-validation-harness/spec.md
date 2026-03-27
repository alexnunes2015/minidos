## ADDED Requirements

### Requirement: Serial markers are versioned and shared across docs, kernel, and tests
The project MUST maintain a versioned catalog of deterministic serial markers that is consumed by the kernel, operator docs, and the QEMU test harness as a shared contract.

#### Scenario: Test harness uses documented marker IDs
- **WHEN** a test waits for boot, shell readiness, or a fault outcome
- **THEN** it synchronizes on a marker ID that is defined in the shared marker catalog and documented for operators

### Requirement: Harness synchronization is marker-driven
Automated QEMU tests MUST use observable state transitions and deterministic markers for readiness and failure detection instead of arbitrary sleep delays or output timing guesses whenever a marker exists for that state.

#### Scenario: Shell readiness does not rely on time-based delay
- **WHEN** a serial-driven test needs to send commands to the shell
- **THEN** it waits for the documented shell-ready marker or equivalent observable state rather than sleeping for a fixed amount of time

### Requirement: Negative hardening coverage exists for critical runtime failures
The automated test suite MUST include negative coverage for scheduler isolation, syscall misuse, and storage failure handling so that critical failure behavior is proven instead of inferred from self-test markers alone.

#### Scenario: User fault containment is asserted by tests
- **WHEN** a user-mode app triggers an invalid syscall pointer or a controlled page fault
- **THEN** the test suite verifies that the offending process is contained and that the expected failure marker is emitted

#### Scenario: Storage failure policy is asserted by tests
- **WHEN** storage enumeration or startup script processing enters an error or degraded path
- **THEN** the test suite verifies the documented marker and rejects any silent skip or synthetic critical resource creation
