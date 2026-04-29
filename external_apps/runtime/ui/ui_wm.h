#ifndef MINIDOS_UI_WM_H
#define MINIDOS_UI_WM_H

#include "ui_draw.h"

/* Shared line buffer for listview batch rendering (used by ui_wm_draw and ui_wm_redraw_dirty) */
#ifdef MINIDOS_UI_IMPLEMENTATION
ui_listview_line_buf_t g_ui_listview_line_buf;
#else
extern ui_listview_line_buf_t g_ui_listview_line_buf;
#endif

static inline ui_rect_t ui_window_client_rect(const ui_window_t* window) {
    ui_rect_t rect = ui_rect_make(0, 0, 0, 0);
    if (!window) {
        return rect;
    }
    if (window->maximized) {
        rect = ui_rect_make(window->bounds.x, window->bounds.y + 18, window->bounds.w, window->bounds.h - 18);
    } else {
        rect = ui_rect_make(window->bounds.x + 4, window->bounds.y + 22, window->bounds.w - 8, window->bounds.h - 26);
    }
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
    if (window->maximized) {
        return ui_rect_make(window->bounds.x, window->bounds.y, window->bounds.w, 18);
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

static inline ui_rect_t ui_window_maximize_button_rect(const ui_window_t* window) {
    ui_rect_t title_rect = ui_window_title_bar_rect(window);
    if (title_rect.w < 56) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(title_rect.x + title_rect.w - 36, title_rect.y + 1, 16, 14);
}

static inline ui_rect_t ui_window_minimize_button_rect(const ui_window_t* window) {
    ui_rect_t title_rect = ui_window_title_bar_rect(window);
    if (title_rect.w < 56) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(title_rect.x + title_rect.w - 54, title_rect.y + 1, 16, 14);
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

static inline int ui_window_hit_maximize(const ui_window_t* window, int x, int y) {
    ui_rect_t max_rect = ui_window_maximize_button_rect(window);
    return window && window->has_maximize_button && ui_rect_contains(&max_rect, x, y);
}

static inline int ui_window_hit_minimize(const ui_window_t* window, int x, int y) {
    ui_rect_t min_rect = ui_window_minimize_button_rect(window);
    return window && window->has_minimize_button && ui_rect_contains(&min_rect, x, y);
}

static inline int ui_window_hit_title(const ui_window_t* window, int x, int y) {
    ui_rect_t title_rect = ui_window_title_bar_rect(window);
    return window && ui_rect_contains(&title_rect, x, y)
        && !ui_window_hit_close(window, x, y)
        && !ui_window_hit_maximize(window, x, y)
        && !ui_window_hit_minimize(window, x, y);
}

static inline ui_rect_t ui_wm_control_abs_bounds(const ui_window_manager_t* wm, const ui_control_t* control);
static inline const ui_wm_window_t* ui_wm_find_window_const(const ui_window_manager_t* wm, int window_id);

static inline ui_rect_t ui_wm_control_render_bounds(const ui_window_manager_t* wm, const ui_control_t* control) {
    ui_rect_t bounds = ui_wm_control_abs_bounds(wm, control);

    if (!control) {
        return ui_rect_make(0, 0, 0, 0);
    }

    if (control->type == UI_CONTROL_DROPDOWN && control->open && control->item_count > 0) {
        return ui_rect_union(bounds, ui_dropdown_popup_rect(bounds, control->item_count));
    }

    return bounds;
}

static inline ui_rect_t ui_wm_control_visible_bounds(const ui_window_manager_t* wm, const ui_control_t* control) {
    const ui_wm_window_t* win;
    ui_rect_t client_rect;
    ui_rect_t render_bounds;

    if (!wm || !control) {
        return ui_rect_make(0, 0, 0, 0);
    }

    win = ui_wm_find_window_const(wm, control->window_id);
    if (!win) {
        return ui_rect_make(0, 0, 0, 0);
    }

    client_rect = ui_window_client_rect(&win->window);
    render_bounds = ui_wm_control_render_bounds(wm, control);
    return ui_rect_intersect(render_bounds, client_rect);
}

static inline int ui_wm_dropdown_item_at(const ui_control_t* control, ui_rect_t abs_bounds, int x, int y) {
    int i;

    if (!control || !control->open) {
        return -1;
    }
    for (i = 0; i < control->item_count; i++) {
        ui_rect_t item_rect = ui_dropdown_item_rect(abs_bounds, i);
        if (ui_rect_contains(&item_rect, x, y)) {
            return i;
        }
    }
    return -1;
}

static inline int ui_wm_menu_item_at(const ui_control_t* control, ui_rect_t abs_bounds, int x, int y) {
    int i;

    if (!control) {
        return -1;
    }
    for (i = 0; i < control->item_count; i++) {
        ui_rect_t item_rect = ui_menu_item_rect(abs_bounds, i);
        if (ui_rect_contains(&item_rect, x, y)) {
            return i;
        }
    }
    return -1;
}

static inline int ui_wm_scrollbar_value_from_thumb(const ui_control_t* control, ui_rect_t abs_bounds, int mouse_y) {
    ui_rect_t track_rect;
    ui_rect_t thumb_rect;
    int range;
    int usable_h;
    int thumb_top;

    if (!control) {
        return 0;
    }

    track_rect = ui_scrollbar_track_rect(abs_bounds);
    thumb_rect = ui_scrollbar_thumb_rect(abs_bounds, control->min_value, control->max_value,
        control->page_size, control->value);
    range = control->max_value - control->min_value;
    usable_h = track_rect.h - thumb_rect.h;
    if (range <= 0 || usable_h <= 0) {
        return control->min_value;
    }

    thumb_top = mouse_y - control->drag_offset;
    if (thumb_top < track_rect.y) {
        thumb_top = track_rect.y;
    }
    if (thumb_top > track_rect.y + usable_h) {
        thumb_top = track_rect.y + usable_h;
    }
    return control->min_value + (((thumb_top - track_rect.y) * range) / usable_h);
}

static inline void ui_wm_close_open_popups(ui_window_manager_t* wm, int except_control_id) {
    int i;

    if (!wm) {
        return;
    }

    for (i = 0; i < wm->control_count; i++) {
        ui_control_t* control = &wm->controls[i];
        if (control->id == 0 || control->id == except_control_id) {
            continue;
        }
        if (control->type == UI_CONTROL_DROPDOWN) {
            control->open = 0;
        }
    }
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
    ui_button_t min_button;
    ui_button_t max_button;
    unsigned int title_bg;
    unsigned int title_fg;
    int title_text_x;

    if (!theme || !window) {
        return;
    }

    title_text_x = 6;
    if (!window->maximized) {
        ui_draw_panel(api, theme, window->bounds, 1);
    } else {
        ui_fill_rect(api, window->bounds, theme->face);
    }

    title_rect = ui_window_title_bar_rect(window);
    title_bg = window->active ? theme->title_active_bg : theme->title_inactive_bg;
    title_fg = window->active ? theme->title_active_text : theme->title_inactive_text;
    ui_fill_rect(api, title_rect, title_bg);

    if (window->icon_id != UI_WINDOW_ICON_NONE) {
        ui_rect_t icon = ui_rect_make(title_rect.x + 2, title_rect.y + 1, 14, 14);
        if (window->icon_id == UI_WINDOW_ICON_FOLDER) {
            /* Simple classic folder glyph (14x14). */
            ui_fill_rect(api, ui_rect_make(icon.x + 1, icon.y + 5, icon.w - 2, icon.h - 6), ui_rgb(236, 196, 52));
            ui_fill_rect(api, ui_rect_make(icon.x + 2, icon.y + 3, 6, 3), ui_rgb(244, 212, 96));
            ui_frame_rect(api, ui_rect_make(icon.x + 1, icon.y + 5, icon.w - 2, icon.h - 6), ui_rgb(160, 128, 32));
            ui_frame_rect(api, ui_rect_make(icon.x + 2, icon.y + 3, 6, 3), ui_rgb(160, 128, 32));
        } else {
            /* Generic fallback: small app square. */
            ui_fill_rect(api, ui_rect_inset(icon, 1), ui_rgb(0, 96, 192));
            ui_frame_rect(api, icon, theme->dark_shadow);
            ui_frame_rect(api, ui_rect_inset(icon, 1), theme->light);
        }
        title_text_x = 18;
    }

    ui_draw_text(api, title_rect.x + title_text_x, title_rect.y + 4,
        window->title ? window->title : "", title_fg, title_bg);

    client_rect = ui_window_client_rect(window);
    ui_fill_rect(api, client_rect, theme->field_bg);

    if (window->has_minimize_button && title_rect.w >= 56) {
        min_button.bounds = ui_window_minimize_button_rect(window);
        min_button.label = "_";
        min_button.pressed = 0;
        min_button.focused = 0;
        min_button.enabled = 1;
        ui_draw_button(api, theme, &min_button);
    }

    if (window->has_maximize_button && title_rect.w >= 56) {
        max_button.bounds = ui_window_maximize_button_rect(window);
        max_button.label = window->maximized ? "2" : "^";
        max_button.pressed = 0;
        max_button.focused = 0;
        max_button.enabled = 1;
        ui_draw_button(api, theme, &max_button);
    }

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
    if (!wm || window_id == 0) {
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
    if (!wm || window_id == 0) {
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
    if (!wm || control_id == 0) {
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
    if (!wm || control_id == 0) {
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
    wm->pressed_hit_minimize = 0;
    wm->pressed_hit_maximize = 0;
    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        wm->windows[i].id = 0;
        wm->windows[i].visible = 0;
        wm->windows[i].z_order = 0;
        wm->windows[i].window.has_close_button = 0;
        wm->windows[i].window.has_minimize_button = 0;
        wm->windows[i].window.has_maximize_button = 0;
        wm->windows[i].window.minimized = 0;
        wm->windows[i].window.maximized = 0;
    }
    for (i = 0; i < UI_WM_MAX_CONTROLS; i++) {
        wm->controls[i].id = 0;
        wm->controls[i].visible = 0;
        wm->controls[i].enabled = 0;
        wm->controls[i].checked = 0;
        wm->controls[i].group_id = 0;
        wm->controls[i].items = 0;
        wm->controls[i].item_count = 0;
        wm->controls[i].selected_index = -1;
        wm->controls[i].open = 0;
        wm->controls[i].hot_index = -1;
        wm->controls[i].min_value = 0;
        wm->controls[i].max_value = 0;
        wm->controls[i].page_size = 0;
        wm->controls[i].value = 0;
        wm->controls[i].drag_offset = 0;
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

static inline int ui_wm_create_window_ex(ui_window_manager_t* wm, ui_rect_t bounds, const char* title,
    int has_close_button, int has_minimize_button, int has_maximize_button) {
    ui_wm_window_t* entry;
    int id;
    int slot = -1;
    int i;

    if (!wm) {
        return 0;
    }

    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (wm->window_count >= UI_WM_MAX_WINDOWS) {
            return 0;
        }
        slot = wm->window_count++;
    }

    entry = &wm->windows[slot];
    id = wm->next_window_id++;

    entry->id = id;
    entry->window.bounds = bounds;
    entry->window.restore_bounds = bounds;
    entry->window.title = title;
    entry->window.icon_id = UI_WINDOW_ICON_NONE;
    entry->window.active = 0;
    entry->window.has_close_button = has_close_button;
    entry->window.has_minimize_button = has_minimize_button;
    entry->window.has_maximize_button = has_maximize_button;
    entry->window.minimized = 0;
    entry->window.maximized = 0;
    entry->visible = 1;
    entry->z_order = 0;
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id != id && wm->windows[i].visible && wm->windows[i].z_order >= entry->z_order) {
            entry->z_order = wm->windows[i].z_order + 1;
        }
    }

    ui_wm_set_active_window(wm, id);
    return id;
}

static inline void ui_wm_set_window_icon(ui_window_manager_t* wm, int window_id, int icon_id) {
    ui_wm_window_t* win;
    if (!wm || window_id == 0) {
        return;
    }
    win = ui_wm_find_window(wm, window_id);
    if (!win) {
        return;
    }
    win->window.icon_id = icon_id;
}

static inline int ui_wm_create_window(ui_window_manager_t* wm, ui_rect_t bounds, const char* title, int has_close_button) {
    return ui_wm_create_window_ex(wm, bounds, title, has_close_button, 0, 0);
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
            if (drawn[i] || wm->windows[i].id == 0 || !wm->windows[i].visible) {
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
        if (wm->windows[i].id == 0 || !wm->windows[i].visible) {
            continue;
        }
        if (wm->windows[i].z_order > max_z) {
            max_z = wm->windows[i].z_order;
        }
    }
    target->z_order = max_z + 1;
    ui_wm_set_active_window(wm, window_id);
}

static inline ui_control_t* ui_wm_alloc_control(ui_window_manager_t* wm, int type, int window_id, ui_rect_t bounds) {
    ui_control_t* control;
    int slot = -1;
    int i;

    if (!wm || !ui_wm_find_window(wm, window_id)) {
        return 0;
    }
    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (wm->control_count >= UI_WM_MAX_CONTROLS) {
            return 0;
        }
        slot = wm->control_count++;
    }
    control = &wm->controls[slot];
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
    control->checked = 0;
    control->group_id = 0;
    control->items = 0;
    control->item_count = 0;
    control->selected_index = -1;
    control->open = 0;
    control->hot_index = -1;
    control->min_value = 0;
    control->max_value = 0;
    control->page_size = 0;
    control->value = 0;
    control->drag_offset = 0;
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

static inline int ui_wm_add_checkbox(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, const char* text, int checked) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_CHECKBOX, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->text = text ? text : "";
    control->checked = checked ? 1 : 0;
    return control->id;
}

static inline int ui_wm_add_radio(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, const char* text, int group_id, int checked) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_RADIO, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->text = text ? text : "";
    control->group_id = group_id;
    control->checked = checked ? 1 : 0;
    return control->id;
}

static inline int ui_wm_add_dropdown(ui_window_manager_t* wm, int window_id, ui_rect_t bounds,
    const char* const* items, int item_count, int selected_index) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_DROPDOWN, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->items = items;
    control->item_count = item_count;
    if (selected_index >= 0 && selected_index < item_count) {
        control->selected_index = selected_index;
    } else {
        control->selected_index = item_count > 0 ? 0 : -1;
    }
    return control->id;
}

static inline int ui_wm_add_menu(ui_window_manager_t* wm, int window_id, ui_rect_t bounds,
    const char* const* items, int item_count, int selected_index) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_MENU, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->items = items;
    control->item_count = item_count;
    control->selected_index = selected_index;
    return control->id;
}

static inline int ui_wm_add_scrollbar(ui_window_manager_t* wm, int window_id, ui_rect_t bounds,
    int min_value, int max_value, int page_size, int value) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_SCROLLBAR, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->min_value = min_value;
    control->max_value = max_value < min_value ? min_value : max_value;
    control->page_size = page_size < 1 ? 1 : page_size;
    if (value < min_value) {
        value = min_value;
    }
    if (value > control->max_value) {
        value = control->max_value;
    }
    control->value = value;
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

    /* Open dropdown popups must stay above sibling controls for hit-testing. */
    for (i = 0; i < wm->control_count; i++) {
        const ui_control_t* control = &wm->controls[i];
        ui_rect_t abs_bounds;
        int item_index;

        if (!control->visible || !control->enabled || control->window_id != window_id
            || control->type != UI_CONTROL_DROPDOWN || !control->open) {
            continue;
        }

        abs_bounds = ui_wm_control_abs_bounds(wm, control);
        item_index = ui_wm_dropdown_item_at(control, abs_bounds, x, y);
        if (item_index >= 0 || ui_rect_contains(&abs_bounds, x, y)) {
            return control->id;
        }
    }

    for (i = 0; i < wm->control_count; i++) {
        const ui_control_t* control = &wm->controls[i];
        ui_rect_t abs_bounds;
        if (!control->visible || !control->enabled || control->window_id != window_id) {
            continue;
        }
        abs_bounds = ui_wm_control_visible_bounds(wm, control);
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
        if (win->id == 0 || !win->visible || win->window.minimized) {
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

        if (win->id == 0 || !win->visible || win->window.minimized) {
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
        if (left_pressed) {
            ui_wm_close_open_popups(wm, 0);
        }
        if (left_released) {
            for (i = 0; i < wm->control_count; i++) {
                wm->controls[i].pressed = 0;
            }
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            wm->pressed_hit_minimize = 0;
            wm->pressed_hit_maximize = 0;
        } else if (left_pressed) {
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            wm->pressed_hit_minimize = 0;
            wm->pressed_hit_maximize = 0;
        }
        return 0;
    }

    window = ui_wm_find_window(wm, top_window_id);
    if (!window || !window->visible) {
        return 0;
    }

    if (left_down && wm->pressed_control_id != 0) {
        ui_control_t* dragging = ui_wm_find_control(wm, wm->pressed_control_id);
        if (dragging && dragging->type == UI_CONTROL_SCROLLBAR) {
            dragging->value = ui_wm_scrollbar_value_from_thumb(dragging,
                ui_wm_control_abs_bounds(wm, dragging), y);
            if (dragging->value < dragging->min_value) {
                dragging->value = dragging->min_value;
            }
            if (dragging->value > dragging->max_value) {
                dragging->value = dragging->max_value;
            }
            if (out_control_id) {
                *out_control_id = dragging->id;
            }
            if (out_window_id) {
                *out_window_id = dragging->window_id;
            }
            return 1;
        }
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
            wm->pressed_hit_minimize = 0;
            wm->pressed_hit_maximize = 0;
        }
        if (left_released) {
            int activated = wm->pressed_hit_close && (wm->pressed_window_id == top_window_id);
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            wm->pressed_hit_minimize = 0;
            wm->pressed_hit_maximize = 0;
            return activated ? 1 : 0;
        }
        return 0;
    }

    control_id = ui_wm_hit_test_control(wm, top_window_id, x, y);
    if (control_id) {
        ui_control_t* control = ui_wm_find_control(wm, control_id);
        if (control && left_pressed) {
            ui_rect_t abs_bounds = ui_wm_control_abs_bounds(wm, control);
            wm->pressed_window_id = top_window_id;
            wm->pressed_control_id = control_id;
            wm->pressed_hit_close = 0;
            wm->pressed_hit_minimize = 0;
            wm->pressed_hit_maximize = 0;
            ui_wm_set_focus_control(wm, control_id);
            ui_wm_close_open_popups(wm, control_id);
            for (i = 0; i < wm->control_count; i++) {
                wm->controls[i].pressed = 0;
            }
            if (control->type == UI_CONTROL_BUTTON) {
                control->pressed = 1;
            } else if (control->type == UI_CONTROL_SCROLLBAR) {
                ui_rect_t thumb_rect = ui_scrollbar_thumb_rect(abs_bounds, control->min_value,
                    control->max_value, control->page_size, control->value);
                ui_rect_t dec_rect = ui_scrollbar_decrement_rect(abs_bounds);
                ui_rect_t inc_rect = ui_scrollbar_increment_rect(abs_bounds);
                ui_rect_t track_rect = ui_scrollbar_track_rect(abs_bounds);

                if (ui_rect_contains(&thumb_rect, x, y)) {
                    control->drag_offset = y - thumb_rect.y;
                } else if (ui_rect_contains(&dec_rect, x, y)) {
                    control->value--;
                } else if (ui_rect_contains(&inc_rect, x, y)) {
                    control->value++;
                } else if (ui_rect_contains(&track_rect, x, y)) {
                    if (y < thumb_rect.y) {
                        control->value -= control->page_size;
                    } else if (y >= thumb_rect.y + thumb_rect.h) {
                        control->value += control->page_size;
                    }
                }
                if (control->value < control->min_value) {
                    control->value = control->min_value;
                }
                if (control->value > control->max_value) {
                    control->value = control->max_value;
                }
            } else if (control->type == UI_CONTROL_DROPDOWN) {
                int item_index = ui_wm_dropdown_item_at(control, abs_bounds, x, y);
                if (item_index >= 0 && control->open) {
                    control->hot_index = item_index;
                    control->pressed = 1;
                } else if (ui_rect_contains(&abs_bounds, x, y)) {
                    control->pressed = 1;
                    control->hot_index = -1;
                } else {
                    control->hot_index = -1;
                }
            } else if (control->type == UI_CONTROL_MENU) {
                control->hot_index = ui_wm_menu_item_at(control, abs_bounds, x, y);
            }
        }

        if (control && left_released) {
            int activated = 0;
            ui_rect_t abs_bounds = ui_wm_control_abs_bounds(wm, control);

            if (control->type == UI_CONTROL_BUTTON) {
                activated = (wm->pressed_window_id == top_window_id)
                    && (wm->pressed_control_id == control_id)
                    && control->pressed;
            } else if (control->type == UI_CONTROL_CHECKBOX) {
                control->checked = control->checked ? 0 : 1;
                activated = 1;
            } else if (control->type == UI_CONTROL_RADIO) {
                int j;
                for (j = 0; j < wm->control_count; j++) {
                    if (wm->controls[j].id != 0
                        && wm->controls[j].window_id == control->window_id
                        && wm->controls[j].type == UI_CONTROL_RADIO
                        && wm->controls[j].group_id == control->group_id) {
                        wm->controls[j].checked = 0;
                    }
                }
                control->checked = 1;
                activated = 1;
            } else if (control->type == UI_CONTROL_DROPDOWN) {
                int item_index = ui_wm_dropdown_item_at(control, abs_bounds, x, y);
                if (control->pressed && control->open && item_index >= 0) {
                    control->selected_index = item_index;
                    control->open = 0;
                    activated = 1;
                } else if (control->pressed && ui_rect_contains(&abs_bounds, x, y)) {
                    control->open = control->open ? 0 : 1;
                    activated = 1;
                } else {
                    control->open = 0;
                }
                control->hot_index = -1;
            } else if (control->type == UI_CONTROL_MENU) {
                int item_index = ui_wm_menu_item_at(control, abs_bounds, x, y);
                if (item_index >= 0) {
                    control->selected_index = item_index;
                    activated = 1;
                }
            } else if (control->type == UI_CONTROL_SCROLLBAR) {
                activated = 1;
            }
            control->pressed = 0;
            control->drag_offset = 0;
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            wm->pressed_hit_minimize = 0;
            wm->pressed_hit_maximize = 0;
            if (out_control_id) {
                *out_control_id = control_id;
            }
            return activated ? 1 : 0;
        }

        if (out_control_id) {
            *out_control_id = control_id;
        }
    }

    if (left_pressed && control_id == 0) {
        wm->pressed_window_id = top_window_id;
        wm->pressed_control_id = 0;
        wm->pressed_hit_close = 0;
        wm->pressed_hit_minimize = 0;
        wm->pressed_hit_maximize = 0;
        ui_wm_close_open_popups(wm, 0);
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
        wm->pressed_hit_minimize = 0;
        wm->pressed_hit_maximize = 0;
    }

    return 0;
}

static inline int ui_wm_dispatch_key(ui_window_manager_t* wm, char key) {
    ui_control_t* focused;

    if (!wm || wm->focused_control_id == 0) {
        return 0;
    }

    focused = ui_wm_find_control(wm, wm->focused_control_id);
    if (!focused || !focused->enabled) {
        return 0;
    }

    if (focused->type == UI_CONTROL_CHECKBOX && (key == 13 || key == 32)) {
        focused->checked = focused->checked ? 0 : 1;
        return 1;
    }

    if (focused->type == UI_CONTROL_RADIO && (key == 13 || key == 32)) {
        int i;
        for (i = 0; i < wm->control_count; i++) {
            if (wm->controls[i].id != 0
                && wm->controls[i].window_id == focused->window_id
                && wm->controls[i].type == UI_CONTROL_RADIO
                && wm->controls[i].group_id == focused->group_id) {
                wm->controls[i].checked = 0;
            }
        }
        focused->checked = 1;
        return 1;
    }

    if (focused->type == UI_CONTROL_DROPDOWN) {
        if (key == 13 || key == 32) {
            focused->open = focused->open ? 0 : 1;
            return 1;
        }
        if ((key == 0x11 || key == 0x15) && focused->selected_index > 0) {
            focused->selected_index--;
            return 1;
        }
        if ((key == 0x12 || key == 0x16) && focused->selected_index < focused->item_count - 1) {
            focused->selected_index++;
            return 1;
        }
        return 0;
    }

    if (focused->type == UI_CONTROL_MENU) {
        if ((key == 0x11 || key == 0x15) && focused->selected_index > 0) {
            focused->selected_index--;
            return 1;
        }
        if ((key == 0x12 || key == 0x16) && focused->selected_index < focused->item_count - 1) {
            focused->selected_index++;
            return 1;
        }
        return (key == 13 || key == 32) ? 1 : 0;
    }

    if (focused->type == UI_CONTROL_SCROLLBAR) {
        if ((key == 0x11 || key == 0x15) && focused->value > focused->min_value) {
            focused->value--;
            return 1;
        }
        if ((key == 0x12 || key == 0x16) && focused->value < focused->max_value) {
            focused->value++;
            return 1;
        }
        return 0;
    }

    if (focused->type != UI_CONTROL_TEXTINPUT || !focused->text_buffer || focused->text_buffer_len <= 0) {
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
    int was_active;
    if (!wm) {
        return;
    }
    was_active = wm->active_window_id == window_id;
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == window_id) {
            wm->windows[i].id = 0;
            wm->windows[i].visible = 0;
            wm->windows[i].z_order = 0;
            wm->windows[i].window.title = "";
            wm->windows[i].window.active = 0;
            wm->windows[i].window.has_minimize_button = 0;
            wm->windows[i].window.has_maximize_button = 0;
            wm->windows[i].window.minimized = 0;
            wm->windows[i].window.maximized = 0;
            break;
        }
    }

    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].window_id != window_id) {
            continue;
        }
        {
            int old_control_id = wm->controls[i].id;
            if (wm->focused_control_id == old_control_id) {
                wm->focused_control_id = 0;
            }
            if (wm->pressed_control_id == old_control_id) {
                wm->pressed_control_id = 0;
            }
        }
        wm->controls[i].id = 0;
        wm->controls[i].window_id = 0;
        wm->controls[i].visible = 0;
        wm->controls[i].enabled = 0;
        wm->controls[i].pressed = 0;
        wm->controls[i].focused = 0;
        wm->controls[i].listview = 0;
        wm->controls[i].items = 0;
        wm->controls[i].item_count = 0;
        wm->controls[i].selected_index = -1;
        wm->controls[i].open = 0;
    }

    if (was_active) {
        int next_active = ui_wm_top_visible_window_id(wm);
        ui_wm_set_active_window(wm, next_active);
    }
    if (wm->pressed_window_id == window_id) {
        wm->pressed_window_id = 0;
        wm->pressed_hit_close = 0;
        wm->pressed_hit_minimize = 0;
        wm->pressed_hit_maximize = 0;
    }
}

static inline void ui_wm_minimize_window(ui_window_manager_t* wm, int window_id) {
    ui_wm_window_t* window;

    if (!wm) {
        return;
    }

    window = ui_wm_find_window(wm, window_id);
    if (!window || !window->visible || !window->window.has_minimize_button) {
        return;
    }

    window->window.minimized = 1;
    if (wm->active_window_id == window_id) {
        ui_wm_set_active_window(wm, ui_wm_top_visible_window_id(wm));
    }
}

static inline void ui_wm_unminimize_window(ui_window_manager_t* wm, int window_id) {
    ui_wm_window_t* window;

    if (!wm) {
        return;
    }

    window = ui_wm_find_window(wm, window_id);
    if (!window || !window->visible
        || (!window->window.has_minimize_button && !window->window.has_maximize_button)) {
        return;
    }

    window->window.minimized = 0;
    ui_wm_bring_to_front(wm, window_id);
}

static inline void ui_wm_restore_window(ui_window_manager_t* wm, int window_id) {
    ui_wm_window_t* window;

    if (!wm) {
        return;
    }

    window = ui_wm_find_window(wm, window_id);
    if (!window || !window->visible || !window->window.has_maximize_button) {
        return;
    }

    window->window.minimized = 0;
    if (window->window.maximized) {
        window->window.bounds = window->window.restore_bounds;
        window->window.maximized = 0;
    }
    ui_wm_bring_to_front(wm, window_id);
}

static inline void ui_wm_maximize_window(ui_window_manager_t* wm, int window_id, ui_rect_t desktop_bounds) {
    ui_wm_window_t* window;

    if (!wm || ui_rect_is_empty(desktop_bounds)) {
        return;
    }

    window = ui_wm_find_window(wm, window_id);
    if (!window || !window->visible || !window->window.has_maximize_button) {
        return;
    }

    window->window.minimized = 0;
    if (!window->window.maximized) {
        window->window.restore_bounds = window->window.bounds;
        window->window.bounds = desktop_bounds;
        window->window.maximized = 1;
    }
    ui_wm_bring_to_front(wm, window_id);
}

static inline void ui_wm_toggle_maximize_window(ui_window_manager_t* wm, int window_id, ui_rect_t desktop_bounds) {
    ui_wm_window_t* window;

    if (!wm) {
        return;
    }

    window = ui_wm_find_window(wm, window_id);
    if (!window || !window->visible) {
        return;
    }

    if (window->window.maximized) {
        ui_wm_restore_window(wm, window_id);
    } else {
        ui_wm_maximize_window(wm, window_id, desktop_bounds);
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

            if (!win->visible || win->window.minimized || drawn[i]) {
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
                abs_bounds = ui_wm_control_visible_bounds(wm, control);
                if (ui_rect_is_empty(abs_bounds)) {
                    continue;
                }

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
                } else if (control->type == UI_CONTROL_CHECKBOX) {
                    ui_draw_checkbox(api, &wm->theme, abs_bounds, control->text ? control->text : "",
                        control->checked, control->focused, control->enabled);
                } else if (control->type == UI_CONTROL_RADIO) {
                    ui_draw_radio_button(api, &wm->theme, abs_bounds, control->text ? control->text : "",
                        control->checked, control->focused, control->enabled);
                } else if (control->type == UI_CONTROL_DROPDOWN) {
                    ui_draw_dropdown(api, &wm->theme, abs_bounds, control->items, control->item_count,
                        control->selected_index, control->open, control->focused, control->hot_index);
                } else if (control->type == UI_CONTROL_MENU) {
                    ui_draw_menu_widget(api, &wm->theme, abs_bounds, control->items,
                        control->item_count, control->selected_index, control->focused);
                } else if (control->type == UI_CONTROL_SCROLLBAR) {
                    ui_draw_scrollbar(api, &wm->theme, abs_bounds, control->min_value, control->max_value,
                        control->page_size, control->value, control->focused);
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
            if (!wm->windows[i].visible || wm->windows[i].window.minimized || drawn[i]) { continue; }
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
                abs_bounds = ui_wm_control_visible_bounds(wm, control);
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
            } else if (control->type == UI_CONTROL_CHECKBOX) {
                ui_draw_checkbox(api, &wm->theme, abs_bounds, control->text ? control->text : "",
                    control->checked, control->focused, control->enabled);
            } else if (control->type == UI_CONTROL_RADIO) {
                ui_draw_radio_button(api, &wm->theme, abs_bounds, control->text ? control->text : "",
                    control->checked, control->focused, control->enabled);
            } else if (control->type == UI_CONTROL_DROPDOWN) {
                ui_draw_dropdown(api, &wm->theme, ui_wm_control_abs_bounds(wm, control), control->items,
                    control->item_count, control->selected_index, control->open, control->focused, control->hot_index);
            } else if (control->type == UI_CONTROL_MENU) {
                ui_draw_menu_widget(api, &wm->theme, ui_wm_control_abs_bounds(wm, control), control->items,
                    control->item_count, control->selected_index, control->focused);
            } else if (control->type == UI_CONTROL_SCROLLBAR) {
                ui_draw_scrollbar(api, &wm->theme, ui_wm_control_abs_bounds(wm, control), control->min_value,
                    control->max_value, control->page_size, control->value, control->focused);
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
