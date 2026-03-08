#ifndef MINIDOS_UI_H
#define MINIDOS_UI_H

#include "minidos_app.h"

#define UI_CHAR_W 8
#define UI_CHAR_H 8

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ui_rect_t;

typedef struct {
    unsigned int desktop_bg;
    unsigned int desktop_accent;
    unsigned int face;
    unsigned int face_alt;
    unsigned int light;
    unsigned int shadow;
    unsigned int dark_shadow;
    unsigned int text;
    unsigned int text_disabled;
    unsigned int title_active_bg;
    unsigned int title_active_text;
    unsigned int title_inactive_bg;
    unsigned int title_inactive_text;
    unsigned int field_bg;
    unsigned int field_text;
    unsigned int selection_bg;
    unsigned int selection_text;
} ui_theme_t;

typedef struct {
    ui_rect_t bounds;
    const char* title;
    int active;
    int has_close_button;
} ui_window_t;

typedef struct {
    ui_rect_t bounds;
    const char* label;
    int pressed;
    int focused;
    int enabled;
} ui_button_t;

static inline int ui_strlen(const char* s) {
    int n = 0;
    while (s && s[n] != '\0') {
        n++;
    }
    return n;
}

static inline ui_rect_t ui_rect_make(int x, int y, int w, int h) {
    ui_rect_t rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    return rect;
}

static inline ui_rect_t ui_rect_inset(ui_rect_t rect, int amount) {
    rect.x += amount;
    rect.y += amount;
    rect.w -= amount * 2;
    rect.h -= amount * 2;
    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static inline int ui_rect_contains(const ui_rect_t* rect, int px, int py) {
    if (!rect) {
        return 0;
    }
    return px >= rect->x && py >= rect->y
        && px < (rect->x + rect->w)
        && py < (rect->y + rect->h);
}

static inline unsigned int ui_rgb(unsigned int r, unsigned int g, unsigned int b) {
    return ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

static inline int ui_mouse_left_down(const app_mouse_state_t* mouse) {
    return mouse && ((mouse->buttons & APP_MOUSE_LEFT) != 0);
}

static inline int ui_mouse_left_pressed(const app_mouse_state_t* prev, const app_mouse_state_t* cur) {
    return !ui_mouse_left_down(prev) && ui_mouse_left_down(cur);
}

static inline int ui_mouse_left_released(const app_mouse_state_t* prev, const app_mouse_state_t* cur) {
    return ui_mouse_left_down(prev) && !ui_mouse_left_down(cur);
}

static inline ui_theme_t ui_theme_classic(void) {
    ui_theme_t theme;
    theme.desktop_bg = 0x008080u;
    theme.desktop_accent = 0x004040u;
    theme.face = 0xC0C0C0u;
    theme.face_alt = 0xD4D0C8u;
    theme.light = 0xFFFFFFu;
    theme.shadow = 0x808080u;
    theme.dark_shadow = 0x000000u;
    theme.text = 0x000000u;
    theme.text_disabled = 0x808080u;
    theme.title_active_bg = 0x000080u;
    theme.title_active_text = 0xFFFFFFu;
    theme.title_inactive_bg = 0x808080u;
    theme.title_inactive_text = 0xD8D8D8u;
    theme.field_bg = 0xFFFFFFu;
    theme.field_text = 0x000000u;
    theme.selection_bg = 0x000080u;
    theme.selection_text = 0xFFFFFFu;
    return theme;
}

static inline int ui_screen_size(const minidos_app_api_t* api, int* out_w, int* out_h) {
    return app_gfx_size(api, out_w, out_h);
}

static inline void ui_clear(const minidos_app_api_t* api, unsigned int color) {
    (void)app_gfx_clear(api, color);
}

static inline void ui_fill_rect(const minidos_app_api_t* api, ui_rect_t rect, unsigned int color) {
    app_gfx_rect_t gfx_rect;
    if (!api || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    gfx_rect.x = rect.x;
    gfx_rect.y = rect.y;
    gfx_rect.w = rect.w;
    gfx_rect.h = rect.h;
    gfx_rect.color = color;
    (void)app_gfx_rect(api, &gfx_rect);
}

static inline void ui_draw_text(const minidos_app_api_t* api, int x, int y, const char* text, unsigned int fg, unsigned int bg) {
    app_gfx_text_t gfx_text;
    if (!api || !text) {
        return;
    }
    gfx_text.x = x;
    gfx_text.y = y;
    gfx_text.text = text;
    gfx_text.fg = fg;
    gfx_text.bg = bg;
    (void)app_gfx_text(api, &gfx_text);
}

static inline void ui_frame_rect(const minidos_app_api_t* api, ui_rect_t rect, unsigned int color) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y, rect.w, 1), color);
    if (rect.h > 1) {
        ui_fill_rect(api, ui_rect_make(rect.x, rect.y + rect.h - 1, rect.w, 1), color);
    }
    if (rect.h > 2) {
        ui_fill_rect(api, ui_rect_make(rect.x, rect.y + 1, 1, rect.h - 2), color);
        if (rect.w > 1) {
            ui_fill_rect(api, ui_rect_make(rect.x + rect.w - 1, rect.y + 1, 1, rect.h - 2), color);
        }
    }
}

static inline void ui_bevel_rect(const minidos_app_api_t* api, ui_rect_t rect, unsigned int top_left, unsigned int bottom_right) {
    if (rect.w <= 1 || rect.h <= 1) {
        return;
    }
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y, rect.w - 1, 1), top_left);
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y, 1, rect.h - 1), top_left);
    ui_fill_rect(api, ui_rect_make(rect.x + rect.w - 1, rect.y, 1, rect.h), bottom_right);
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y + rect.h - 1, rect.w, 1), bottom_right);
}

static inline void ui_draw_panel(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect, int raised) {
    if (!theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    ui_fill_rect(api, rect, theme->face);
    ui_bevel_rect(api, rect, raised ? theme->light : theme->shadow, raised ? theme->dark_shadow : theme->light);
    if (rect.w > 2 && rect.h > 2) {
        ui_bevel_rect(api, ui_rect_inset(rect, 1), raised ? theme->face_alt : theme->dark_shadow,
            raised ? theme->shadow : theme->face_alt);
    }
}

static inline void ui_draw_label(const minidos_app_api_t* api, int x, int y, const char* text, const ui_theme_t* theme, unsigned int bg) {
    unsigned int fg = theme ? theme->text : 0x000000u;
    ui_draw_text(api, x, y, text, fg, bg);
}

static inline void ui_draw_label_centered(const minidos_app_api_t* api, ui_rect_t rect, const char* text,
    unsigned int fg, unsigned int bg) {
    int text_w = ui_strlen(text) * UI_CHAR_W;
    int draw_x = rect.x;
    int draw_y = rect.y;

    if (rect.w > text_w) {
        draw_x += (rect.w - text_w) / 2;
    }
    if (rect.h > UI_CHAR_H) {
        draw_y += (rect.h - UI_CHAR_H) / 2;
    }
    ui_draw_text(api, draw_x, draw_y, text, fg, bg);
}

static inline void ui_draw_text_box(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect, const char* text, int focused) {
    unsigned int fill;
    if (!theme) {
        return;
    }
    ui_draw_panel(api, theme, rect, 0);
    rect = ui_rect_inset(rect, 2);
    fill = focused ? theme->light : theme->field_bg;
    ui_fill_rect(api, rect, fill);
    ui_frame_rect(api, rect, focused ? theme->selection_bg : theme->shadow);
    if (text) {
        ui_draw_text(api, rect.x + 4, rect.y + 4, text, theme->field_text, fill);
    }
}

static inline ui_rect_t ui_window_client_rect(const ui_window_t* window) {
    ui_rect_t rect = ui_rect_make(0, 0, 0, 0);
    if (!window) {
        return rect;
    }
    rect = ui_rect_make(window->bounds.x + 4, window->bounds.y + 22, window->bounds.w - 8, window->bounds.h - 26);
    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static inline ui_rect_t ui_window_title_bar_rect(const ui_window_t* window) {
    if (!window) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(window->bounds.x + 3, window->bounds.y + 3, window->bounds.w - 6, 16);
}

static inline ui_rect_t ui_window_close_button_rect(const ui_window_t* window) {
    ui_rect_t title_rect = ui_window_title_bar_rect(window);
    if (title_rect.w < 20) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(title_rect.x + title_rect.w - 18, title_rect.y + 1, 16, 14);
}

static inline int ui_button_contains(const ui_button_t* button, int x, int y) {
    if (!button) {
        return 0;
    }
    return ui_rect_contains(&button->bounds, x, y);
}

static inline int ui_window_hit_close(const ui_window_t* window, int x, int y) {
    ui_rect_t close_rect = ui_window_close_button_rect(window);
    return window && window->has_close_button && ui_rect_contains(&close_rect, x, y);
}

static inline int ui_window_hit_title(const ui_window_t* window, int x, int y) {
    ui_rect_t title_rect = ui_window_title_bar_rect(window);
    return window && ui_rect_contains(&title_rect, x, y) && !ui_window_hit_close(window, x, y);
}

static inline void ui_draw_button(const minidos_app_api_t* api, const ui_theme_t* theme, const ui_button_t* button) {
    ui_rect_t inner;
    unsigned int fg;
    if (!theme || !button) {
        return;
    }

    ui_fill_rect(api, button->bounds, theme->face);
    if (button->pressed) {
        ui_bevel_rect(api, button->bounds, theme->shadow, theme->light);
        if (button->bounds.w > 2 && button->bounds.h > 2) {
            ui_bevel_rect(api, ui_rect_inset(button->bounds, 1), theme->dark_shadow, theme->face_alt);
        }
    } else {
        ui_bevel_rect(api, button->bounds, theme->light, theme->dark_shadow);
        if (button->bounds.w > 2 && button->bounds.h > 2) {
            ui_bevel_rect(api, ui_rect_inset(button->bounds, 1), theme->face_alt, theme->shadow);
        }
    }

    inner = ui_rect_inset(button->bounds, 3);
    ui_fill_rect(api, inner, theme->face);
    fg = button->enabled ? theme->text : theme->text_disabled;

    if (button->focused && button->bounds.w > 8 && button->bounds.h > 8) {
        ui_frame_rect(api, ui_rect_inset(button->bounds, 4), theme->dark_shadow);
    }

    ui_draw_label_centered(api,
        ui_rect_make(inner.x + (button->pressed ? 1 : 0), inner.y + (button->pressed ? 1 : 0), inner.w, inner.h),
        button->label ? button->label : "",
        fg,
        theme->face);
}

static inline void ui_draw_window(const minidos_app_api_t* api, const ui_theme_t* theme, const ui_window_t* window) {
    ui_rect_t title_rect;
    ui_rect_t client_rect;
    ui_button_t close_button;
    unsigned int title_bg;
    unsigned int title_fg;

    if (!theme || !window) {
        return;
    }

    ui_draw_panel(api, theme, window->bounds, 1);

    title_rect = ui_window_title_bar_rect(window);
    title_bg = window->active ? theme->title_active_bg : theme->title_inactive_bg;
    title_fg = window->active ? theme->title_active_text : theme->title_inactive_text;
    ui_fill_rect(api, title_rect, title_bg);
    ui_draw_text(api, title_rect.x + 6, title_rect.y + 4, window->title ? window->title : "", title_fg, title_bg);

    client_rect = ui_window_client_rect(window);
    ui_fill_rect(api, client_rect, theme->field_bg);

    if (window->has_close_button && title_rect.w >= 20) {
        close_button.bounds = ui_window_close_button_rect(window);
        close_button.label = "X";
        close_button.pressed = 0;
        close_button.focused = 0;
        close_button.enabled = 1;
        ui_draw_button(api, theme, &close_button);
    }
}

static inline void ui_draw_desktop(const minidos_app_api_t* api, const ui_theme_t* theme, int width, int height, const char* title) {
    ui_button_t start_button;
    ui_rect_t taskbar;
    ui_rect_t brand;

    if (!theme || width <= 0 || height <= 0) {
        return;
    }

    ui_clear(api, theme->desktop_bg);
    ui_fill_rect(api, ui_rect_make(0, 0, width, height - 28), theme->desktop_bg);
    ui_fill_rect(api, ui_rect_make(0, 0, width, 2), theme->desktop_accent);

    taskbar = ui_rect_make(0, height - 28, width, 28);
    ui_draw_panel(api, theme, taskbar, 1);

    start_button.bounds = ui_rect_make(4, height - 24, 58, 20);
    start_button.label = "Start";
    start_button.pressed = 0;
    start_button.focused = 0;
    start_button.enabled = 1;
    ui_draw_button(api, theme, &start_button);

    brand = ui_rect_make(width - 130, height - 24, 122, 18);
    ui_draw_panel(api, theme, brand, 0);
    ui_draw_label_centered(api, brand, title ? title : "MiniDOS", theme->text, theme->face);
}

static inline void ui_draw_cursor(const minidos_app_api_t* api, int x, int y, unsigned int fill, unsigned int outline) {
    ui_fill_rect(api, ui_rect_make(x, y, 1, 12), outline);
    ui_fill_rect(api, ui_rect_make(x + 1, y + 1, 1, 10), fill);
    ui_fill_rect(api, ui_rect_make(x + 2, y + 2, 1, 8), fill);
    ui_fill_rect(api, ui_rect_make(x + 3, y + 3, 1, 6), fill);
    ui_fill_rect(api, ui_rect_make(x + 4, y + 4, 1, 4), fill);
    ui_fill_rect(api, ui_rect_make(x + 5, y + 5, 1, 2), fill);
    ui_fill_rect(api, ui_rect_make(x + 2, y + 10, 4, 1), outline);
    ui_fill_rect(api, ui_rect_make(x + 3, y + 11, 3, 1), outline);
}

#endif
