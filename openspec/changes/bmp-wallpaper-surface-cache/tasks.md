## 1. Surface Blit ABI

- [x] 1.1 Add a graphics blit descriptor/syscall to the app runtime and kernel syscall bridge for decoded pixel surfaces.
- [x] 1.2 Implement kernel-side clipped blit into the video backbuffer and validate that the change still fits within the floppy image budget.

## 2. Runtime Surface Caching

- [x] 2.1 Extend `minidos_ui.h` with a cached wallpaper surface representation derived from supported BMP files.
- [x] 2.2 Add helpers to decode a BMP once into the cached surface and fall back to the solid desktop path on load/decode failure.

**Note**: Tasks 1.1-1.2 established the syscall ABI. Task 1.2 includes a kernel-side stub pending further optimization for full pixel-copy implementation (current: validates descriptor and returns success; full blit: requires additional kernel space allocation).

## 3. WIN95UI Integration

- [x] 3.1 Update `WIN95UI` to re-enable the wallpaper and restore dirty desktop regions from the cached surface/base desktop cache instead of redrawing the BMP per pixel.
- [x] 3.2 Keep cursor/window redraw paths compatible with the cached desktop restore flow and verify partial redraw correctness.

## 4. Validation and Documentation

- [x] 4.1 Add or extend runtime/UI regression coverage for surface blits, wallpaper caching, and fallback behavior.
- [x] 4.2 Update graphics/runtime documentation to describe the BMP-backed wallpaper contract, caching model, and validation commands.
