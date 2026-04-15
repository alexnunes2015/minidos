## ADDED Requirements

### Requirement: Reader-writer lock for framebuffer access
The video subsystem SHALL implement a reader-writer lock allowing concurrent reads without blocking other readers, while ensuring exclusive access for writes.

#### Scenario: Multiple concurrent readers
- **WHEN** multiple threads call `video_read_lock()` without any writer
- **THEN** all readers acquire the lock immediately
- **AND** they proceed concurrently without blocking each other

#### Scenario: Writer blocks readers
- **WHEN** a writer holds `video_write_lock()` and new readers call `video_read_lock()`
- **THEN** readers block until writer releases `video_write_unlock()`
- **AND** no reader proceeds while writer is active

#### Scenario: Reader blocks writer
- **WHEN** readers are active and a writer calls `video_write_lock()`
- **THEN** writer waits until all readers call `video_read_unlock()`
- **AND** no new readers are accepted after writer starts waiting

#### Scenario: Write-lock acquired as mutex
- **WHEN** calling `video_write_lock()` and no readers present
- **THEN** writer acquires exclusive access immediately
- **AND** behaves equivalently to previous mutex lock

### Requirement: Backward compatibility with existing lock API
Existing code using `video_lock()` and `video_unlock()` SHALL continue working.

#### Scenario: Old API wraps write-lock
- **WHEN** kernel code calls `video_lock()`
- **THEN** it internally calls `video_write_lock()`
- **AND** all existing code gets exclusive access as before

#### Scenario: Old API release wraps write-unlock
- **WHEN** kernel code calls `video_unlock()`
- **THEN** it internally calls `video_write_unlock()`
- **AND** lock is properly released

### Requirement: Non-blocking spin-based synchronization
The reader-writer lock SHALL use atomic compare-swap (CAS) without syscalls or sleepable primitives.

#### Scenario: CAS-based acquire loop
- **WHEN** thread attempts to acquire lock
- **THEN** it spins using `__sync_bool_compare_and_swap()` until successful
- **AND** no OS synchronization primitives are called

#### Scenario: Memory barrier for coherence
- **WHEN** lock state changes
- **THEN** `__sync_synchronize()` ensures all CPUs see the change
- **AND** no race conditions occur between readers/writers

### Requirement: Read-side operations do not modify framebuffer
Code using `video_read_lock()` SHALL only query or read framebuffer state, never write pixels.

#### Scenario: Read-lock used for color query
- **WHEN** application calls `video_read_lock()` to query current framebuffer color
- **THEN** it can safely read pixel data without blocking other readers
- **AND** write operations are still exclusive

#### Scenario: Read-lock used for compositing
- **WHEN** UI compositor needs to read existing framebuffer to blend layers
- **THEN** multiple render threads can read concurrently
- **AND** performance improves vs exclusive lock

### Requirement: Write-side operations have exclusive access
Code using `video_write_lock()` SHALL have sole access to framebuffer.

#### Scenario: Write-lock for pixel modification
- **WHEN** fill or blit operation acquires `video_write_lock()`
- **THEN** no other thread reads or writes framebuffer simultaneously
- **AND** data consistency is guaranteed

#### Scenario: Present operation has exclusive access
- **WHEN** `video_present_pending()` runs with write-lock
- **THEN** no concurrent reads or writes affect the frame being presented
- **AND** no partial-frame artifacts occur