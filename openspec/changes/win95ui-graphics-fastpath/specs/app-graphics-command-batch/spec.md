## ADDED Requirements

### Requirement: Applications can submit bounded rectangle batches
The graphics ABI SHALL accept an ordered array of solid rectangles in one syscall when the count is between 1 and `APP_GFX_RECT_BATCH_MAX` inclusive.

#### Scenario: Valid batch renders in order
- **WHEN** a foreground application submits several valid overlapping rectangles
- **THEN** the driver renders them in array order under one atomic video operation
- **AND** later rectangles appear above earlier rectangles

#### Scenario: Full batch is validated before drawing
- **WHEN** any entry or user range in a rectangle batch is invalid
- **THEN** the syscall rejects the entire batch
- **AND** no rectangle from that batch changes the frame

#### Scenario: Batch count is bounded
- **WHEN** an application submits zero entries or more than `APP_GFX_RECT_BATCH_MAX` entries
- **THEN** the syscall rejects the request without reading beyond the descriptor

### Requirement: Existing rectangle calls remain compatible
The existing single-rectangle graphics syscall SHALL retain its current clipping, color, and return-value behavior.

#### Scenario: Legacy application draws one rectangle
- **WHEN** an application built against the previous runtime calls `app_gfx_rect`
- **THEN** the rectangle is rendered without requiring a batch descriptor
