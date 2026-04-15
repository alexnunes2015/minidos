## ADDED Requirements

### Requirement: Backbuffer allocation with 64-byte stride alignment
The graphics backbuffer SHALL be allocated with scanline stride rounded up to 64-byte (x86 cacheline) boundaries.

#### Scenario: Stride calculation for standard resolution
- **WHEN** allocating backbuffer for 640x480x32
- **THEN** stride is (640 * 4 + 63) / 64 * 64 = 2624 bytes per row
- **AND** each scanline starts at cacheline boundary

#### Scenario: Stride calculation for wide resolution
- **WHEN** allocating backbuffer for 1280x1024x32
- **THEN** stride is (1280 * 4 + 63) / 64 * 64 = 5120 bytes per row
- **AND** each row aligns independently

#### Scenario: Unmodified behavior for 16-bit
- **WHEN** backbuffer uses 16-bit color (RGB565)
- **THEN** stride is (width * 2 + 63) / 64 * 64
- **AND** alignment rules apply uniformly across all color depths

### Requirement: Pixel address calculation respects alignment
All pixel address calculations SHALL use the aligned stride, never the logical width.

#### Scenario: Pixel lookup uses aligned stride
- **WHEN** accessing pixel at (x, y) in backbuffer
- **THEN** address = base + y * aligned_pitch + x * bytes_per_pixel
- **AND** never uses logical_width * bytes_per_pixel

#### Scenario: Consecutive scanlines do not share cacheline
- **WHEN** two adjacent scanlines (y and y+1) are laid out in memory
- **THEN** last pixel of row y and first pixel of row y+1 never share same 64-byte cacheline
- **AND** false-sharing is eliminated

### Requirement: Padding bytes between scanlines have no side effects
Unused padding bytes at the end of each scanline SHALL be safe to read or write.

#### Scenario: Reading past logical width is safe
- **WHEN** code reads from padding region of a scanline
- **THEN** it accesses valid backbuffer memory (not unrelated kernel data)
- **AND** reads return garbage/uninitialized but cause no crash

#### Scenario: Writing padding does not corrupt adjacent scanlines
- **WHEN** fill or rendering operation overwrites padding bytes
- **THEN** next scanline remains unaffected
- **AND** cacheline alignment prevents unintended overwrite

### Requirement: Memory overhead is bounded
Backbuffer allocation overhead from stride alignment SHALL not exceed 5% of total size.

#### Scenario: Overhead for 640x480x32
- **WHEN** calculating backbuffer size with alignment
- **THEN** size is 480 rows * 2624 bytes/row = 1,259,520 bytes
- **AND** overhead vs unaligned (480 * 2560 = 1,228,800) is 2.5%

#### Scenario: Overhead for 1280x1024x32
- **WHEN** calculating backbuffer size
- **THEN** size is 1024 * 5120 = 5,242,880 bytes
- **AND** overhead vs unaligned (1024 * 5120 = 5,242,880) is 0% (already aligned)

### Requirement: Initialization does not change
Backbuffer allocation and initialization flow remains unchanged, stride is internal detail.

#### Scenario: Fixed allocation address unchanged
- **WHEN** backbuffer allocates at 0x00B00000
- **THEN** allocation address remains the same
- **AND** only stride calculation is updated

#### Scenario: Color initialization still works
- **WHEN** kernel initializes backbuffer with clear color
- **THEN** all pixels (including padding) are written to clear color
- **AND** visual result is unchanged

### Requirement: Performance benefit from cacheline alignment
Multi-threaded rendering operations SHALL see reduced cache contention.

#### Scenario: Parallel row fills do not share cachelines
- **WHEN** two threads fill separate rows concurrently
- **THEN** they operate on distinct cachelines
- **AND** no cache-line bouncing occurs

#### Scenario: Compiler optimizations leverage alignment
- **WHEN** compiler sees 64-byte aligned stride
- **THEN** it can apply SIMD/vectorization hints
- **AND** fill operations may auto-vectorize more effectively