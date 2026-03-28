#ifndef MINIDOS_UI_WM_H
#define MINIDOS_UI_WM_H

#include "ui_draw.h"

/* Shared line buffer for listview batch rendering (used by ui_wm_draw and ui_wm_redraw_dirty) */
static ui_listview_line_buf_t g_ui_listview_line_buf;

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

    int drew_bitmap = 0;
    ui_clear(api, theme->desktop_bg);
    if (theme->desktop_bg_bitmap) {
        if (ui_wallpaper_surface_matches(theme->desktop_bg_bitmap)) {
            drew_bitmap = ui_wallpaper_surface_blit_scaled(api, 0, 0, width, height, -1, 0, 0, 0);
        } else {
            drew_bitmap = ui_draw_bitmap(api, theme->desktop_bg_bitmap, 0, 0, width, height);
        }
    }
    (void)drew_bitmap;
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

static inline ui_wm_window_t* ui_wm_find_window(ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == window_id) {
            return &wm->windows[i];
        }
    }
    return 0;
}

static inline const ui_wm_window_t* ui_wm_find_window_const(const ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == window_id) {
            return &wm->windows[i];
        }
    }
    return 0;
}

static inline ui_control_t* ui_wm_find_control(ui_window_manager_t* wm, int control_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].id == control_id) {
            return &wm->controls[i];
        }
    }
    return 0;
}

static inline const ui_control_t* ui_wm_find_control_const(const ui_window_manager_t* wm, int control_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].id == control_id) {
            return &wm->controls[i];
        }
    }
    return 0;
}

static inline void ui_wm_init(ui_window_manager_t* wm, ui_theme_t theme) {
    int i;
    if (!wm) {
        return;
    }
    wm->theme = theme;
    wm->window_count = 0;
    wm->control_count = 0;
    wm->next_window_id = 1;
    wm->next_control_id = 1;
    wm->active_window_id = 0;
    wm->focused_control_id = 0;
    wm->pressed_window_id = 0;
    wm->pressed_control_id = 0;
    wm->pressed_hit_close = 0;
    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        wm->windows[i].id = 0;
        wm->windows[i].visible = 0;
        wm->windows[i].z_order = 0;
    }
    for (i = 0; i < UI_WM_MAX_CONTROLS; i++) {
        wm->controls[i].id = 0;
        wm->controls[i].visible = 0;
        wm->controls[i].enabled = 0;
    }
}

static inline void ui_wm_set_active_window(ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return;
    }
    wm->active_window_id = window_id;
    for (i = 0; i < wm->window_count; i++) {
        wm->windows[i].window.active = (wm->windows[i].id == window_id);
    }
}

static inline int ui_wm_create_window(ui_window_manager_t* wm, ui_rect_t bounds, const char* title, int has_close_button) {
    ui_wm_window_t* entry;
    int id;

    if (!wm || wm->window_count >= UI_WM_MAX_WINDOWS) {
        return 0;
    }

    entry = &wm->windows[wm->window_count];
    id = wm->next_window_id++;

    entry->id = id;
    entry->window.bounds = bounds;
    entry->window.title = title;
    entry->window.active = 0;
    entry->window.has_close_button = has_close_button;
    entry->visible = 1;
    entry->z_order = wm->window_count;
    wm->window_count++;

    ui_wm_set_active_window(wm, id);
    return id;
}

static inline void ui_wm_normalize_z_order(ui_window_manager_t* wm) {
    int drawn[UI_WM_MAX_WINDOWS];
    int draw_count;
    int i;

    if (!wm) {
        return;
    }

    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        drawn[i] = 0;
    }

    for (draw_count = 0; draw_count < wm->window_count; draw_count++) {
        int best_index = -1;
        int best_z = 2147483647;

        for (i = 0; i < wm->window_count; i++) {
            if (drawn[i]) {
                continue;
            }
            if (best_index >= 0 && wm->windows[i].z_order >= best_z) {
                continue;
            }

            best_index = i;
            best_z = wm->windows[i].z_order;
        }

        if (best_index < 0) {
            break;
        }

        drawn[best_index] = 1;
        wm->windows[best_index].z_order = draw_count;
    }
}

static inline void ui_wm_bring_to_front(ui_window_manager_t* wm, int window_id) {
    int i;
    int max_z = -1;
    ui_wm_window_t* target = ui_wm_find_window(wm, window_id);
    if (!wm || !target) {
        return;
    }
    ui_wm_normalize_z_order(wm);
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].z_order > max_z) {
            max_z = wm->windows[i].z_order;
        }
    }
    target->z_order = max_z + 1;
    ui_wm_set_active_window(wm, window_id);
}

static inline ui_control_t* ui_wm_alloc_control(ui_window_manager_t* wm, int type, int window_id, ui_rect_t bounds) {
    ui_control_t* control;
    if (!wm || wm->control_count >= UI_WM_MAX_CONTROLS || !ui_wm_find_window(wm, window_id)) {
        return 0;
    }
    control = &wm->controls[wm->control_count++];
    control->id = wm->next_control_id++;
    control->type = type;
    control->window_id = window_id;
    control->bounds = bounds;
    control->text = "";
    control->text_buffer = 0;
    control->text_buffer_len = 0;
    control->text_len = 0;
    control->visible = 1;
    control->enabled = 1;
    control->focused = 0;
    control->pressed = 0;
    control->listview = 0;
    return control;
}

static inline int ui_wm_add_label(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, const char* text) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_LABEL, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->text = text ? text : "";
    return control->id;
}

static inline int ui_wm_add_button(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, const char* text) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_BUTTON, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->text = text ? text : "";
    return control->id;
}

static inline int ui_wm_add_textinput(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, char* text_buffer, int text_buffer_len) {
    ui_control_t* control;
    int len = 0;

    if (!text_buffer || text_buffer_len <= 0) {
        return 0;
    }

    control = ui_wm_alloc_control(wm, UI_CONTROL_TEXTINPUT, window_id, bounds);
    if (!control) {
        return 0;
    }

    while (len < (text_buffer_len - 1) && text_buffer[len] != '\0') {
        len++;
    }
    text_buffer[len] = '\0';

    control->text = text_buffer;
    control->text_buffer = text_buffer;
    control->text_buffer_len = text_buffer_len;
    control->text_len = len;
    return control->id;
}

static inline int ui_wm_add_listview(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, ui_listview_t* lv) {
    ui_control_t* control;

    if (!lv) {
        return 0;
    }

    control = ui_wm_alloc_control(wm, UI_CONTROL_LISTVIEW, window_id, bounds);
    if (!control) {
        return 0;
    }

    control->listview = lv;
    return control->id;
}

static inline ui_rect_t ui_wm_control_abs_bounds(const ui_window_manager_t* wm, const ui_control_t* control) {
    const ui_wm_window_t* win;
    ui_rect_t client;

    if (!wm || !control) {
        return ui_rect_make(0, 0, 0, 0);
    }

    win = ui_wm_find_window_const(wm, control->window_id);
    if (!win) {
        return ui_rect_make(0, 0, 0, 0);
    }

    client = ui_window_client_rect(&win->window);
    return ui_rect_make(client.x + control->bounds.x, client.y + control->bounds.y, control->bounds.w, control->bounds.h);
}

static inline void ui_wm_set_focus_control(ui_window_manager_t* wm, int control_id) {
    int i;
    if (!wm) {
        return;
    }
    wm->focused_control_id = control_id;
    for (i = 0; i < wm->control_count; i++) {
        wm->controls[i].focused = (wm->controls[i].id == control_id);
    }
}

static inline int ui_wm_hit_test_control(const ui_window_manager_t* wm, int window_id, int x, int y) {
    int i;
    int found_id = 0;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->control_count; i++) {
        const ui_control_t* control = &wm->controls[i];
        ui_rect_t abs_bounds;
        if (!control->visible || !control->enabled || control->window_id != window_id) {
            continue;
        }
        abs_bounds = ui_wm_control_abs_bounds(wm, control);
        if (ui_rect_contains(&abs_bounds, x, y)) {
            found_id = control->id;
        }
    }
    return found_id;
}

static inline int ui_wm_top_window_at(const ui_window_manager_t* wm, int x, int y) {
    int i;
    int best_id = 0;
    int best_z = -2147483647;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->window_count; i++) {
        const ui_wm_window_t* win = &wm->windows[i];
        if (!win->visible) {
            continue;
        }
        if (ui_rect_contains(&win->window.bounds, x, y) && win->z_order >= best_z) {
            best_z = win->z_order;
            best_id = win->id;
        }
    }
    return best_id;
}

static inline int ui_wm_top_visible_window_id(const ui_window_manager_t* wm) {
    int i;
    int best_id = 0;
    int best_z = -2147483647;

    if (!wm) {
        return 0;
    }

    for (i = 0; i < wm->window_count; i++) {
        const ui_wm_window_t* win = &wm->windows[i];

        if (!win->visible) {
            continue;
        }
        if (win->z_order >= best_z) {
            best_z = win->z_order;
            best_id = win->id;
        }
    }

    return best_id;
}

static inline int ui_wm_dispatch_mouse(ui_window_manager_t* wm, int x, int y, int left_down, int left_pressed,
    int left_released, int* out_window_id, int* out_control_id, int* out_hit_close) {
    int i;
    int top_window_id;
    ui_wm_window_t* window;
    int control_id;
    int hit_close;

    if (!wm) {
        return 0;
    }
    (void)left_down;
    if (out_window_id) {
        *out_window_id = 0;
    }
    if (out_control_id) {
        *out_control_id = 0;
    }
    if (out_hit_close) {
        *out_hit_close = 0;
    }

    top_window_id = ui_wm_top_window_at(wm, x, y);
    if (!top_window_id) {
        if (left_released) {
            for (i = 0; i < wm->control_count; i++) {
                wm->controls[i].pressed = 0;
            }
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
        } else if (left_pressed) {
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
        }
        return 0;
    }

    window = ui_wm_find_window(wm, top_window_id);
    if (!window || !window->visible) {
        return 0;
    }

    if (left_pressed) {
        ui_wm_bring_to_front(wm, top_window_id);
    }
    if (out_window_id) {
        *out_window_id = top_window_id;
    }

    hit_close = ui_window_hit_close(&window->window, x, y);
    if (hit_close) {
        if (out_hit_close) {
            *out_hit_close = 1;
        }
        if (left_pressed) {
            wm->pressed_window_id = top_window_id;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 1;
        }
        if (left_released) {
            int activated = wm->pressed_hit_close && (wm->pressed_window_id == top_window_id);
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            return activated ? 1 : 0;
        }
        return 0;
    }

    control_id = ui_wm_hit_test_control(wm, top_window_id, x, y);
    if (control_id) {
        ui_control_t* control = ui_wm_find_control(wm, control_id);
        if (control && left_pressed) {
            wm->pressed_window_id = top_window_id;
            wm->pressed_control_id = control_id;
            wm->pressed_hit_close = 0;
            ui_wm_set_focus_control(wm, control_id);
            for (i = 0; i < wm->control_count; i++) {
                wm->controls[i].pressed = 0;
            }
            if (control->type == UI_CONTROL_BUTTON) {
                control->pressed = 1;
            }
        }

        if (control && left_released) {
            int activated = (control->type == UI_CONTROL_BUTTON)
                && (wm->pressed_window_id == top_window_id)
                && (wm->pressed_control_id == control_id)
                && control->pressed;
            control->pressed = 0;
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            if (out_control_id) {
                *out_control_id = control_id;
            }
            return activated ? 1 : 0;
        }

        if (out_control_id) {
            *out_control_id = control_id;
        }
    }

    if (left_pressed) {
        wm->pressed_window_id = top_window_id;
        wm->pressed_control_id = 0;
        wm->pressed_hit_close = 0;
        for (i = 0; i < wm->control_count; i++) {
            wm->controls[i].pressed = 0;
        }
    }

    if (left_released) {
        for (i = 0; i < wm->control_count; i++) {
            wm->controls[i].pressed = 0;
        }
        wm->pressed_window_id = 0;
        wm->pressed_control_id = 0;
        wm->pressed_hit_close = 0;
    }

    return 0;
}

static inline int ui_wm_dispatch_key(ui_window_manager_t* wm, char key) {
    ui_control_t* focused;

    if (!wm || wm->focused_control_id == 0) {
        return 0;
    }

    focused = ui_wm_find_control(wm, wm->focused_control_id);
    if (!focused || focused->type != UI_CONTROL_TEXTINPUT || !focused->enabled || !focused->text_buffer || focused->text_buffer_len <= 0) {
        return 0;
    }

    if (key == 8) {
        if (focused->text_len > 0) {
            focused->text_len--;
            focused->text_buffer[focused->text_len] = '\0';
            return 1;
        }
        return 0;
    }

    if (key >= 32 && key <= 126) {
        if (focused->text_len < (focused->text_buffer_len - 1)) {
            focused->text_buffer[focused->text_len++] = key;
            focused->text_buffer[focused->text_len] = '\0';
            return 1;
        }
    }

    return 0;
}

static inline void ui_wm_close_window(ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return;
    }
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == window_id) {
            wm->windows[i].visible = 0;
            break;
        }
    }

    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].window_id != window_id) {
            continue;
        }
        wm->controls[i].pressed = 0;
        wm->controls[i].focused = 0;
        if (wm->focused_control_id == wm->controls[i].id) {
            wm->focused_control_id = 0;
        }
        if (wm->pressed_control_id == wm->controls[i].id) {
            wm->pressed_control_id = 0;
        }
    }

    if (wm->active_window_id == window_id) {
        int next_active = ui_wm_top_visible_window_id(wm);
        ui_wm_set_active_window(wm, next_active);
    }
    if (wm->pressed_window_id == window_id) {
        wm->pressed_window_id = 0;
        wm->pressed_hit_close = 0;
    }
}

static inline void ui_wm_draw(const minidos_app_api_t* api, const ui_window_manager_t* wm, int screen_w, int screen_h, const char* desktop_title) {
    int i;
    int drawn[UI_WM_MAX_WINDOWS];
    int draw_count;
    if (!wm) {
        return;
    }

    ui_draw_desktop(api, &wm->theme, screen_w, screen_h, desktop_title);

    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        drawn[i] = 0;
    }

    for (draw_count = 0; draw_count < wm->window_count; draw_count++) {
        int best_index = -1;
        int best_z = 2147483647;

        for (i = 0; i < wm->window_count; i++) {
            const ui_wm_window_t* win = &wm->windows[i];

            if (!win->visible || drawn[i]) {
                continue;
            }
            if (best_index >= 0 && win->z_order >= best_z) {
                continue;
            }

            best_index = i;
            best_z = win->z_order;
        }

        if (best_index < 0) {
            break;
        }

        drawn[best_index] = 1;

        {
            const ui_wm_window_t* win = &wm->windows[best_index];
            int c;

            ui_draw_window(api, &wm->theme, &win->window);

            for (c = 0; c < wm->control_count; c++) {
                const ui_control_t* control = &wm->controls[c];
                ui_rect_t abs_bounds;
                if (!control->visible || control->window_id != win->id) {
                    continue;
                }
                abs_bounds = ui_wm_control_abs_bounds(wm, control);

                if (control->type == UI_CONTROL_LABEL) {
                    ui_draw_label(api, abs_bounds.x, abs_bounds.y, control->text ? control->text : "", &wm->theme, wm->theme.field_bg);
                } else if (control->type == UI_CONTROL_BUTTON) {
                    ui_button_t button;
                    button.bounds = abs_bounds;
                    button.label = control->text ? control->text : "";
                    button.pressed = control->pressed;
                    button.focused = control->focused;
                    button.enabled = control->enabled;
                    ui_draw_button(api, &wm->theme, &button);
                } else if (control->type == UI_CONTROL_TEXTINPUT) {
                    ui_draw_text_box(api, &wm->theme, abs_bounds, control->text ? control->text : "", control->focused);
                } else if (control->type == UI_CONTROL_LISTVIEW && control->listview) {
                    ui_draw_listview(api, &g_ui_listview_line_buf, abs_bounds, control->listview, &wm->theme);
                }
            }
        }
    }
}

/*
 * Layer-aware dirty-rect redraw: repaints only the intersection of each layer
 * with the given dirty rect, in correct z-order:
 *   Layer 0: desktop background
 *   Layer 1..N: windows in z-order (chrome + controls, clipped to dirty)
 *
 * Callers are responsible for drawing overlays (menus, cursor) on top after this.
 */
static inline void ui_wm_redraw_dirty(const minidos_app_api_t* api,
    const ui_window_manager_t* wm, ui_rect_t dirty,
    int screen_w, int screen_h) {
    int i;
    int drawn[UI_WM_MAX_WINDOWS];
    int draw_count;
    ui_rect_t desktop;
    ui_rect_t dirty_desktop;

    if (!api || !wm || ui_rect_is_empty(dirty)) { return; }

    /* Layer 0: desktop background (clipped to dirty) */
    desktop = ui_rect_make(0, 0, screen_w, screen_h);
    dirty_desktop = ui_rect_intersect(dirty, desktop);
    if (!ui_rect_is_empty(dirty_desktop)) {
        ui_fill_rect(api, dirty_desktop, wm->theme.desktop_bg);
        if (wm->theme.desktop_bg_bitmap) {
            if (ui_wallpaper_surface_matches(wm->theme.desktop_bg_bitmap)) {
                (void)ui_wallpaper_surface_blit_scaled(api, 0, 0, screen_w, screen_h,
                    dirty_desktop.x, dirty_desktop.y, dirty_desktop.w, dirty_desktop.h);
            } else {
                (void)ui_draw_bitmap_clipped(api, wm->theme.desktop_bg_bitmap,
                    0, 0, screen_w, screen_h, dirty_desktop);
            }
        }
        /* Desktop accent bar */
        {
            ui_rect_t accent = ui_rect_intersect(dirty_desktop, ui_rect_make(0, 0, screen_w, 2));
            if (!ui_rect_is_empty(accent)) {
                ui_fill_rect(api, accent, wm->theme.desktop_accent);
            }
        }
    }

    /* Layer 1..N: windows in z-order */
    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) { drawn[i] = 0; }

    for (draw_count = 0; draw_count < wm->window_count; draw_count++) {
        int best_index = -1;
        int best_z = 2147483647;
        const ui_wm_window_t* win;
        ui_rect_t win_dirty;
        ui_rect_t title_rect;
        ui_rect_t client_rect;
        int c;

        for (i = 0; i < wm->window_count; i++) {
            if (!wm->windows[i].visible || drawn[i]) { continue; }
            if (best_index >= 0 && wm->windows[i].z_order >= best_z) { continue; }
            best_index = i;
            best_z = wm->windows[i].z_order;
        }
        if (best_index < 0) { break; }
        drawn[best_index] = 1;

        win = &wm->windows[best_index];
        win_dirty = ui_rect_intersect(dirty, win->window.bounds);
        if (ui_rect_is_empty(win_dirty)) { continue; }

        /* Check if dirty touches chrome (title/border) */
        title_rect = ui_window_title_bar_rect(&win->window);
        client_rect = ui_window_client_rect(&win->window);

        if (!ui_rect_is_empty(ui_rect_intersect(dirty, title_rect))
            || win_dirty.x <= win->window.bounds.x + 4
            || win_dirty.y <= win->window.bounds.y + 4
            || win_dirty.x + win_dirty.w >= win->window.bounds.x + win->window.bounds.w - 4
            || win_dirty.y + win_dirty.h >= win->window.bounds.y + win->window.bounds.h - 4) {
            /* Dirty touches chrome — repaint full window chrome */
            ui_draw_window(api, &wm->theme, &win->window);
        } else {
            /* Only client area — fill damaged client region */
            ui_fill_rect(api, ui_rect_intersect(dirty, client_rect), wm->theme.field_bg);
        }

        /* Controls: only those intersecting dirty rect */
        for (c = 0; c < wm->control_count; c++) {
            const ui_control_t* control = &wm->controls[c];
            ui_rect_t abs_bounds;

            if (!control->visible || control->window_id != win->id) { continue; }
            abs_bounds = ui_wm_control_abs_bounds(wm, control);
            if (ui_rect_is_empty(ui_rect_intersect(dirty, abs_bounds))) { continue; }

            if (control->type == UI_CONTROL_LABEL) {
                ui_draw_text_clipped(api, abs_bounds.x, abs_bounds.y,
                    control->text ? control->text : "",
                    wm->theme.text, wm->theme.field_bg, dirty);
            } else if (control->type == UI_CONTROL_BUTTON) {
                ui_button_t button;
                button.bounds = abs_bounds;
                button.label = control->text ? control->text : "";
                button.pressed = control->pressed;
                button.focused = control->focused;
                button.enabled = control->enabled;
                ui_draw_button(api, &wm->theme, &button);
            } else if (control->type == UI_CONTROL_TEXTINPUT) {
                ui_draw_text_box(api, &wm->theme, abs_bounds,
                    control->text ? control->text : "", control->focused);
            } else if (control->type == UI_CONTROL_LISTVIEW && control->listview) {
                ui_draw_listview(api, &g_ui_listview_line_buf, abs_bounds, control->listview, &wm->theme);
            }
        }
    }
}

/*
 * Redraw all dirty rects from a dirty list through the layer compositor.
 * Clears the dirty list after processing.
 */
static inline void ui_wm_flush_dirty(const minidos_app_api_t* api,
    const ui_window_manager_t* wm, ui_dirty_list_t* dirty_list,
    int screen_w, int screen_h) {
    int i;

    if (!api || !wm || !dirty_list) { return; }

    for (i = 0; i < dirty_list->count; i++) {
        ui_wm_redraw_dirty(api, wm, dirty_list->rects[i], screen_w, screen_h);
    }

    dirty_list->count = 0;
}

#endif
