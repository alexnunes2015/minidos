# MiniDOS Graphics API

This document defines the first reusable graphics/UI layer for MiniDOS applications.
It is intentionally small and deterministic so the project can grow toward a Windows 95/98-style GUI without pulling in browser-like complexity.

## Scope

The kernel already exposes low-level graphics syscalls for applications through `external_apps/runtime/minidos_app.h`:

- `app_gfx_clear`
- `app_gfx_rect`
- `app_gfx_text`
- `app_gfx_size`
- `app_mouse_state`
- `app_wait_event`

The new layer lives in `external_apps/runtime/minidos_ui.h` and builds classic desktop widgets on top of those primitives.

Current goals:

- stable pixel-space drawing API for apps
- classic Win95-like theme defaults
- reusable window, panel, button, label, and text-box helpers
- mouse snapshots and waitable input events for GUI apps
- zero dynamic allocation
- no dependency on libc

Non-goals for this phase:

- CSS parsing
- automatic layout/reflow
- compositing/window manager
- overlapping window invalidation

## Files

- `external_apps/runtime/minidos_ui.h`: header-only UI toolkit for external apps
- `external_apps/templates/win95_demo.c`: reference app that exercises the toolkit

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

## Primitive Helpers

`minidos_ui.h` adds these core helpers:

- `ui_rgb(r, g, b)`
- `ui_screen_size(api, &w, &h)`
- `ui_clear(api, color)`
- `ui_fill_rect(api, rect, color)`
- `ui_frame_rect(api, rect, color)`
- `ui_bevel_rect(api, rect, top_left, bottom_right)`
- `ui_draw_text(api, x, y, text, fg, bg)`
- `ui_rect_make(x, y, w, h)`
- `ui_rect_inset(rect, amount)`
- `ui_rect_contains(&rect, x, y)`
- `ui_mouse_left_down(...)`
- `ui_mouse_left_pressed(...)`
- `ui_mouse_left_released(...)`

These are enough to implement custom controls without touching kernel code.

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

## Design Notes

This layer is meant to be the bridge between the current framebuffer primitives and a later full GUI stack.
If the project evolves toward a real desktop shell, keep the next steps in this order:

1. richer UI event queue (avoid collapsing fast click sequences into a single snapshot)
2. redraw invalidation and backbuffering
3. control tree/layout
4. menu bars, list views, and text editing widgets
5. optional declarative styling format

Do not introduce CSS semantics before the widget tree and event model exist.
