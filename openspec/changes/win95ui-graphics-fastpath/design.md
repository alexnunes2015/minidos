## Context

The foreground graphics session already renders into a 32-bit software backbuffer and defers frontbuffer updates until `GFX_PRESENT`. The application ABI, however, exposes only one solid rectangle or one opaque text string per syscall, while the surface ABI uses a legacy byte-stream layout and always walks pixels through the generic conversion/scaling loop. `WIN95UI` consequently rebuilds clipped glyphs, alpha icons, bevels, and the software cursor from many tiny rectangle calls. Every call is separately copied from user memory, locked, marked dirty, and later recorded again for present.

This change is owned by the video subsystem. The shell dispatcher remains the user-pointer validation boundary; the UI runtime remains responsible for widget layout and z-order.

## Goals / Non-Goals

**Goals:**

- Bound syscall and lock overhead for rectangle-heavy UI drawing.
- Preserve the existing graphics ABI while adding explicit packed XRGB and ARGB formats.
- Make opaque, unscaled, clipped surface copies a row-copy operation.
- Provide one-lock clipped text rendering with optional transparent background.
- Keep kernel dirty tracking bounded while eliminating common duplicate and adjacent rectangles.
- Preserve draw ordering when the header-only runtime batches rectangles around text and surface operations.
- Reject malformed descriptors before any pixels change.

**Non-Goals:**

- A kernel window manager, per-process compositor, or multi-application desktop.
- Runtime VBE mode changes, refresh-rate negotiation, triple buffering, or GPU acceleration.
- Arbitrary fonts, Unicode shaping, antialiasing, or a general vector drawing API.
- Removing or changing the behavior of existing `GFX_RECT`, `GFX_TEXT`, or legacy surface calls.

## Decisions

### 1. Add bounded, all-or-nothing rectangle batches

The ABI gains a descriptor containing a user pointer to `app_gfx_rect_t` entries and a count capped at `APP_GFX_RECT_BATCH_MAX` (64). The syscall copies and validates every entry into a kernel-local `video_rect_t` array before acquiring the video lock. The video layer clips and renders the entries in array order under one lock, records damage, releases the lock, and performs the usual deferred-present check once.

This keeps the kernel stack cost near 1.25 KiB, provides deterministic validation, and preserves painter's-order semantics. A general variable-length command stream was considered, but rejected for this phase because mixed command parsing would create a much larger validation surface.

### 2. Keep format 0 and add explicit little-endian packed formats

`APP_SURFACE_FORMAT_XRGB8888` remains the legacy four-byte sequence `X,R,G,B`. Two new formats are added:

- `APP_SURFACE_FORMAT_XRGB8888_LE`: each pixel is a little-endian `u32` with value `0x00RRGGBB`.
- `APP_SURFACE_FORMAT_ARGB8888_LE`: each pixel is a little-endian `u32` with value `0xAARRGGBB` and uses source-over blending.

The explicit names avoid silently changing existing callers. The runtime migrates its decoded wallpaper and list-row buffers to the packed layout, while generated Win95 icon arrays can be submitted directly as ARGB.

Formats 1 and 2 require the runtime and kernel to be deployed in lockstep. The runtime has no capability negotiation and can only use a fallback when the syscall explicitly returns failure; it cannot detect an older kernel that accepts a descriptor but ignores its format field. Format 0 is the only preserved cross-version byte-stream contract.

### 3. Select the blit path before locking

The shell validates format, positive dimensions, minimum stride, bounded destination geometry, and the exact last accessible source byte with checked unsigned arithmetic. The video layer then selects:

1. packed XRGB, 1:1 scale, backbuffer available: copy clipped rows as 32-bit words;
2. packed ARGB: nearest-neighbor sample and source-over blend, with alpha 0 skipped and alpha 255 stored directly;
3. scaled or legacy input: generic nearest-neighbor conversion using quotient/remainder stepping so the inner pixel loop performs no division.

The driver still marks only the clipped destination. Direct access to the validated user buffer is retained from the existing ABI; callers must not mutate a submitted surface until the syscall returns.

### 4. Add a clipped text descriptor and consolidate locking

The new text descriptor carries position, string pointer, colors, clip rectangle, and `APP_GFX_TEXT_TRANSPARENT_BG`. The shell copies the descriptor and a bounded NUL-terminated string. The renderer holds `video_lock()` once, rasterizes glyph pixels through `draw_pixel_locked()`, records the actual clipped text bounds, and runs one present check. The existing `video_draw_text_at()` delegates to the same implementation with a framebuffer-sized opaque clip.

This was chosen over a glyph-cache surface API because the current fixed 8x8 font is small; eliminating per-pixel locking and userland clipping captures the immediate benefit without adding font resource lifetime rules.

### 5. Batch transparently inside the UI runtime

The header-only runtime owns a fixed 64-entry rectangle queue. `ui_fill_rect()` enqueues, automatically flushing when full. Operations whose ordering is externally visible—clear, text, bitmap/surface blit, and present—flush the queue first. This preserves existing call sites while turning bevels, borders, cursor runs, and other consecutive fills into bounded batch syscalls.

`ui_draw_text_clipped()` and the generic `ui_draw_text_transparent_clipped()` first use the new clipped-text syscall instead of one text syscall per character and retain their existing userland fallbacks if it is rejected. The `WIN95UI` compatibility wrapper deliberately uses `ui_draw_text_transparent_runs_clipped()` directly: it rasterizes `ui_font_8x8` into clipped horizontal foreground runs, which keeps labels pixel-identical while still feeding the rectangle batch. Icon drawing submits the generated ARGB bitmap through a clipped surface blit and retains its clipped run fallback.

Packed wallpaper and list-row helpers likewise try the packed-XRGB surface call first. If the syscall explicitly returns failure, they sample the same clipped destination and enqueue adjacent equal-color pixels as one-pixel-high rectangle runs. The ARGB icon helper also falls back only after an explicit failure. None of these paths detects a kernel that accepts but misinterprets packed format 1 or 2.

### 6. Coalesce only predictable damage shapes

When recording damage, the driver first removes contained duplicates, then merges rectangles that share a full horizontal or vertical span and overlap or touch. General L-shaped overlaps are not collapsed because their bounding box can copy substantially more pixels. Capacity overflow retains the existing conservative union fallback.

This favors the shapes emitted by text rows, bevel edges, batch fills, and surface strips without introducing a tile grid or allocation.

### 7. Test contracts, not implementation labels

Host runtime tests assert batching boundaries, operation ordering, packed surface fields, generic transparent clipped text, and rectangle/run fallbacks after rejected batch, text, or surface calls. Kernel/QEMU tests assert malformed descriptors are rejected, graphics stress completes, timing fields are present, and `WIN95UI` remains interactive with its existing `ui_font_8x8` glyph shape. The graphics performance test records ticks and operation counts; it no longer treats a boot marker alone as a performance measurement.

## Risks / Trade-offs

- **[Risk] Runtime batching changes when pixels reach the kernel.** → Flush before every non-rectangle graphics operation and at present; preserve array order within a batch.
- **[Risk] ARGB icons look different from the previous alpha threshold.** → Use mathematically defined source-over blending and add alpha 0/128/255 regression cases.
- **[Risk] Descriptor arithmetic can wrap.** → Validate minimum stride and exact byte span with division-based overflow checks before touching the frame.
- **[Risk] A 64-entry kernel-local batch increases syscall stack use.** → Keep the cap fixed and below 1.5 KiB; reject zero or oversized counts.
- **[Risk] Damage merging can trade precision for fewer copies.** → Merge only containment and same-span adjacency/overlap; retain the existing union fallback on overflow.
- **[Risk] Direct user-surface reads can race another app thread.** → Preserve current synchronous ownership semantics and document that the buffer is immutable for the duration of the call.
- **[Risk] The header-only rectangle queue is process-global.** → Keep Win95Ui rendering single-threaded and require other multithreaded apps to serialize runtime graphics-helper calls; queue synchronization is deferred to a future runtime change.

## Migration Plan

1. Add ABI constants/descriptors and kernel validation/rendering while retaining old calls.
2. Add focused tests for new calls and validation failures.
3. Switch runtime-owned surfaces and rectangle helpers to the new formats and batch path.
4. Switch only the existing transparent-text and icon helper bodies in `WIN95UI`, keeping text on pixel-identical batched `ui_font_8x8` runs and retaining the icon run fallback.
5. Run the focused graphics/UI suites, then complete Tier 3 validation with `make ci` and compare the QEMU graphics timing markers.

Rollback consists of routing runtime helpers back to the old individual syscalls; the additive ABI can remain without changing old applications.

## Open Questions

None for this phase. A driver-owned cursor surface and per-window compositor remain candidates for separate changes after measured results from these fast paths.
