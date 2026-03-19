# MiniDOS Graphics API

This document defines the first reusable graphics/UI layer for MiniDOS applications.
It is intentionally small and deterministic so the project can grow toward a Windows 95/98-style GUI without pulling in browser-like complexity.

## Scope

The kernel already exposes low-level graphics syscalls for applications through `external_apps/runtime/minidos_app.h`:

- `app_gfx_clear`
- `app_gfx_rect`
- `app_gfx_text`
- `app_gfx_size`
- `app_gfx_present`
- `app_mouse_state`
- `app_wait_event`
- `app_wait_event_timeout`
- `app_get_time`

The new layer lives in `external_apps/runtime/minidos_ui.h` and builds classic desktop widgets on top of those primitives.

Current goals:

- stable pixel-space drawing API for apps
- classic Win95-like theme defaults
- reusable window, panel, button, label, and text-box helpers
- retained-mode window manager for app-level UI composition
- explicit parent/child controls (label, button, text input)
- mouse snapshots and waitable input events for GUI apps
- RTC time reads for UI elements such as taskbar clocks
- predictable dirty-rect invalidation for software-cursor apps
- explicit frame presentation for GUI apps
- zero dynamic allocation
- no dependency on libc

Non-goals for this phase:

- CSS parsing
- automatic layout/reflow
- overlapping window invalidation

## Files

- `external_apps/runtime/minidos_ui.h`: header-only UI toolkit for external apps
- `external_apps/runtime/minidos_cursor_bitmap.h`: generated cursor bitmap header used by the UI runtime
- `external_apps/templates/win95_demo.c`: reference app that exercises the toolkit
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

Use `ui_theme_classic()` as the default bootstrap theme.

### `ui_window_t`

Describes a top-level classic window:

- bounds
- title
- active/inactive state
- optional close button

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

These are enough to implement custom controls without touching kernel code.

For cursor-driven apps, prefer keeping a short list of dirty rects, repainting the cursor last, and calling `ui_present(api)` once at the end of the frame.

## Widget Helpers

The initial reusable widget layer includes:

- `ui_draw_desktop`
- `ui_draw_panel`
- `ui_draw_window`
- `ui_window_title_bar_rect`
- `ui_window_close_button_rect`
- `ui_window_client_rect`
- `ui_draw_button`
- `ui_button_contains`
- `ui_window_hit_title`
- `ui_window_hit_close`
- `ui_draw_label`
- `ui_draw_label_centered`
- `ui_draw_text_box`
- `ui_draw_cursor`

`ui_draw_cursor` now renders from a generated `16x16` bitmap header. The default asset pipeline is:

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
- `ui_wm_bring_to_front`
- `ui_wm_add_label`
- `ui_wm_add_button`
- `ui_wm_add_textinput`
- `ui_wm_set_focus_control`
- `ui_wm_hit_test_control`
- `ui_wm_dispatch_mouse`
- `ui_wm_dispatch_key`
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

For a fuller example, use `external_apps/templates/win95_demo.c`.

## Build / Validation

Build and install the demo app into `minidos.img`:

```bash
make
./external_apps/add_app.sh external_apps/templates/win95_demo.c WIN95UI
```

Then boot MiniDOS and run:

```text
win95ui
```

Expected result:

- a teal desktop with taskbar
- a classic beveled window
- a text box and two buttons
- a software cursor driven by the PS/2 mouse
- click support for `OK`, `Cancel`, and the title-bar close button
- title-bar window dragging
- keyboard-driven focus cycling with `TAB`
- activation with `SPACE` or `ENTER`
- exit with `Q` or `ESC`

Automated validation:

```bash
make test-mouse
```

The `win95ui` demo also uses `app_wait_event_timeout(api, ..., 1000)` plus `app_get_time(api, ...)` to refresh the taskbar clock once per second without blocking on user input forever.

## Design Notes

This layer is meant to be the bridge between the current framebuffer primitives and a later full GUI stack.
If the project evolves toward a real desktop shell, keep the next steps in this order:

1. richer UI event queue (avoid collapsing fast click sequences into a single snapshot)
2. richer present/dirty-rect batching on top of the reserved backbuffer window
3. control tree/layout
4. menu bars, list views, and text editing widgets
5. optional declarative styling format

Do not introduce CSS semantics before the widget tree and event model exist.

The current `win95ui` demo still batches its drawing and calls `present` once per frame. The kernel presents those completed frames from the software backbuffer through the C copy path, while the older ASM fast-present path stays disabled under the preemptive scheduler.
