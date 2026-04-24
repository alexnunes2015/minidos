#include "win95_demo.h"

static void close_main_window(demo_state_t* state) {
    if (!state) {
        return;
    }

    dismiss_start_menu(state);
    ui_wm_close_window(&state->wm, state->window_id);
}

static int handle_activated_control(demo_state_t* state, int control_id) {
    if (control_id == state->button_ok_id) {
        close_main_window(state);
        return 0;
    }
    if (control_id == state->button_cancel_id) {
        close_main_window(state);
        return 0;
    }
    return 0;
}

static void cycle_focus(demo_state_t* state) {
    int next_id;

    if (!state) {
        return;
    }

    if (state->wm.focused_control_id == state->button_ok_id) {
        next_id = state->button_cancel_id;
    } else if (state->wm.focused_control_id == state->button_cancel_id) {
        next_id = state->input_id;
    } else {
        next_id = state->button_ok_id;
    }

    ui_wm_set_focus_control(&state->wm, next_id);
    update_status_text(state, "Foco movido por teclado.");
}

int handle_keyboard(demo_state_t* state, char c) {
    if (c == 'q' || c == 'Q' || c == KEY_ESC) {
        return 1;
    }

    if (c == KEY_TAB) {
        cycle_focus(state);
        return 0;
    }

    if (c == KEY_ENTER || c == KEY_SPACE) {
        if (state->wm.focused_control_id == state->button_ok_id || state->wm.focused_control_id == state->button_cancel_id) {
            return handle_activated_control(state, state->wm.focused_control_id);
        }
    }

    (void)ui_wm_dispatch_key(&state->wm, c);
    return 0;
}

int handle_mouse(demo_state_t* state, const app_mouse_state_t* previous_mouse) {
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

    if (left_pressed) {
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
        desktop_hit = desktop_item_hit_test(state, state->mouse.x, state->mouse.y);
        if (desktop_hit >= 0) {
            state->selected_desktop_item = desktop_hit;
            dismiss_start_menu(state);
            return 0;
        }
        if (!main_window_is_visible(state)
            || !ui_rect_contains(&main_window_rect, state->mouse.x, state->mouse.y)) {
            state->selected_desktop_item = -1;
        }
    }

    activated = ui_wm_dispatch_mouse(&state->wm,
        state->mouse.x,
        state->mouse.y,
        left_down,
        left_pressed,
        left_released,
        &out_window_id,
        &out_control_id,
        &out_hit_close);

    if (left_pressed) {
        if (out_control_id == state->button_ok_id || out_control_id == state->button_cancel_id) {
            state->mouse_pressed_control_id = out_control_id;
        } else {
            state->mouse_pressed_control_id = 0;
        }
    }

    win = ui_wm_find_window(&state->wm, state->window_id);

    if (win && left_pressed
        && out_window_id == state->window_id
        && out_control_id == 0
        && !out_hit_close
        && ui_window_hit_title(&win->window, state->mouse.x, state->mouse.y)) {
        state->dragging = 1;
        state->drag_offset_x = state->mouse.x - win->window.bounds.x;
        state->drag_offset_y = state->mouse.y - win->window.bounds.y;
        state->drag_preview_bounds = win->window.bounds;
        update_status_text(state, "A arrastar a janela.");
    }

    if (state->dragging && left_down && win) {
        state->drag_preview_bounds.x = state->mouse.x - state->drag_offset_x;
        state->drag_preview_bounds.y = state->mouse.y - state->drag_offset_y;
        clamp_rect_to_desktop(state, &state->drag_preview_bounds);
    }

    if (left_released) {
        if (state->dragging && win) {
            win->window.bounds = state->drag_preview_bounds;
        }
        state->dragging = 0;
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
        close_main_window(state);
        return 0;
    }

    if (activated && out_control_id != 0) {
        return handle_activated_control(state, out_control_id);
    }

    return 0;
}
