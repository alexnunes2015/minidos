## ADDED Requirements

### Requirement: Word-aligned fill operations
Fill and clear operations in the backbuffer SHALL use word-aligned memory writes instead of per-pixel loops.

#### Scenario: 32-bit fill on aligned dimensions
- **WHEN** `fill_frontbuffer_rect_rgb_locked()` is called with 640x480 framebuffer
- **THEN** the fill operation uses 32-bit writes instead of individual pixel writes
- **AND** throughput improves by at least 8x

#### Scenario: Fallback for non-aligned fills
- **WHEN** a fill region cannot be word-aligned
- **THEN** the implementation falls back to per-pixel loop
- **AND** no data corruption occurs

#### Scenario: Clear backbuffer optimization
- **WHEN** `video_clear_color()` clears entire backbuffer
- **THEN** operation uses fastest available path
- **AND** operation completes in under 1ms for 640x480x32

### Requirement: Cacheline-friendly stride
The backbuffer stride SHALL be rounded up to 64-byte alignment.

#### Scenario: Stride calculated with cacheline alignment
- **WHEN** allocating backbuffer for 640x480x32
- **THEN** stride rounds from 2560 to 2624 bytes (next 64-byte boundary)
- **AND** padding at row end preserves cacheline isolation

#### Scenario: Pixel address calculation respects aligned stride
- **WHEN** calculating address of pixel (x, y)
- **THEN** address = backbuffer_base + y * aligned_pitch + x * bytes_per_pixel
- **AND** scanlines never share cacheline

#### Scenario: Overhead is minimal
- **WHEN** allocating 640x480x32 with padding
- **THEN** overhead vs unaligned is less than 4%

### Requirement: Zero-cost abstraction for fill API
The public fill API SHALL remain unchanged with optimizations transparent to callers.

#### Scenario: Existing fill calls maintain behavior
- **WHEN** code calls `video_fill_rect(100, 50, 200, 150, 0xFF0000)`
- **THEN** filled region is identical to before
- **AND** no API changes required

#### Scenario: Performance improvement is automatic
- **WHEN** fill executes with optimized stride
- **THEN** operation is faster without caller changes
- **AND** no conditional branching needed

### Requirement: Correctness in all color depths
Fill operations SHALL work correctly for 16-bit, 24-bit, and 32-bit modes.

#### Scenario: 32-bit fill
- **WHEN** framebuffer is XRGB8888
- **THEN** fill uses u32 writes with correct color packing

#### Scenario: 16-bit fill
- **WHEN** framebuffer is RGB565
- **THEN** color packs to 16-bit and uses u16 writes
- **AND** correct color appears

#### Scenario: 24-bit fill
- **WHEN** framebuffer is RGB888
- **THEN** fill uses per-pixel loop to avoid unaligned writes
- **AND** correct color appears