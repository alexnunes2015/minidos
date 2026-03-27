## ADDED Requirements

### Requirement: Wallpaper BMPs are decoded once and reused
The UI runtime SHALL accept user-provided BMP wallpapers in the existing supported formats, decode them once into a cached surface, and reuse that decoded surface for subsequent redraws.

#### Scenario: Full desktop render reuses cached wallpaper
- **WHEN** a GUI app renders a desktop with a valid wallpaper BMP that has already been loaded
- **THEN** the runtime reuses the cached decoded surface instead of reparsing the BMP file for that frame

#### Scenario: Partial redraw restores from cached wallpaper
- **WHEN** a dirty region of the desktop must be restored after cursor or window motion
- **THEN** the runtime restores that region from the cached wallpaper or composed desktop surface instead of redrawing the BMP pixel-by-pixel

### Requirement: Wallpaper failures preserve a usable desktop
If a wallpaper BMP cannot be loaded or decoded, the UI runtime MUST fall back to a valid solid desktop render without preventing the app from starting.

#### Scenario: Invalid wallpaper falls back cleanly
- **WHEN** the configured wallpaper BMP is missing, too large, or malformed
- **THEN** the app starts with the solid desktop background and continues to render normally

### Requirement: WIN95UI shows wallpaper without regressing interaction
The reference `WIN95UI` app MUST be able to keep its background visible while preserving responsive interaction for cursor motion and partial redraw paths.

#### Scenario: WIN95UI keeps background enabled
- **WHEN** `WIN95UI` runs with a valid wallpaper BMP
- **THEN** the wallpaper remains visible during normal operation and the app uses the cached-surface path for redraws
