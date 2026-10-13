## ADDED Requirements

### Requirement: Surface formats are explicit and backward compatible
The surface ABI SHALL retain legacy `APP_SURFACE_FORMAT_XRGB8888` byte-stream input and SHALL additionally accept little-endian packed `0x00RRGGBB` and `0xAARRGGBB` pixels.

#### Scenario: Legacy surface still renders
- **WHEN** an application submits format 0 pixels in `X,R,G,B` byte order
- **THEN** the resulting colors match the pre-change surface behavior

#### Scenario: Packed XRGB surface renders
- **WHEN** an application submits `APP_SURFACE_FORMAT_XRGB8888_LE`
- **THEN** each little-endian `0x00RRGGBB` pixel produces the corresponding RGB color

### Requirement: Opaque unscaled blits use a direct copy path
The video subsystem SHALL copy clipped rows directly into the 32-bit backbuffer when packed XRGB source and destination dimensions match.

#### Scenario: Clipped one-to-one copy
- **WHEN** a packed XRGB surface is blitted without scaling and with a valid clip rectangle
- **THEN** only intersecting source words are copied
- **AND** the generic per-pixel color conversion path is not required

### Requirement: ARGB surfaces use source-over transparency
The video subsystem SHALL blend `APP_SURFACE_FORMAT_ARGB8888_LE` pixels over the current draw target using their 8-bit alpha channel.

#### Scenario: Fully transparent pixel
- **WHEN** an ARGB source pixel has alpha 0
- **THEN** the destination pixel remains unchanged

#### Scenario: Fully opaque pixel
- **WHEN** an ARGB source pixel has alpha 255
- **THEN** the destination becomes the source RGB value

#### Scenario: Partial alpha pixel
- **WHEN** an ARGB source pixel has alpha between 1 and 254
- **THEN** each destination RGB component equals source-over blending rounded to the nearest representable value

### Requirement: Surface descriptors fail safely
The syscall boundary MUST reject unsupported formats, insufficient stride, overflowing byte spans, invalid user ranges, and unreasonable geometry before invoking the video renderer.

#### Scenario: Stride is shorter than a source row
- **WHEN** stride is less than width multiplied by four bytes
- **THEN** the request returns failure and leaves the current frame unchanged

#### Scenario: Source span arithmetic would overflow
- **WHEN** height, stride, or width would make the last accessible source byte wrap the address space
- **THEN** the request returns failure without dereferencing the buffer

#### Scenario: Unsupported format is submitted
- **WHEN** a descriptor contains a format not defined by the ABI
- **THEN** the request returns failure and does not mark damage

### Requirement: Runtime packed surfaces retain a rectangle-run fallback
The shared UI runtime SHALL attempt packed-XRGB surface transfer for decoded wallpaper and list-row buffers, and SHALL fall back to clipped horizontal solid-color runs when the surface syscall returns failure.

#### Scenario: Packed wallpaper format is rejected
- **WHEN** the wallpaper helper has a valid packed surface but its surface syscall returns failure
- **THEN** it samples the same scaled and clipped destination
- **AND** enqueues adjacent equal-color pixels as ordered rectangle runs

#### Scenario: Packed list row format is rejected
- **WHEN** a list-row surface blit returns failure
- **THEN** the visible clipped row is reproduced as ordered rectangle runs

### Requirement: Packed formats require runtime and kernel lockstep
Formats 1 and 2 SHALL be treated as a coordinated runtime/kernel ABI addition; only format 0 retains the earlier byte-stream contract. A runtime fallback SHALL be triggered by an explicit syscall failure, not by inferred format support.

#### Scenario: Surface syscall explicitly fails
- **WHEN** packed wallpaper, list-row, or ARGB icon submission returns failure
- **THEN** the corresponding helper may use its documented rectangle-run fallback

#### Scenario: Kernel accepts but ignores format
- **WHEN** an incompatible kernel reports success without honoring format 1 or 2
- **THEN** the runtime cannot detect the mismatch or invoke a fallback
