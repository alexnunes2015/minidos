#include "win95_demo.h"

enum {
    TITLE_BUTTON_NONE = 0,
    TITLE_BUTTON_MINIMIZE = 1,
    TITLE_BUTTON_MAXIMIZE = 2,
};

static void close_main_window(demo_state_t* state) {
    if (!state) {
        return;
    }

    dismiss_start_menu(state);
    ui_wm_close_window(&state->wm, state->window_id);
}

static void clear_window_interaction_state(demo_state_t* state, int window_id) {
    if (!state || window_id == 0) {
        return;
    }

    if (state->dragging_window_id == window_id) {
        state->dragging = 0;
        state->dragging_window_id = 0;
    }

    if (state->resizing_window_id == window_id) {
        state->resizing = 0;
        state->resizing_window_id = 0;
        state->resize_edges = 0;
        state->resize_start_bounds = ui_rect_make(0, 0, 0, 0);
    }

    if (state->resize_hover_window_id == window_id) {
        state->resize_hover_window_id = 0;
        state->resize_hover_edges = 0;
    }

    if (state->title_button_pressed_window_id == window_id) {
        state->title_button_pressed_window_id = 0;
        state->title_button_pressed_action = TITLE_BUTTON_NONE;
    }
}

static void activate_taskbar_window(demo_state_t* state, int window_id) {
    ui_wm_window_t* win;
    explorer_state_t* explorer;

    if (!state || window_id == 0) {
        return;
    }

    win = ui_wm_find_window(&state->wm, window_id);
    if (!win || !win->visible) {
        return;
    }

    explorer = explorer_for_window(state, window_id);
    if (win->window.minimized || state->wm.active_window_id != window_id) {
        ui_wm_unminimize_window(&state->wm, window_id);
        if (explorer) {
            explorer_relayout_window(state, window_id);
        }
    } else {
        ui_wm_minimize_window(&state->wm, window_id);
    }
    state->layout_version++;
}

static int title_button_hit_action(const ui_wm_window_t* win, int x, int y) {
    if (!win) {
        return TITLE_BUTTON_NONE;
    }
    if (ui_window_hit_minimize(&win->window, x, y)) {
        return TITLE_BUTTON_MINIMIZE;
    }
    if (ui_window_hit_maximize(&win->window, x, y)) {
        return TITLE_BUTTON_MAXIMIZE;
    }
    return TITLE_BUTTON_NONE;
}

static void activate_title_button(demo_state_t* state, int window_id, int action) {
    explorer_state_t* explorer;

    if (!state || window_id == 0) {
        return;
    }

    if (action == TITLE_BUTTON_MINIMIZE) {
        ui_wm_minimize_window(&state->wm, window_id);
        state->layout_version++;
        return;
    }

    if (action == TITLE_BUTTON_MAXIMIZE) {
        ui_wm_toggle_maximize_window(&state->wm, window_id, window_desktop_bounds(state));
        explorer = explorer_for_window(state, window_id);
        if (explorer) {
            explorer_relayout_window(state, window_id);
        }
        state->layout_version++;
    }
}

static int update_resize_hover(demo_state_t* state) {
    int top_window_id;
    int edges = 0;
    int changed;
    ui_rect_t start_rect;
    ui_rect_t taskbar;
    ui_rect_t menu;

    if (!state || state->resizing || state->dragging) {
        return 0;
    }

    start_rect = start_button_rect(state);
    taskbar = taskbar_rect(state);
    menu = start_menu_rect(state);
    if (ui_rect_contains(&start_rect, state->mouse.x, state->mouse.y)
        || ui_rect_contains(&taskbar, state->mouse.x, state->mouse.y)
        || (state->start_menu_open && ui_rect_contains(&menu, state->mouse.x, state->mouse.y))) {
        changed = state->resize_hover_window_id != 0 || state->resize_hover_edges != 0;
        state->resize_hover_window_id = 0;
        state->resize_hover_edges = 0;
        return changed;
    }

    top_window_id = ui_wm_top_window_at(&state->wm, state->mouse.x, state->mouse.y);
    if (top_window_id != 0) {
        edges = window_resize_hit_test(state, top_window_id, state->mouse.x, state->mouse.y);
    }

    if (edges == 0) {
        top_window_id = 0;
    }

    changed = state->resize_hover_window_id != top_window_id
        || state->resize_hover_edges != edges;
    state->resize_hover_window_id = top_window_id;
    state->resize_hover_edges = edges;
    return changed;
}

static void clamp_resize_preview(const demo_state_t* state, ui_rect_t* rect, int edges) {
    ui_rect_t desktop;
    int right;
    int bottom;

    if (!state || !rect) {
        return;
    }

    desktop = window_desktop_bounds(state);
    right = rect->x + rect->w;
    bottom = rect->y + rect->h;

    if (rect->w < WINDOW_MIN_W) {
        if (edges & RESIZE_EDGE_LEFT) {
            rect->x = right - WINDOW_MIN_W;
        }
        rect->w = WINDOW_MIN_W;
    }
    if (rect->h < WINDOW_MIN_H) {
        if (edges & RESIZE_EDGE_TOP) {
            rect->y = bottom - WINDOW_MIN_H;
        }
        rect->h = WINDOW_MIN_H;
    }

    if (rect->x < desktop.x) {
        rect->w -= desktop.x - rect->x;
        rect->x = desktop.x;
    }
    if (rect->y < desktop.y) {
        rect->h -= desktop.y - rect->y;
        rect->y = desktop.y;
    }
    if (rect->x + rect->w > desktop.x + desktop.w) {
        rect->w = desktop.x + desktop.w - rect->x;
    }
    if (rect->y + rect->h > desktop.y + desktop.h) {
        rect->h = desktop.y + desktop.h - rect->y;
    }

    if (rect->w < WINDOW_MIN_W) {
        rect->w = WINDOW_MIN_W;
    }
    if (rect->h < WINDOW_MIN_H) {
        rect->h = WINDOW_MIN_H;
    }
}

static ui_rect_t resize_preview_for_mouse(const demo_state_t* state) {
    ui_rect_t rect;
    int dx;
    int dy;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    rect = state->resize_start_bounds;
    dx = state->mouse.x - state->resize_start_mouse_x;
    dy = state->mouse.y - state->resize_start_mouse_y;

    if (state->resize_edges & RESIZE_EDGE_LEFT) {
        rect.x += dx;
        rect.w -= dx;
    }
    if (state->resize_edges & RESIZE_EDGE_RIGHT) {
        rect.w += dx;
    }
    if (state->resize_edges & RESIZE_EDGE_TOP) {
        rect.y += dy;
        rect.h -= dy;
    }
    if (state->resize_edges & RESIZE_EDGE_BOTTOM) {
        rect.h += dy;
    }

    clamp_resize_preview(state, &rect, state->resize_edges);
    return rect;
}

static int handle_activated_control(const minidos_app_api_t* api, demo_state_t* state, int control_id) {
    if (control_id == state->button_ok_id) {
        sync_showcase_state(state);
        update_status_text(state, "Valores aplicados na janela de componentes.");
        return 0;
    }
    if (control_id == state->button_cancel_id) {
        close_main_window(state);
        return 0;
    }
    if (control_id == state->checkbox_sound_id) {
        sync_showcase_state(state);
        update_status_text(state, "Checkbox 'Ativar sons' atualizado.");
        return 0;
    }
    if (control_id == state->checkbox_grid_id) {
        sync_showcase_state(state);
        update_status_text(state, "Checkbox 'Mostrar grelha' atualizado.");
        return 0;
    }
    if (control_id == state->radio_theme_classic_id || control_id == state->radio_theme_cloud_id) {
        sync_showcase_state(state);
        update_status_text(state, "Tema selecionado na janela de teste.");
        return 0;
    }
    if (control_id == state->dropdown_speed_id) {
        sync_showcase_state(state);
        update_status_text(state, "Velocidade selecionada no dropdown.");
        return 0;
    }
    if (control_id == state->menu_actions_id) {
        sync_showcase_state(state);
        update_status_text(state, "Acao escolhida no menu widget.");
        return 0;
    }
    if (control_id == state->scrollbar_zoom_id) {
        sync_showcase_state(state);
        update_status_text(state, "Zoom ajustado no scrollbar.");
        return 0;
    }
    {
        explorer_state_t* explorer = explorer_for_control(state, control_id);
        if (explorer && control_id == explorer->back_button_id) {
            (void)explorer_go_back_in(api, state, explorer);
            return 0;
        }
        if (explorer && control_id == explorer->forward_button_id) {
            (void)explorer_go_forward_in(api, state, explorer);
            return 0;
        }
        if (explorer && control_id == explorer->up_button_id) {
            (void)explorer_go_up_in(api, state, explorer);
            return 0;
        }
        if (explorer && control_id == explorer->open_button_id) {
            (void)explorer_open_selected_in(api, state, explorer);
            return 0;
        }
    }
    return 0;
}

/* Visual WM state that ui_wm_dispatch_mouse can clear without reporting a
 * control id (e.g. releasing outside a pressed button, or a click on empty
 * space closing an open dropdown). Snapshot it around the dispatch so those
 * transitions still trigger a repaint instead of leaving stale pixels. */
static void snapshot_wm_visual_state(const demo_state_t* state, int* popup_open, int* pressed_any) {
    int i;

    *popup_open = 0;
    *pressed_any = 0;
    if (!state) {
        return;
    }

    for (i = 0; i < state->wm.control_count; i++) {
        const ui_control_t* control = &state->wm.controls[i];

        if (control->id == 0 || !control->visible) {
            continue;
        }
        if (control->type == UI_CONTROL_DROPDOWN && control->open) {
            *popup_open = 1;
        }
        if (control->pressed) {
            *pressed_any = 1;
        }
    }
}

static int explorer_window_is_active(demo_state_t* state, explorer_state_t** out_explorer) {
    explorer_state_t* explorer;

    if (out_explorer) {
        *out_explorer = 0;
    }
    if (!state) {
        return 0;
    }
    explorer = explorer_for_window(state, state->wm.active_window_id);
    if (!explorer) {
        return 0;
    }
    if (out_explorer) {
        *out_explorer = explorer;
    }
    return 1;
}

static void cycle_focus(demo_state_t* state) {
    int focus_order[9];
    int count = 0;
    int i;
    int next_index = 0;

    if (!state) {
        return;
    }

    focus_order[count++] = state->input_id;
    focus_order[count++] = state->dropdown_speed_id;
    focus_order[count++] = state->checkbox_sound_id;
    focus_order[count++] = state->checkbox_grid_id;
    focus_order[count++] = state->radio_theme_classic_id;
    focus_order[count++] = state->radio_theme_cloud_id;
    focus_order[count++] = state->menu_actions_id;
    focus_order[count++] = state->scrollbar_zoom_id;
    focus_order[count++] = state->button_ok_id;

    for (i = 0; i < count; i++) {
        if (focus_order[i] == state->wm.focused_control_id) {
            next_index = (i + 1) % count;
            break;
        }
    }

    ui_wm_set_focus_control(&state->wm, focus_order[next_index]);
    update_status_text(state, "Foco movido por teclado.");
}

static void cycle_explorer_focus(demo_state_t* state) {
    explorer_state_t* explorer = 0;
    int next_id;

    if (!explorer_window_is_active(state, &explorer)) {
        return;
    }

    if (state->wm.focused_control_id == explorer->listview_id) {
        next_id = explorer->back_button_id;
    } else if (state->wm.focused_control_id == explorer->back_button_id) {
        next_id = explorer->forward_button_id;
    } else if (state->wm.focused_control_id == explorer->forward_button_id) {
        next_id = explorer->up_button_id;
    } else if (state->wm.focused_control_id == explorer->up_button_id) {
        next_id = explorer->open_button_id;
    } else {
        next_id = explorer->listview_id;
    }

    ui_wm_set_focus_control(&state->wm, next_id);
    state->layout_version++;
}

int handle_keyboard(const minidos_app_api_t* api, demo_state_t* state, char c) {
    if (c == 'q' || c == 'Q' || c == KEY_ESC) {
        return 1;
    }

    if (c == KEY_TAB) {
        if (explorer_window_is_active(state, 0)) {
            cycle_explorer_focus(state);
            return 0;
        }
        cycle_focus(state);
        return 0;
    }

    {
        explorer_state_t* explorer = 0;
        if (explorer_window_is_active(state, &explorer)) {
        int explorer_columns = 1;
        const ui_control_t* explorer_control = ui_wm_find_control_const(&state->wm, explorer->listview_id);
        if (explorer_control) {
            ui_rect_t explorer_bounds = ui_wm_control_abs_bounds(&state->wm, explorer_control);
            explorer_columns = explorer_bounds.w / EXPLORER_GRID_CELL_W;
            if (explorer_columns < 1) {
                explorer_columns = 1;
            }
        }
        if (c == KEY_UP) {
            explorer_move_selection_in(state, explorer, -explorer_columns);
            return 0;
        }
        if (c == KEY_DOWN) {
            explorer_move_selection_in(state, explorer, explorer_columns);
            return 0;
        }
        if (c == KEY_LEFT) {
            explorer_move_selection_in(state, explorer, -1);
            return 0;
        }
        if (c == KEY_RIGHT) {
            explorer_move_selection_in(state, explorer, 1);
            return 0;
        }
        if (c == KEY_BACKSPACE) {
            (void)explorer_go_up_in(api, state, explorer);
            return 0;
        }
        if (c == '[') {
            (void)explorer_go_back_in(api, state, explorer);
            return 0;
        }
        if (c == ']') {
            (void)explorer_go_forward_in(api, state, explorer);
            return 0;
        }
        if (c == KEY_ENTER) {
            if (state->wm.focused_control_id == explorer->back_button_id) {
                (void)explorer_go_back_in(api, state, explorer);
            } else if (state->wm.focused_control_id == explorer->forward_button_id) {
                (void)explorer_go_forward_in(api, state, explorer);
            } else if (state->wm.focused_control_id == explorer->up_button_id) {
                (void)explorer_go_up_in(api, state, explorer);
            } else if (state->wm.focused_control_id == explorer->open_button_id) {
                (void)explorer_open_selected_in(api, state, explorer);
            } else {
                (void)explorer_open_selected_in(api, state, explorer);
            }
            return 0;
        }
        }
    }

    if (c == KEY_ENTER || c == KEY_SPACE) {
        if (state->wm.focused_control_id == state->button_ok_id
            || state->wm.focused_control_id == state->button_cancel_id) {
            return handle_activated_control(api, state, state->wm.focused_control_id);
        }
    }

    if (ui_wm_dispatch_key(&state->wm, c)) {
        sync_showcase_state(state);
        if (state->wm.focused_control_id == state->scrollbar_zoom_id) {
            update_status_text(state, "Zoom ajustado por teclado.");
        } else if (state->wm.focused_control_id == state->menu_actions_id) {
            update_status_text(state, "Menu widget atualizado por teclado.");
        } else if (state->wm.focused_control_id == state->dropdown_speed_id) {
            update_status_text(state, "Dropdown atualizado por teclado.");
        }
    }
    return 0;
}

int handle_mouse(const minidos_app_api_t* api, demo_state_t* state, const app_mouse_state_t* previous_mouse) {
    int out_window_id = 0;
    int out_control_id = 0;
    int out_hit_close = 0;
    int activated;
    int left_down;
    int left_pressed;
    int left_released;
    int chrome_consumed = 0;
    int menu_hit = START_MENU_ITEM_NONE;
    ui_rect_t start_rect;
    ui_rect_t menu_rect;
    ui_rect_t main_window_rect;
    int over_start;
    int over_menu;
    int desktop_hit;
    ui_wm_window_t* win;
    const ui_control_t* pressed_control;
    ui_rect_t pressed_bounds;

    if (!state || !previous_mouse) {
        return 0;
    }

    left_down = ui_mouse_left_down(&state->mouse);
    left_pressed = ui_mouse_left_pressed(previous_mouse, &state->mouse);
    left_released = ui_mouse_left_released(previous_mouse, &state->mouse);
    start_rect = start_button_rect(state);
    menu_rect = start_menu_rect(state);
    main_window_rect = current_window_rect(state);
    over_start = ui_rect_contains(&start_rect, state->mouse.x, state->mouse.y);
    over_menu = state->start_menu_open && ui_rect_contains(&menu_rect, state->mouse.x, state->mouse.y);

    if (state->start_menu_open) {
        menu_hit = start_menu_hit_test(state, state->mouse.x, state->mouse.y);
        state->start_menu_hot_item = menu_hit;
    } else {
        state->start_menu_hot_item = START_MENU_ITEM_NONE;
    }

    if (update_resize_hover(state)) {
        state->layout_version++;
    }

    if (left_pressed) {
        if (over_start) {
            state->start_button_pressed = 1;
            state->taskbar_pressed_window_id = 0;
            state->start_menu_pressed_item = START_MENU_ITEM_NONE;
            chrome_consumed = 1;
        } else {
            int taskbar_window_id = taskbar_button_hit_test(state, state->mouse.x, state->mouse.y);
            if (taskbar_window_id != 0) {
                state->taskbar_pressed_window_id = taskbar_window_id;
                state->start_button_pressed = 0;
                state->start_menu_pressed_item = START_MENU_ITEM_NONE;
                dismiss_start_menu(state);
                chrome_consumed = 1;
            }
        }
    }

    if (left_released && state->taskbar_pressed_window_id != 0) {
        int pressed_window_id = state->taskbar_pressed_window_id;

        state->taskbar_pressed_window_id = 0;
        chrome_consumed = 1;
        if (taskbar_button_hit_test(state, state->mouse.x, state->mouse.y) == pressed_window_id) {
            activate_taskbar_window(state, pressed_window_id);
            return 0;
        }
    }

    if (left_pressed && !chrome_consumed) {
        int top_window_id = ui_wm_top_window_at(&state->wm, state->mouse.x, state->mouse.y);
        ui_wm_window_t* top_win = ui_wm_find_window(&state->wm, top_window_id);
        int title_action = title_button_hit_action(top_win, state->mouse.x, state->mouse.y);
        int resize_edges = window_resize_hit_test(state, top_window_id, state->mouse.x, state->mouse.y);

        if (resize_edges != 0 && top_win) {
            ui_wm_bring_to_front(&state->wm, top_window_id);
            state->resizing = 1;
            state->resizing_window_id = top_window_id;
            state->resize_edges = resize_edges;
            state->resize_hover_window_id = top_window_id;
            state->resize_hover_edges = resize_edges;
            state->resize_start_mouse_x = state->mouse.x;
            state->resize_start_mouse_y = state->mouse.y;
            state->resize_start_bounds = top_win->window.bounds;
            state->drag_preview_bounds = top_win->window.bounds;
            state->layout_version++;
            dismiss_start_menu(state);
            chrome_consumed = 1;
        } else if (title_action != TITLE_BUTTON_NONE) {
            ui_wm_bring_to_front(&state->wm, top_window_id);
            state->title_button_pressed_window_id = top_window_id;
            state->title_button_pressed_action = title_action;
            state->layout_version++;
            dismiss_start_menu(state);
            chrome_consumed = 1;
        }
    }

    if (left_released && state->title_button_pressed_window_id != 0) {
        int pressed_window_id = state->title_button_pressed_window_id;
        int pressed_action = state->title_button_pressed_action;
        ui_wm_window_t* pressed_win = ui_wm_find_window(&state->wm, pressed_window_id);

        state->title_button_pressed_window_id = 0;
        state->title_button_pressed_action = TITLE_BUTTON_NONE;
        chrome_consumed = 1;
        if (title_button_hit_action(pressed_win, state->mouse.x, state->mouse.y) == pressed_action) {
            activate_title_button(state, pressed_window_id, pressed_action);
            return 0;
        }
    }

    if (left_pressed && !chrome_consumed) {
        if (over_start) {
            state->start_button_pressed = 1;
            state->start_menu_pressed_item = START_MENU_ITEM_NONE;
            chrome_consumed = 1;
        } else if (over_menu) {
            state->start_menu_pressed_item = menu_hit;
            state->start_button_pressed = 0;
            chrome_consumed = 1;
        } else if (state->start_menu_open) {
            close_start_menu(state, "Menu Iniciar fechado.");
        }
    }

    if (left_released) {
        if (state->start_button_pressed) {
            state->start_button_pressed = 0;
            if (over_start) {
                if (state->start_menu_open) {
                    close_start_menu(state, "Menu Iniciar fechado.");
                } else {
                    open_start_menu(state);
                }
                return 0;
            }
        }

        if (state->start_menu_pressed_item != START_MENU_ITEM_NONE) {
            int pressed_item = state->start_menu_pressed_item;
            state->start_menu_pressed_item = START_MENU_ITEM_NONE;
            chrome_consumed = 1;
            if (over_menu && pressed_item == menu_hit) {
                return handle_start_menu_action(state, pressed_item);
            }
        } else if (over_menu) {
            chrome_consumed = 1;
        }
    }

    if (chrome_consumed) {
        return 0;
    }

    if (left_pressed) {
        if (ui_wm_top_window_at(&state->wm, state->mouse.x, state->mouse.y) == 0) {
            desktop_hit = desktop_item_hit_test(state, state->mouse.x, state->mouse.y);
            if (desktop_hit >= 0) {
                unsigned int now = app_get_ticks(api);
                state->selected_desktop_item = desktop_hit;
                dismiss_start_menu(state);
                if (state->desktop_items[desktop_hit].is_dir
                    && state->last_desktop_click_index == desktop_hit
                    && (unsigned int)(now - state->last_desktop_click_ticks) <= DOUBLE_CLICK_TICKS) {
                    (void)open_desktop_folder(api, state, desktop_hit);
                    state->last_desktop_click_index = -1;
                    state->last_desktop_click_ticks = 0;
                } else {
                    state->last_desktop_click_index = desktop_hit;
                    state->last_desktop_click_ticks = now;
                }
                return 0;
            }
        }
        if (!main_window_is_visible(state)
            || !ui_rect_contains(&main_window_rect, state->mouse.x, state->mouse.y)) {
            state->selected_desktop_item = -1;
        }
    }

    {
        int pre_popup_open;
        int pre_pressed_any;
        int post_popup_open;
        int post_pressed_any;

        snapshot_wm_visual_state(state, &pre_popup_open, &pre_pressed_any);

        activated = ui_wm_dispatch_mouse(&state->wm,
            state->mouse.x,
            state->mouse.y,
            left_down,
            left_pressed,
            left_released,
            &out_window_id,
            &out_control_id,
            &out_hit_close);

        snapshot_wm_visual_state(state, &post_popup_open, &post_pressed_any);
        if (pre_popup_open != post_popup_open || pre_pressed_any != post_pressed_any) {
            state->layout_version++;
        }
    }

    if (out_control_id != 0 || activated) {
        state->layout_version++;
        sync_showcase_state(state);
    }

    if (left_pressed) {
        if (out_control_id == state->button_ok_id
            || out_control_id == state->button_cancel_id
            || out_control_id == state->scrollbar_zoom_id) {
            state->mouse_pressed_control_id = out_control_id;
        } else if (out_control_id == state->checkbox_sound_id
            || out_control_id == state->checkbox_grid_id
            || out_control_id == state->radio_theme_classic_id
            || out_control_id == state->radio_theme_cloud_id
            || out_control_id == state->dropdown_speed_id
            || out_control_id == state->menu_actions_id) {
            state->mouse_pressed_control_id = out_control_id;
        } else if (explorer_for_control(state, out_control_id)) {
            state->mouse_pressed_control_id = out_control_id;
        } else {
            state->mouse_pressed_control_id = 0;
        }
    }

    win = ui_wm_find_window(&state->wm, out_window_id);

    if (win && left_pressed
        && out_control_id == 0
        && !out_hit_close
        && ui_window_hit_title(&win->window, state->mouse.x, state->mouse.y)) {
        unsigned int now = app_get_ticks(api);
        int is_double = state->last_title_click_window_id == out_window_id
            && (unsigned int)(now - state->last_title_click_ticks) <= DOUBLE_CLICK_TICKS;

        if (is_double && win->window.has_maximize_button) {
            explorer_state_t* explorer = explorer_for_window(state, out_window_id);
            ui_wm_toggle_maximize_window(&state->wm, out_window_id, window_desktop_bounds(state));
            if (explorer) {
                explorer_relayout_window(state, out_window_id);
            }
            state->last_title_click_window_id = 0;
            state->last_title_click_ticks = 0;
            state->layout_version++;
            return 0;
        }

        state->last_title_click_window_id = out_window_id;
        state->last_title_click_ticks = now;

        if (!win->window.maximized) {
            state->dragging = 1;
            state->dragging_window_id = out_window_id;
            state->drag_offset_x = state->mouse.x - win->window.bounds.x;
            state->drag_offset_y = state->mouse.y - win->window.bounds.y;
            state->drag_preview_bounds = win->window.bounds;
            update_status_text(state, "A arrastar a janela.");
        }
    }

    win = ui_wm_find_window(&state->wm, state->dragging_window_id);
    if (state->dragging && left_down && win) {
        state->drag_preview_bounds.x = state->mouse.x - state->drag_offset_x;
        state->drag_preview_bounds.y = state->mouse.y - state->drag_offset_y;
        clamp_rect_to_desktop(state, &state->drag_preview_bounds);
        state->layout_version++;
    } else if (state->dragging && (!win || !win->visible || win->window.minimized)) {
        state->dragging = 0;
        state->dragging_window_id = 0;
        state->drag_preview_bounds = ui_rect_make(0, 0, 0, 0);
    }

    if (state->resizing && left_down) {
        win = ui_wm_find_window(&state->wm, state->resizing_window_id);
        if (win && win->visible && !win->window.minimized) {
            state->drag_preview_bounds = resize_preview_for_mouse(state);
            state->layout_version++;
        } else {
            state->resizing = 0;
            state->resizing_window_id = 0;
            state->resize_edges = 0;
            state->resize_start_bounds = ui_rect_make(0, 0, 0, 0);
            state->drag_preview_bounds = ui_rect_make(0, 0, 0, 0);
        }
    }

    {
        explorer_state_t* explorer = explorer_for_control(state, out_control_id);
        if (left_pressed && explorer && out_control_id == explorer->listview_id) {
        int hit_index = explorer_hit_test_item_in(state, explorer, state->mouse.x, state->mouse.y);
        if (hit_index >= 0) {
            unsigned int now = app_get_ticks(api);
            explorer_select_item_in(state, explorer, hit_index);
            if (explorer->last_click_index == hit_index
                && (unsigned int)(now - explorer->last_click_ticks) <= DOUBLE_CLICK_TICKS) {
                (void)explorer_open_selected_in(api, state, explorer);
                explorer->last_click_index = -1;
                explorer->last_click_ticks = 0;
            } else {
                explorer->last_click_index = hit_index;
                explorer->last_click_ticks = now;
            }
        }
        }
    }

    if (left_released) {
        if (state->dragging && win) {
            win->window.bounds = state->drag_preview_bounds;
            state->layout_version++;
        }
        if (state->resizing) {
            win = ui_wm_find_window(&state->wm, state->resizing_window_id);
            if (win) {
                win->window.bounds = state->drag_preview_bounds;
                win->window.restore_bounds = win->window.bounds;
                explorer_relayout_window(state, state->resizing_window_id);
                state->layout_version++;
            }
        }
        state->dragging = 0;
        state->resizing = 0;
        state->dragging_window_id = 0;
        state->resizing_window_id = 0;
        state->resize_edges = 0;
        state->resize_start_bounds = ui_rect_make(0, 0, 0, 0);
        state->drag_preview_bounds = ui_rect_make(0, 0, 0, 0);

        if (!activated
            && state->mouse_pressed_control_id != 0) {
            pressed_control = ui_wm_find_control_const(&state->wm, state->mouse_pressed_control_id);
            pressed_bounds = ui_wm_control_abs_bounds(&state->wm, pressed_control);
            if (pressed_control && ui_rect_contains(&pressed_bounds, state->mouse.x, state->mouse.y)) {
                activated = 1;
                out_control_id = state->mouse_pressed_control_id;
            }
        }

        state->mouse_pressed_control_id = 0;
    }

    if (out_hit_close && activated) {
        clear_window_interaction_state(state, out_window_id);
        state->drag_preview_bounds = ui_rect_make(0, 0, 0, 0);
        if (out_window_id == state->window_id) {
            close_main_window(state);
        } else if (explorer_for_window(state, out_window_id)) {
            explorer_close_window(state, out_window_id);
        }
        return 0;
    }

    if (activated && out_control_id != 0) {
        return handle_activated_control(api, state, out_control_id);
    }

    return 0;
}
