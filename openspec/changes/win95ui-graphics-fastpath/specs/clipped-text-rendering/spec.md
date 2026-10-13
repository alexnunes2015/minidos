## ADDED Requirements

### Requirement: Applications can render clipped text in one request
The graphics ABI SHALL accept an 8x8 text request with an explicit destination, colors, and optional clip rectangle, and SHALL rasterize it while holding the video write lock once.

#### Scenario: Text is partially clipped
- **WHEN** a glyph intersects only part of the clip rectangle
- **THEN** pixels inside the intersection are rendered
- **AND** no pixels outside the clip rectangle change

#### Scenario: Multiple characters share one operation
- **WHEN** a string contains multiple visible characters
- **THEN** the driver renders the complete string without one syscall or lock acquisition per character or pixel

### Requirement: Text background can be transparent
The clipped text descriptor SHALL support a transparent-background flag that writes foreground glyph bits without overwriting zero glyph bits.

#### Scenario: Transparent label over wallpaper
- **WHEN** transparent text is drawn over existing non-solid pixels
- **THEN** foreground glyph pixels change to the requested color
- **AND** background pixels retain their previous values

### Requirement: Legacy opaque text remains compatible
The existing text syscall SHALL keep opaque foreground/background rendering and SHALL use the consolidated renderer internally.

#### Scenario: Existing application draws opaque text
- **WHEN** an application calls `app_gfx_text` without the new descriptor
- **THEN** the visual result remains an opaque 8x8-cell string with the supplied foreground and background colors
