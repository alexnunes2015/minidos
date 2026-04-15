## ADDED Requirements

### Requirement: Dirty rectangle accumulation during frame
The kernel video subsystem SHALL accumulate the bounding box of all modified regions during a frame rendering cycle, and only flush changed pixels to VESA LFB on present.

#### Scenario: Single draw operation marks region
- **WHEN** a single `fill_rect` or `draw_text` operation modifies pixels in region (100, 50, 200, 300)
- **THEN** the dirty rectangle is set to that region's bounds

#### Scenario: Multiple draws expand bounding box
- **WHEN** first operation modifies (100, 50, 200, 300) and second modifies (300, 100, 400, 200)
- **THEN** the dirty rectangle expands to encompass both: (100, 50, 400, 300)

#### Scenario: Dirty rect reset after present
- **WHEN** `video_present_pending()` is called with a valid dirty rect
- **THEN** the dirty rectangle is flushed to VESA LFB and reset for the next frame

#### Scenario: Clipping dirty rect to framebuffer bounds
- **WHEN** a draw operation attempts to modify region (-10, -10, 650, 490) on a 640x480 framebuffer
- **THEN** the dirty rectangle is clipped to (0, 0, 640, 480) before flushing

### Requirement: Fast-present using dirty rectangle bounds
The kernel SHALL flush only the dirty rectangle region to VESA LFB instead of the entire framebuffer.

#### Scenario: Partial region presented
- **WHEN** dirty rect is (100, 100, 200, 200) on 640x480 display
- **THEN** only that 100x100 region is copied from backbuffer to VESA LFB
- **AND** VESA bandwidth is reduced by ~96% compared to full-frame flush

#### Scenario: Empty dirty rect skips flush
- **WHEN** `video_present_pending()` is called with no modifications since last present
- **THEN** no VESA I/O occurs and no dirty rect is transmitted

#### Scenario: Dirty rect respects ASM fast-present path
- **WHEN** using `asm_fast_present_rect_rgb()` for standard formats (R8G8B8)
- **THEN** the ASM path receives clipped dirty rect bounds instead of full framebuffer

### Requirement: API for marking regions dirty
The kernel SHALL provide a public API to mark arbitrary rectangular regions as dirty.

#### Scenario: Explicit dirty mark from userland app
- **WHEN** userland app calls syscall to mark region (50, 50, 100, 100) dirty
- **THEN** that region is included in the frame's dirty rectangle on next present

#### Scenario: Internal kernel operations mark dirty automatically
- **WHEN** kernel builtin operations (e.g., boot splash, cursor draw) execute
- **THEN** those operations automatically mark affected regions dirty via internal helpers
- **AND** userland code does not need to be aware of dirty-rect mechanics

### Requirement: No data loss from dirty-rect approximation
The backbuffer SHALL always contain the complete, fully-rendered frame, even if dirty rect bounds are conservative (overshoot).

#### Scenario: Backbuffer is never truncated
- **WHEN** present flushes only dirty rect (100, 50, 200, 150)
- **THEN** the backbuffer retains full-frame data for the next render cycle
- **AND** reads outside dirty rect still access valid pixels

#### Scenario: Conservative dirty rect is acceptable
- **WHEN** two scattered small draws occur (e.g., corners of screen)
- **THEN** it is acceptable for dirty rect to encompass entire framebuffer if that simplifies implementation
- **AND** no visual artifact occurs
