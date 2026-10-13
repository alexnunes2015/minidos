# MiniDOS Graphics API

This document defines the first reusable graphics/UI layer for MiniDOS applications.
It is intentionally small and deterministic so the project can grow toward a Windows 95/98-style GUI without pulling in browser-like complexity.

## Scope

The kernel already exposes low-level graphics syscalls for applications through `external_apps/runtime/minidos_app.h`:

- `app_gfx_clear`
- `app_gfx_rect`
- `app_gfx_rect_batch` — submit 1 through 64 ordered solid rectangles atomically
- `app_gfx_text`
- `app_gfx_text_clipped` — opaque or transparent 8x8 text inside an explicit clip rectangle
- `app_gfx_size`
- `app_gfx_present`
- `app_gfx_surface_blit` — blit a legacy XRGB, packed XRGB, or straight-alpha ARGB surface in one operation
- `app_mouse_state`
- `app_wait_event`
- `app_wait_event_timeout`
- `app_get_time`

The new layer lives in `external_apps/runtime/minidos_ui.h` and builds classic desktop widgets on top of those primitives.

Current goals:

- stable pixel-space drawing API for apps
- classic Win95-like theme defaults
- reusable window, panel, button, label, text-box, checkbox, radio, dropdown, menu, and scrollbar helpers
- retained-mode window manager for app-level UI composition
- explicit parent/child controls (label, button, text input)
- mouse snapshots and waitable input events for GUI apps
- RTC time reads for UI elements such as taskbar clocks
- predictable dirty-rect invalidation for software-cursor apps
- bounded rectangle batching with painter-order preservation and a legacy-call fallback
- clipped opaque or transparent text without userland per-character clipping
- explicit frame presentation for GUI apps
- zero dynamic allocation
- no dependency on libc

Non-goals for this phase:

- CSS parsing
- automatic layout/reflow
- overlapping window invalidation

## Files

- `external_apps/runtime/minidos_app.h`: low-level syscall ABI, including rectangle-batch, clipped-text, and surface descriptors
- `external_apps/runtime/minidos_ui.h`: header-only UI toolkit; includes `ui_wallpaper_surface_t` and BMP decode/blit helpers
- `external_apps/runtime/minidos_cursor_bitmap.h`: generated cursor bitmap header used by the UI runtime
- `external_apps/apps/win95_demo/win95_demo.c`: reference app that exercises the toolkit; uses wallpaper surface caching
- `assets/cursor/`: cursor bitmap source + conversion kit

## Main Types

### `ui_rect_t`

Basic rectangle in pixels:

```c
typedef struct {
    int x;
    int y;
    int w;
    int h;
} ui_rect_t;
```

### `ui_theme_t`

Color palette for the classic desktop look:

- desktop background and accent
- face/light/shadow colors for beveled widgets
- active/inactive title bar colors
- field and selection colors
- optional `desktop_bg_bitmap` path that `ui_draw_desktop` will attempt to paint (24/32-bit BMP, ≤ `UI_BITMAP_MAX_FILE_SIZE`)

Use `ui_theme_classic()` as the default bootstrap theme.

### `ui_window_t`

Describes a top-level classic window:

- bounds
- title
- active/inactive state
- optional close button
- optional minimize/maximize buttons
- minimized/maximized state and restore bounds for resizable windows
- resizable windows can be resized from their borders and corners

### `ui_button_t`

Describes a push button:

- bounds
- label
- pressed state
- focus state
- enabled state

### `ui_window_manager_t`

Header-only retained-mode manager for app-side GUI state:

- fixed-capacity windows (`UI_WM_MAX_WINDOWS`)
- fixed-capacity controls (`UI_WM_MAX_CONTROLS`)
- no dynamic allocation
- tracks active window and focused control

Children are represented by `ui_control_t` and support:

- `UI_CONTROL_LABEL`
- `UI_CONTROL_BUTTON`
- `UI_CONTROL_TEXTINPUT`
- `UI_CONTROL_LISTVIEW`
- `UI_CONTROL_CHECKBOX`
- `UI_CONTROL_RADIO`
- `UI_CONTROL_DROPDOWN`
- `UI_CONTROL_MENU`
- `UI_CONTROL_SCROLLBAR`

## Primitive Helpers

`minidos_ui.h` adds these core helpers:

- `ui_rgb(r, g, b)`
- `ui_screen_size(api, &w, &h)`
- `ui_clear(api, color)`
- `ui_present(api)`
- `ui_fill_rect(api, rect, color)`
- `ui_frame_rect(api, rect, color)`
- `ui_bevel_rect(api, rect, top_left, bottom_right)`
- `ui_draw_text(api, x, y, text, fg, bg)`
- `ui_draw_text_clipped(api, x, y, text, fg, bg, clip)`
- `ui_draw_text_transparent_clipped(api, x, y, text, fg, clip)`
- `ui_draw_text_transparent_runs_clipped(api, x, y, text, fg, clip)`
- `ui_rect_make(x, y, w, h)`
- `ui_rect_inset(rect, amount)`
- `ui_rect_is_empty(rect)`
- `ui_rect_contains(&rect, x, y)`
- `ui_rect_contains_rect(&outer, &inner)`
- `ui_rects_intersect(a, b)`
- `ui_rect_intersect(a, b)`
- `ui_rect_union(a, b)`
- `ui_rect_subtract(rect, cutout, out_rects, max_rects)`
- `ui_dirty_list_init(&dirty_list)`
- `ui_dirty_list_add(&dirty_list, rect)`
- `ui_dirty_list_add_clipped(&dirty_list, rect, clip)`
- `ui_mouse_left_down(...)`
- `ui_mouse_left_pressed(...)`
- `ui_mouse_left_released(...)`
- `ui_draw_bitmap(api, path, x, y, w, h)`

These are enough to implement custom controls without touching kernel code.

`ui_fill_rect()` queues consecutive fills in a fixed 64-entry runtime batch. The runtime flushes that queue before clear, text, bitmap/surface operations, presentation, or a change of application API pointer, and when the queue is full. If syscall 35 is unavailable or rejects the batch, the flush retries the saved rectangles through the existing single-rectangle syscall in the same order.

For cursor-driven apps, prefer keeping a short list of dirty rects, repainting the cursor last, and calling `ui_present(api)` once at the end of the frame.

## Widget Helpers

The initial reusable widget layer includes:

- `ui_draw_desktop`
- `ui_draw_panel`
- `ui_draw_window`
- `ui_window_title_bar_rect`
- `ui_window_close_button_rect`
- `ui_window_minimize_button_rect`
- `ui_window_maximize_button_rect`
- `ui_window_client_rect`
- `ui_draw_button`
- `ui_button_contains`
- `ui_window_hit_title`
- `ui_window_hit_close`
- `ui_window_hit_minimize`
- `ui_window_hit_maximize`
- `ui_draw_label`
- `ui_draw_label_centered`
- `ui_draw_text_box`
- `ui_draw_checkbox`
- `ui_draw_radio_button`
- `ui_draw_dropdown`
- `ui_draw_menu_widget`
- `ui_draw_scrollbar`
- `ui_draw_cursor`
- `ui_draw_bitmap`

`ui_draw_bitmap` loads an uncompressed BMP (`BI_RGB`) that is <= `UI_BITMAP_MAX_FILE_SIZE` (256 KiB by default) and paints it at `(x, y)`, scaling to the provided `w x h` rectangle when both dimensions are positive (pass `w` or `h` ≤ 0 to keep the source size). Only 24-bit and 32-bit bitmaps are supported; magenta (`#FF00FF`) pixels in 24-bit images or pixels whose alpha byte is zero are treated as transparent so earlier drawings remain visible.

The `STARTUI` demo keeps `desktop_bg_bitmap` disabled by default so the optional wallpaper asset does not change its normal appearance or redraw cost. When enabled, the helper first uses a packed surface blit and can fall back to batched horizontal color runs if that syscall is rejected.

`ui_draw_cursor` now renders from a generated bitmap header and emits horizontal runs instead of 1x1 rects wherever possible. The default asset pipeline is:

1. place `cursor.png` in `assets/cursor/` for alpha transparency, or `cursor.bmp` as fallback
2. run `make` or `./external_apps/add_app.sh ...`
3. let `assets/cursor/convert_cursor.sh` regenerate `external_apps/runtime/minidos_cursor_bitmap.h`

The converter maps the source image to three cursor states:

- transparent
- outline
- fill

Window-manager API additions:

- `ui_wm_init`
- `ui_wm_create_window`
- `ui_wm_create_window_ex`
- `ui_wm_bring_to_front`
- `ui_wm_add_label`
- `ui_wm_add_button`
- `ui_wm_add_textinput`
- `ui_wm_add_checkbox`
- `ui_wm_add_radio`
- `ui_wm_add_dropdown`
- `ui_wm_add_menu`
- `ui_wm_add_scrollbar`
- `ui_wm_set_focus_control`
- `ui_wm_hit_test_control`
- `ui_wm_dispatch_mouse`
- `ui_wm_dispatch_key`
- `ui_wm_minimize_window`
- `ui_wm_restore_window`
- `ui_wm_maximize_window`
- `ui_wm_toggle_maximize_window`
- `ui_wm_close_window`
- `ui_wm_draw`

These helpers intentionally assume the current MiniDOS text cell size of `8x8` pixels for text positioning and centering.

## Example

Minimal usage pattern:

```c
#include "minidos_ui.h"

int app_main(const minidos_app_api_t* api) {
    ui_theme_t theme = ui_theme_classic();
    ui_window_t window;
    int sw = 640;
    int sh = 480;

    ui_screen_size(api, &sw, &sh);
    ui_draw_desktop(api, &theme, sw, sh, "MiniDOS UI");

    window.bounds = ui_rect_make(120, 80, 320, 180);
    window.title = "Settings";
    window.active = 1;
    window.has_close_button = 1;
    ui_draw_window(api, &theme, &window);
    ui_present(api);

    return 0;
}
```

For a fuller example, use `external_apps/apps/win95_demo/win95_demo.c`.

## Build / Validation

Build and install the demo app into `minidos.img`:

```bash
make
```

Then boot MiniDOS and run:

```text
cd user
cd adm
cd aios
startui
```

Expected result:

- a teal desktop with taskbar
- a "Componentes UI" test window opened at startup
- mouse/keyboard-testable checkbox, radio, combo/dropdown, menu, and scrollbar widgets
- desktop icons and a Start menu
- Explorer windows with taskbar buttons, including minimized windows
- a software cursor driven by the PS/2 mouse
- title-bar minimize/maximize/restore/close for resizable windows
- border/corner resizing for Explorer windows; until resize cursors exist, the demo shows a small resize marker and corner grip
- static windows can be created with only a close button
- title-bar window dragging
- keyboard-driven focus cycling with `TAB`
- activation with `SPACE` or `ENTER`
- exit with `Q` or `ESC`

Automated validation:

```bash
make test-mouse
```

The `startui` demo also uses `app_wait_event_timeout(api, ..., 1000)` plus `app_get_time(api, ...)` to refresh the taskbar clock once per second without blocking on user input forever. The runtime now anchors resource lookup to `A:\AIOS`, so companion icons/BMPs/sounds can live beside `STARTUI.ELF` in that folder.

## Graphics Fast-Path ABI

The additions are backward compatible. Existing syscall numbers and the legacy format-0 surface interpretation do not change.

| Syscall | Wrapper | Contract |
|---|---|---|
| 33 | `app_gfx_surface_blit` | Validated legacy/packed surface transfer, optional nearest-neighbor scaling and clipping |
| 35 | `app_gfx_rect_batch` | 1..64 ordered solid rectangles, validated all-or-nothing |
| 36 | `app_gfx_text_clipped` | One bounded 8x8 text request with clipping and optional transparent background |

Background tasks cannot mutate the foreground graphics session through these calls.

### Rectangle batch syscall (`app_gfx_rect_batch`)

`APP_GFX_RECT_BATCH_MAX` is fixed at 64. The syscall copies the batch descriptor and every rectangle into kernel memory, validates the complete request, and only then enters the video renderer. If the count is zero or greater than 64, the rectangle array is outside the current app's user range, or any entry has invalid/overflowing geometry, the call fails and no rectangle from the batch is drawn or marked dirty.

Valid rectangles are drawn in array order under one video lock and one deferred-present decision, so later entries retain painter-order precedence over earlier entries. The existing `app_gfx_rect()` call remains available and keeps its previous behavior.

The header-only runtime queue (`g_ui_gfx_rect_batch`) is process-global and assumes a single UI/render thread, as used by Win95Ui. An app that draws from multiple threads must serialize calls to the runtime graphics helpers; the queue itself is not thread-safe.

```c
app_gfx_rect_t rects[2];
app_gfx_rect_batch_t batch;

rects[0] = (app_gfx_rect_t){ 10, 10, 80, 20, 0x00C0C0C0 };
rects[1] = (app_gfx_rect_t){ 12, 12, 76, 16, 0x00000080 };
batch.rects = rects;
batch.count = 2;

if (app_gfx_rect_batch(api, &batch) <= 0) {
    /* A compatibility caller may retry each entry with app_gfx_rect(). */
}
```

### Clipped text syscall (`app_gfx_text_clipped`)

Syscall 36 accepts `app_gfx_text_clipped_t`. The kernel copies a NUL-terminated string through its bounded 256-byte user-text buffer, validates the position, clip geometry, and flags, then rasterizes the request while holding the video lock once. A clip is active only when both `clip_w` and `clip_h` are positive; otherwise the framebuffer bounds are used.

Set `APP_GFX_TEXT_TRANSPARENT_BG` to write only foreground glyph bits. With `flags == 0`, both foreground and background glyph pixels are written, matching `app_gfx_text()`. Unknown flag bits, an unterminated/out-of-range string, or invalid geometry reject the request before drawing.

```c
app_gfx_text_clipped_t label;

label.x = 24;
label.y = 40;
label.text = "My Computer";
label.fg = 0x00FFFFFF;
label.bg = 0;
label.clip_x = 20;
label.clip_y = 36;
label.clip_w = 96;
label.clip_h = 16;
label.flags = APP_GFX_TEXT_TRANSPARENT_BG;
app_gfx_text_clipped(api, &label);
```

The runtime helpers flush pending rectangles before issuing the text request, preserving the original draw order. `ui_draw_text_clipped()` uses opaque mode; the generic `ui_draw_text_transparent_clipped()` supplies the transparent-background flag and falls back to `ui_draw_text_transparent_runs_clipped()` if syscall 36 is rejected.

The Win95Ui wrapper intentionally calls the runs helper directly instead of syscall 36. It rasterizes the runtime's existing `ui_font_8x8` and enqueues each clipped horizontal foreground run through `ui_fill_rect()`, preserving the demo's pixel-exact glyph shapes while still benefiting from rectangle batching.

## BMP Wallpaper and Surface Blit

### Surface blit syscall (`app_gfx_surface_blit`)

Apps can transfer a pre-decoded pixel surface into the current frame with a single syscall instead of one `app_gfx_rect` per pixel. Surface format IDs have intentionally distinct layouts:

| Constant | ID | Pixel contract | Bytes in increasing memory addresses |
|---|---:|---|---|
| `APP_SURFACE_FORMAT_XRGB8888` | 0 | Legacy byte stream; not a native packed word | `[X,R,G,B]` |
| `APP_SURFACE_FORMAT_XRGB8888_LE` | 1 | Little-endian `u32` with numeric value `0x00RRGGBB` | `[BB,GG,RR,00]` |
| `APP_SURFACE_FORMAT_ARGB8888_LE` | 2 | Straight-alpha little-endian `u32` with numeric value `0xAARRGGBB` | `[BB,GG,RR,AA]` |

Format 0 is preserved exactly for binaries built against the earlier ABI. Do not relabel an existing `[X,R,G,B]` byte buffer as format 1. Runtime-owned native `u32` surfaces use format 1, while the generated Win95 icon arrays use format 2 directly.

Formats 1 and 2 require the runtime and kernel to be updated together. There is no surface capability negotiation: a helper can fall back only when `app_gfx_surface_blit()` explicitly returns failure. It cannot detect an older kernel that reports success while ignoring `format`, so format 0 remains the only safe earlier-ABI byte stream.

Example using packed XRGB words:

```c
app_gfx_surface_blit_t blit;
blit.buffer = (const unsigned char*)pixels; /* little-endian 0x00RRGGBB words */
blit.width  = surface_w;
blit.height = surface_h;
blit.stride = surface_w * 4;
blit.format = APP_SURFACE_FORMAT_XRGB8888_LE;
blit.dest_x = 0;
blit.dest_y = 0;
blit.clip_x = -1;            /* -1 = no clipping */
blit.clip_y = 0;
blit.clip_w = 0;
blit.clip_h = 0;
blit.dest_w = 640;           /* <= 0 keeps source size */
blit.dest_h = 480;
app_gfx_surface_blit(api, &blit);
```

The syscall validates the whole descriptor before entering the renderer:

- `buffer` must be non-NULL and its exact accessible span must lie inside the calling app's user range;
- source width/height must be positive and bounded, and `stride` must be at least `width * 4`;
- `(height - 1) * stride + width * 4` is checked for unsigned overflow before the user range is read;
- only formats 0, 1, and 2 are accepted;
- effective destination dimensions (source dimensions when `dest_w`/`dest_h` are non-positive), destination coordinates, and an enabled clip rectangle must be bounded and must not overflow signed endpoint arithmetic.

Any validation failure returns failure without changing pixels or recording damage. The submitted buffer has synchronous ownership: the application must leave it unchanged until `app_gfx_surface_blit()` returns. Runtime helpers may respond to an explicit failure by reproducing their already-validated content through rectangle primitives; this is a return-value fallback, not automatic ABI-version detection.

When packed format 1 is unscaled and the 32-bit software backbuffer is active, the renderer copies only the clipped source rows as 32-bit words. Other valid cases use nearest-neighbor sampling; quotient/remainder stepping avoids a division in the inner pixel loop. This lets apps reuse a smaller decoded wallpaper cache without falling back to per-pixel syscalls.

Format 2 uses straight (not premultiplied) alpha and source-over composition. Alpha 0 preserves the destination, alpha 255 stores the source RGB directly, and partial alpha applies this independently to each RGB component:

```text
out = (source * alpha + destination * (255 - alpha) + 127) / 255
```

The `+127` term gives nearest-integer rounding. The framebuffer remains RGB; source alpha is used for composition rather than stored as a destination channel.

### Damage coalescing

Every primitive still contributes only its framebuffer-clipped destination to pending damage. Before adding a dirty rectangle, the driver discards it if an existing entry contains it, replaces contained entries, and merges rectangles that overlap or touch while sharing the same complete `y`/`h` span or `x`/`w` span. Incompatible shapes stay separate. If the fixed 512-entry list is exhausted, the existing conservative bounding-union fallback remains in force, so presentation never omits a modified pixel.

### BMP wallpaper caching (`ui_wallpaper_surface_t`)

`minidos_ui.h` provides a runtime-side wallpaper cache that decodes a BMP file once and reuses the result across redraws:

```c
/* On first call: reads file, validates BMP header, decodes to XRGB8888 */
if (ui_wallpaper_surface_load(api, "WALLPAPR.BMP")) {
    state->wm.theme.desktop_bg_bitmap = "WALLPAPR.BMP";
}

/* For dirty-rect restore (e.g. after cursor or window motion): */
if (ui_wallpaper_surface_matches("WALLPAPR.BMP")) {
    ui_wallpaper_surface_blit_scaled(api, 0, 0, screen_w, screen_h, rect.x, rect.y, rect.w, rect.h);
} else {
    ui_draw_bitmap_clipped(api, "WALLPAPR.BMP", 0, 0, screen_w, screen_h, rect);
}
```

**Contract and fallback:**
- Supported formats: 24bpp and 32bpp uncompressed BMP (same as `ui_draw_bitmap`)
- Max decoded surface: `UI_WALLPAPER_SURFACE_MAX_BYTES` (340 KB) — covers 24bpp BMPs up to the 256 KB file limit
- If the BMP cannot be opened, parsed, or exceeds the limit: `ui_wallpaper_surface_load` returns 0, `g_ui_wallpaper_surface.valid` stays 0, and the wallpaper helpers return 0 — the caller can fall back to `ui_fill_rect` (solid colour) or `ui_draw_bitmap_clipped`
- If the decoded surface size does not match the target desktop size, `ui_wallpaper_surface_blit_scaled` lets the kernel scale it in one blit for both full renders and dirty-rect restores
- If the packed surface call explicitly returns failure after a successful decode, the wallpaper helper samples the same scaled/clipped destination and enqueues adjacent equal-color pixels as one-pixel-high rectangle runs; listview line buffers use the same clipped-run fallback
- `STARTUI` looks for `BG.BMP` under `A:\AIOS` at startup; if absent or invalid it runs with the solid teal desktop unchanged

**Validation:**
```bash
make test-ui-runtime
```
Tests cover syscall dispatch, rectangle-batch ordering/fallback, packed pixel byte order, generic clipped-text flags/fallback, pixel decode correctness (BGR to packed XRGB, bottom-up row order), rejected list-row surface fallback, and fallback on a missing file.

## Design Notes

This layer is meant to be the bridge between the current framebuffer primitives and a later full GUI stack.
If the project evolves toward a real desktop shell, keep the next steps in this order:

1. richer UI event queue (avoid collapsing fast click sequences into a single snapshot)
2. richer present/dirty-rect batching on top of the reserved backbuffer window
3. control tree/layout
4. menu bars, list views, and text editing widgets
5. optional declarative styling format

Do not introduce CSS semantics before the widget tree and event model exist.

The current `startui` demo batches consecutive rectangle fills, routes generated ARGB icons through the surface compositor, preserves its transparent label glyphs as pixel-identical batched `ui_font_8x8` runs, and calls `present` once per frame. Icons retain a clipped run fallback if ARGB surface submission fails. The kernel presents completed frames from the software backbuffer through the ASM fast-present path on standard `24/32 bpp` `R8:G8:B8` framebuffers, and falls back to the generic C copy path when the VBE pixel layout is not directly compatible.

Because this graphics change crosses kernel rendering, syscall validation, the application ABI, and the UI runtime, its validation level is Tier 3:

```bash
make test-ui-runtime
make test-graphics
make test-mouse
make ci
```

`make ci` supplies the repository-wide Tier 3 gate. The three focused commands remain explicit because `make ci` does not currently include the QEMU graphics-performance or mouse/Win95Ui suites.
