#include "win95_demo.h"

static void draw_inset_field_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, ui_rect_t clip) {
    if (!api || !theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    ui_fill_rect_clipped(api, rect, theme->field_bg, clip);
    ui_bevel_rect_clipped(api, rect, theme->shadow, theme->light, clip);
    if (rect.w > 2 && rect.h > 2) {
        ui_bevel_rect_clipped(api, ui_rect_inset(rect, 1), theme->dark_shadow, theme->face_alt, clip);
    }
}

static void draw_status_panel_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, ui_rect_t clip) {
    if (!api || !theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    ui_draw_panel_clipped(api, theme, rect, 0, clip);
}

static void draw_explorer_chrome_clipped(const minidos_app_api_t* api, const demo_state_t* state,
    const ui_wm_window_t* win, ui_rect_t clip) {
    const ui_theme_t* theme;
    ui_rect_t client_abs;
    ui_rect_t client;
    ui_rect_t menu;
    ui_rect_t toolbar;
    ui_rect_t addr;
    ui_rect_t sidebar;
    ui_rect_t divider;
    ui_rect_t toolbar_btn;

    if (!api || !state || !win) {
        return;
    }

    theme = &state->wm.theme;
    client_abs = ui_window_client_rect(&win->window);
    if (ui_rect_is_empty(ui_rect_intersect(client_abs, clip))) {
        return;
    }

    client = ui_rect_make(client_abs.x, client_abs.y, client_abs.w, client_abs.h);
    menu = ui_rect_make(client.x, client.y, client.w, EXPLORER_MENU_H);
    toolbar = ui_rect_make(client.x, client.y + EXPLORER_MENU_H, client.w, EXPLORER_TOOLBAR_H);
    addr = ui_rect_make(client.x, client.y + EXPLORER_MENU_H + EXPLORER_TOOLBAR_H, client.w, EXPLORER_ADDR_H);
    sidebar = ui_rect_make(client.x + 8, client.y + EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + EXPLORER_ADDR_H + 6,
        EXPLORER_SIDEBAR_W, client.h - (EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + EXPLORER_ADDR_H) - 34);

    ui_fill_rect_clipped(api, menu, theme->face, clip);
    ui_fill_rect_clipped(api, ui_rect_make(menu.x, menu.y + menu.h - 1, menu.w, 1), theme->shadow, clip);
    ui_draw_text_clipped(api, menu.x + 8, menu.y + 5, "File   Edit   View   Go   Favorites   Help",
        theme->text, theme->face, clip);

    ui_draw_panel_clipped(api, theme, toolbar, 1, clip);
    toolbar_btn = ui_rect_make(toolbar.x + 8, toolbar.y + 4, 22, 20);
    ui_draw_panel_clipped(api, theme, toolbar_btn, 1, clip);
    ui_draw_text_clipped(api, toolbar_btn.x + 7, toolbar_btn.y + 6, "<", theme->text, theme->face, clip);
    toolbar_btn = ui_rect_make(toolbar.x + 34, toolbar.y + 4, 22, 20);
    ui_draw_panel_clipped(api, theme, toolbar_btn, 1, clip);
    ui_draw_text_clipped(api, toolbar_btn.x + 7, toolbar_btn.y + 6, ">", theme->text, theme->face, clip);
    toolbar_btn = ui_rect_make(toolbar.x + 60, toolbar.y + 4, 22, 20);
    ui_draw_panel_clipped(api, theme, toolbar_btn, 1, clip);
    ui_draw_text_clipped(api, toolbar_btn.x + 7, toolbar_btn.y + 6, "^", theme->text, theme->face, clip);

    divider = ui_rect_make(toolbar.x + 90, toolbar.y + 3, 2, toolbar.h - 6);
    ui_fill_rect_clipped(api, ui_rect_make(divider.x, divider.y, 1, divider.h), theme->shadow, clip);
    ui_fill_rect_clipped(api, ui_rect_make(divider.x + 1, divider.y, 1, divider.h), theme->light, clip);

    ui_fill_rect_clipped(api, addr, theme->face, clip);
    ui_fill_rect_clipped(api, ui_rect_make(addr.x, addr.y + addr.h - 1, addr.w, 1), theme->shadow, clip);

    if (sidebar.h > 0) {
        ui_draw_panel_clipped(api, theme, sidebar, 0, clip);
        ui_draw_text_clipped(api, sidebar.x + 10, sidebar.y + 10, "Select an item to",
            theme->text, theme->face, clip);
        ui_draw_text_clipped(api, sidebar.x + 10, sidebar.y + 20, "view its description.",
            theme->text, theme->face, clip);
        ui_draw_text_clipped(api, sidebar.x + 10, sidebar.y + 40, "(C:)", theme->title_active_bg, theme->face, clip);
    }
}

void redraw_region(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t rect) {
    int i;
    int draw_count;
    int drawn[UI_WM_MAX_WINDOWS];
    ui_rect_t desktop;
    ui_rect_t dirty_desktop;

    if (!api || !state || ui_rect_is_empty(rect)) {
        return;
    }

    desktop = ui_rect_make(0, 0, state->sw, state->sh);
    dirty_desktop = ui_rect_intersect(rect, desktop);
    if (!ui_rect_is_empty(dirty_desktop)) {
        ui_fill_rect(api, dirty_desktop, state->wm.theme.desktop_bg);
        if (state->wm.theme.desktop_bg_bitmap) {
            if (ui_wallpaper_surface_matches(state->wm.theme.desktop_bg_bitmap)) {
                (void)ui_wallpaper_surface_blit_scaled(api, 0, 0, state->sw, state->sh,
                    dirty_desktop.x, dirty_desktop.y, dirty_desktop.w, dirty_desktop.h);
            } else {
                (void)ui_draw_bitmap_clipped(api, state->wm.theme.desktop_bg_bitmap,
                    0, 0, state->sw, state->sh, dirty_desktop);
            }
        }

        {
            ui_rect_t accent = ui_rect_intersect(dirty_desktop, ui_rect_make(0, 0, state->sw, 2));
            if (!ui_rect_is_empty(accent)) {
                ui_fill_rect(api, accent, state->wm.theme.desktop_accent);
            }
        }

        draw_desktop_items(api, state, dirty_desktop);
    }

    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        drawn[i] = 0;
    }

    for (draw_count = 0; draw_count < state->wm.window_count; draw_count++) {
        int best_index = -1;
        int best_z = 2147483647;
        const ui_wm_window_t* win;
        ui_rect_t win_dirty;
        ui_rect_t title_rect;
        ui_rect_t client_rect;
        int c;

        for (i = 0; i < state->wm.window_count; i++) {
            if (!state->wm.windows[i].visible || state->wm.windows[i].window.minimized || drawn[i]) {
                continue;
            }
            if (best_index >= 0 && state->wm.windows[i].z_order >= best_z) {
                continue;
            }
            best_index = i;
            best_z = state->wm.windows[i].z_order;
        }
        if (best_index < 0) {
            break;
        }
        drawn[best_index] = 1;

        win = &state->wm.windows[best_index];
        win_dirty = ui_rect_intersect(rect, win->window.bounds);
        if (ui_rect_is_empty(win_dirty)) {
            continue;
        }

        title_rect = ui_window_title_bar_rect(&win->window);
        client_rect = ui_window_client_rect(&win->window);

        if (!ui_rect_is_empty(ui_rect_intersect(rect, title_rect))
            || win_dirty.x <= win->window.bounds.x + 4
            || win_dirty.y <= win->window.bounds.y + 4
            || win_dirty.x + win_dirty.w >= win->window.bounds.x + win->window.bounds.w - 4
            || win_dirty.y + win_dirty.h >= win->window.bounds.y + win->window.bounds.h - 4) {
            ui_draw_window(api, &state->wm.theme, &win->window);
        } else {
            ui_fill_rect(api, ui_rect_intersect(rect, client_rect), state->wm.theme.field_bg);
        }

        {
            const explorer_state_t* explorer = explorer_for_window((demo_state_t*)state, win->id);
            if (explorer) {
                draw_explorer_chrome_clipped(api, state, win, rect);
            }
        }

        for (c = 0; c < state->wm.control_count; c++) {
            const ui_control_t* control = &state->wm.controls[c];
            ui_rect_t abs_bounds;

            if (!control->visible || control->window_id != win->id) {
                continue;
            }
            abs_bounds = ui_wm_control_abs_bounds(&state->wm, control);
            if (ui_rect_is_empty(ui_rect_intersect(rect, abs_bounds))) {
                continue;
            }

            if (control->type == UI_CONTROL_LABEL) {
                const explorer_state_t* explorer = explorer_for_control((demo_state_t*)state, control->id);
                int text_y = abs_bounds.y;

                if (abs_bounds.h > UI_CHAR_H) {
                    text_y += (abs_bounds.h - UI_CHAR_H) / 2;
                }

                if (explorer && control->id == explorer->path_value_id) {
                    draw_inset_field_clipped(api, &state->wm.theme, abs_bounds, rect);
                    ui_draw_text_clipped(api, abs_bounds.x + 4, text_y,
                        control->text ? control->text : "",
                        state->wm.theme.field_text, state->wm.theme.field_bg, rect);
                } else if (explorer && (control->id == explorer->status_label_id
                    || control->id == explorer->status_count_id
                    || control->id == explorer->status_extra_id)) {
                    draw_status_panel_clipped(api, &state->wm.theme, abs_bounds, rect);
                    ui_draw_text_clipped(api, abs_bounds.x + 4, text_y,
                        control->text ? control->text : "",
                        state->wm.theme.text, state->wm.theme.face, rect);
                } else {
                    ui_draw_text_clipped(api, abs_bounds.x, abs_bounds.y,
                        control->text ? control->text : "",
                        state->wm.theme.text, state->wm.theme.field_bg, rect);
                }
            } else if (control->type == UI_CONTROL_BUTTON) {
                ui_button_t button;
                button.bounds = abs_bounds;
                button.label = control->text ? control->text : "";
                button.pressed = control->pressed;
                button.focused = control->focused;
                button.enabled = control->enabled;
                ui_draw_button(api, &state->wm.theme, &button);
            } else if (control->type == UI_CONTROL_TEXTINPUT) {
                ui_draw_text_box(api, &state->wm.theme, abs_bounds,
                    control->text ? control->text : "", control->focused);
            } else if (control->type == UI_CONTROL_LISTVIEW && control->listview) {
                const explorer_state_t* explorer = explorer_for_control((demo_state_t*)state, control->id);
                if (explorer) {
                    draw_explorer_grid(api, state, explorer, abs_bounds, rect);
                } else {
                    ui_draw_listview(api, &g_ui_listview_line_buf, abs_bounds, control->listview, &state->wm.theme);
                }
            }
        }
    }
}

void render_clock_update(const minidos_app_api_t* api, const demo_state_t* state) {
    ui_rect_t clock_rect;
    ui_rect_t cursor_rect;

    if (!api || !state) {
        return;
    }

    clock_rect = taskbar_clock_rect(state);
    draw_taskbar_overlay(api, state);

    if (state->mouse.present) {
        cursor_rect = cursor_rect_at(state->mouse.x, state->mouse.y);
        if (!ui_rect_is_empty(ui_rect_intersect(clock_rect, cursor_rect))) {
            ui_draw_cursor(api, state->mouse.x, state->mouse.y,
                state->wm.theme.light, state->wm.theme.dark_shadow);
        }
    }

    ui_present(api);
}

void render_partial_motion(const minidos_app_api_t* api,
    const demo_state_t* state,
    const app_mouse_state_t* previous_mouse) {
    ui_dirty_list_t dirty;
    ui_rect_t previous_cursor_rect;
    ui_rect_t current_cursor_rect;
    ui_rect_t bar_rect;
    ui_rect_t menu_rect;
    int i;

    if (!api || !state || !previous_mouse) {
        return;
    }

    ui_dirty_list_init(&dirty);
    previous_cursor_rect = cursor_rect_at(previous_mouse->x, previous_mouse->y);
    current_cursor_rect = cursor_rect_at(state->mouse.x, state->mouse.y);
    ui_dirty_list_add(&dirty, previous_cursor_rect);
    ui_dirty_list_add(&dirty, current_cursor_rect);
    add_window_damage_for_cursor(&dirty, state, previous_cursor_rect, current_cursor_rect);

    bar_rect = taskbar_rect(state);
    menu_rect = start_menu_rect(state);

    for (i = 0; i < dirty.count; i++) {
        redraw_region(api, state, dirty.rects[i]);
    }

    for (i = 0; i < dirty.count; i++) {
        if (!ui_rect_is_empty(ui_rect_intersect(dirty.rects[i], bar_rect))) {
            draw_taskbar_overlay(api, state);
            break;
        }
    }

    if (state->start_menu_open) {
        for (i = 0; i < dirty.count; i++) {
            if (!ui_rect_is_empty(ui_rect_intersect(dirty.rects[i], menu_rect))) {
                draw_start_menu(api, state);
                break;
            }
        }
    }

    draw_resize_hint(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

void render_partial_drag(const minidos_app_api_t* api,
    const demo_state_t* state,
    ui_rect_t previous_drag_rect,
    const app_mouse_state_t* previous_mouse) {
    ui_dirty_list_t dirty;
    ui_rect_t previous_cursor_rect;
    ui_rect_t current_cursor_rect;
    ui_rect_t bar_rect;
    ui_rect_t menu_rect;
    int i;

    if (!api || !state || !previous_mouse) {
        return;
    }

    ui_dirty_list_init(&dirty);
    ui_dirty_list_add(&dirty, previous_drag_rect);
    ui_dirty_list_add(&dirty, state->drag_preview_bounds);
    previous_cursor_rect = cursor_rect_at(previous_mouse->x, previous_mouse->y);
    current_cursor_rect = cursor_rect_at(state->mouse.x, state->mouse.y);
    ui_dirty_list_add(&dirty, previous_cursor_rect);
    ui_dirty_list_add(&dirty, current_cursor_rect);
    add_window_damage_for_cursor(&dirty, state, previous_cursor_rect, current_cursor_rect);

    bar_rect = taskbar_rect(state);
    menu_rect = start_menu_rect(state);

    for (i = 0; i < dirty.count; i++) {
        redraw_region(api, state, dirty.rects[i]);
    }

    for (i = 0; i < dirty.count; i++) {
        if (!ui_rect_is_empty(ui_rect_intersect(dirty.rects[i], bar_rect))) {
            draw_taskbar_overlay(api, state);
            break;
        }
    }

    if (state->start_menu_open) {
        for (i = 0; i < dirty.count; i++) {
            if (!ui_rect_is_empty(ui_rect_intersect(dirty.rects[i], menu_rect))) {
                draw_start_menu(api, state);
                break;
            }
        }
    }

    draw_resize_hint(api, state);
    draw_drag_outline(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

void render(const minidos_app_api_t* api, const demo_state_t* state) {
    if (!api || !state) {
        return;
    }

    redraw_region(api, state, ui_rect_make(0, 0, state->sw, state->sh));

    draw_taskbar_overlay(api, state);
    draw_start_menu(api, state);
    draw_resize_hint(api, state);
    draw_drag_outline(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}
