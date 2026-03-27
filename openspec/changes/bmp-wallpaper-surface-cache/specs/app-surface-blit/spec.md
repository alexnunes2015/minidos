## ADDED Requirements

### Requirement: Apps can blit decoded surfaces with one graphics operation
The graphics runtime SHALL allow an app to submit a decoded pixel surface and request that it be copied into the current frame through a single blit operation rather than one rectangle syscall per pixel.

#### Scenario: Full-surface blit succeeds
- **WHEN** an app submits a valid decoded surface plus destination coordinates and size
- **THEN** the graphics layer blits that surface into the frame with one logical graphics request

#### Scenario: Clipped blit restores only a dirty region
- **WHEN** an app submits a valid decoded surface plus a clip rect
- **THEN** only the clipped destination region is copied into the frame

### Requirement: Invalid blit descriptors fail safely
The graphics layer MUST reject invalid surface blit descriptors without corrupting the current frame or crashing the app runtime.

#### Scenario: Invalid buffer or dimensions are rejected
- **WHEN** an app submits a blit request with an invalid buffer pointer, unsupported format, or non-positive dimensions
- **THEN** the graphics layer rejects the blit and leaves the current frame unchanged
