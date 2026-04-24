#include "win95_demo.h"

enum {
    CURSOR_REGION_DESKTOP = 0,
    CURSOR_REGION_WINDOW_BORDER = 1,
    CURSOR_REGION_WINDOW_TITLE = 2,
    CURSOR_REGION_WINDOW_CLIENT = 3,
};

ui_rect_t taskbar_rect(const demo_state_t* state) {
    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(0, state->sh - TASKBAR_H, state->sw, TASKBAR_H);
}

ui_rect_t start_button_rect(const demo_state_t* state) {
    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(4, state->sh - 24, START_BUTTON_W, START_BUTTON_H);
}

ui_rect_t taskbar_clock_rect(const demo_state_t* state) {
    ui_rect_t taskbar;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    taskbar = taskbar_rect(state);
    return ui_rect_make(state->sw - 88, taskbar.y + 4, 80, 20);
}

int main_window_is_visible(const demo_state_t* state) {
    const ui_wm_window_t* win;

    if (!state) {
        return 0;
    }

    win = ui_wm_find_window_const(&state->wm, state->window_id);
    return win && win->visible;
}

ui_rect_t cursor_rect_at(int x, int y) {
    return ui_rect_make(x - UI_CURSOR_HOTSPOT_X,
        y - UI_CURSOR_HOTSPOT_Y,
        UI_CURSOR_BITMAP_WIDTH,
        UI_CURSOR_BITMAP_HEIGHT);
}

ui_rect_t current_window_rect(const demo_state_t* state) {
    const ui_wm_window_t* win;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    win = ui_wm_find_window_const(&state->wm, state->window_id);
    if (!win || !win->visible) {
        return ui_rect_make(0, 0, 0, 0);
    }

    return win->window.bounds;
}

ui_rect_t current_title_bar_rect(const demo_state_t* state) {
    const ui_wm_window_t* win;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    win = ui_wm_find_window_const(&state->wm, state->window_id);
    if (!win || !win->visible) {
        return ui_rect_make(0, 0, 0, 0);
    }

    return ui_window_title_bar_rect(&win->window);
}

static int cursor_window_region(const demo_state_t* state, ui_rect_t cursor_rect) {
    ui_rect_t window_rect;
    ui_rect_t title_bar_rect;
    ui_rect_t client_rect;

    if (!state || ui_rect_is_empty(cursor_rect)) {
        return CURSOR_REGION_DESKTOP;
    }

    window_rect = current_window_rect(state);
    if (ui_rect_is_empty(window_rect)
        || ui_rect_is_empty(ui_rect_intersect(cursor_rect, window_rect))) {
        return CURSOR_REGION_DESKTOP;
    }

    title_bar_rect = current_title_bar_rect(state);
    if (!ui_rect_is_empty(title_bar_rect)
        && !ui_rect_is_empty(ui_rect_intersect(cursor_rect, title_bar_rect))) {
        return CURSOR_REGION_WINDOW_TITLE;
    }

    {
        const ui_wm_window_t* win = ui_wm_find_window_const(&state->wm, state->window_id);
        if (win) {
            client_rect = ui_window_client_rect(&win->window);
            if (!ui_rect_is_empty(client_rect)
                && !ui_rect_is_empty(ui_rect_intersect(cursor_rect, client_rect))) {
                return CURSOR_REGION_WINDOW_CLIENT;
            }
        }
    }

    return CURSOR_REGION_WINDOW_BORDER;
}

int cursor_touches_title_bar(const demo_state_t* state,
    ui_rect_t previous_cursor_rect,
    ui_rect_t current_cursor_rect) {
    ui_rect_t title_bar_rect;

    if (!state) {
        return 0;
    }

    title_bar_rect = current_title_bar_rect(state);
    if (ui_rect_is_empty(title_bar_rect)) {
        return 0;
    }

    return !ui_rect_is_empty(ui_rect_intersect(previous_cursor_rect, title_bar_rect))
        || !ui_rect_is_empty(ui_rect_intersect(current_cursor_rect, title_bar_rect));
}

int cursor_crosses_window_chrome(const demo_state_t* state,
    ui_rect_t previous_cursor_rect,
    ui_rect_t current_cursor_rect) {
    int previous_region;
    int current_region;

    if (!state) {
        return 0;
    }

    previous_region = cursor_window_region(state, previous_cursor_rect);
    current_region = cursor_window_region(state, current_cursor_rect);

    if (previous_region == current_region) {
        return 0;
    }

    return previous_region == CURSOR_REGION_WINDOW_TITLE
        || previous_region == CURSOR_REGION_WINDOW_BORDER
        || current_region == CURSOR_REGION_WINDOW_TITLE
        || current_region == CURSOR_REGION_WINDOW_BORDER;
}

ui_rect_t current_drag_preview_rect(const demo_state_t* state) {
    if (!state || !state->dragging) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return state->drag_preview_bounds;
}

void add_window_damage_for_cursor(ui_dirty_list_t* dirty,
    const demo_state_t* state,
    ui_rect_t previous_cursor_rect,
    ui_rect_t current_cursor_rect) {
    ui_rect_t window_rect;

    if (!dirty || !state) {
        return;
    }

    window_rect = current_window_rect(state);
    if (ui_rect_is_empty(window_rect)) {
        return;
    }

    if (!ui_rect_is_empty(ui_rect_intersect(previous_cursor_rect, window_rect))
        || !ui_rect_is_empty(ui_rect_intersect(current_cursor_rect, window_rect))) {
        ui_dirty_list_add(dirty, window_rect);
    }
}

ui_rect_t start_menu_rect(const demo_state_t* state) {
    int menu_y;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    menu_y = state->sh - TASKBAR_H - START_MENU_H;
    if (menu_y < 4) {
        menu_y = 4;
    }

    return ui_rect_make(2, menu_y, START_MENU_W, START_MENU_H);
}

ui_rect_t start_menu_item_rect(const demo_state_t* state, int item_index) {
    ui_rect_t menu;

    if (!state || item_index < 0 || item_index >= START_MENU_ITEM_COUNT) {
        return ui_rect_make(0, 0, 0, 0);
    }

    menu = start_menu_rect(state);
    return ui_rect_make(menu.x + START_MENU_STRIP_W + 10,
        menu.y + 12 + (item_index * 24),
        menu.w - START_MENU_STRIP_W - 20,
        22);
}

ui_rect_t start_menu_exit_rect(const demo_state_t* state) {
    ui_rect_t menu;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    menu = start_menu_rect(state);
    return ui_rect_make(menu.x + START_MENU_STRIP_W + 14,
        menu.y + menu.h - 40,
        menu.w - START_MENU_STRIP_W - 28,
        26);
}

void dismiss_start_menu(demo_state_t* state) {
    if (!state) {
        return;
    }

    state->start_menu_open = 0;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
}

void open_start_menu(demo_state_t* state) {
    if (!state) {
        return;
    }

    state->start_menu_open = 1;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
    update_status_text(state, "Menu Iniciar aberto.");
}

void close_start_menu(demo_state_t* state, const char* reason) {
    dismiss_start_menu(state);
    if (reason) {
        update_status_text(state, reason);
    }
}

int start_menu_hit_test(const demo_state_t* state, int x, int y) {
    int item_index;
    ui_rect_t rect;

    if (!state || !state->start_menu_open) {
        return START_MENU_ITEM_NONE;
    }

    for (item_index = 0; item_index < START_MENU_ITEM_COUNT; item_index++) {
        rect = start_menu_item_rect(state, item_index);
        if (ui_rect_contains(&rect, x, y)) {
            return item_index;
        }
    }

    rect = start_menu_exit_rect(state);
    if (ui_rect_contains(&rect, x, y)) {
        return START_MENU_ITEM_BACK_TO_DOS;
    }

    return START_MENU_ITEM_NONE;
}

int handle_start_menu_action(demo_state_t* state, int action_id) {
    if (!state) {
        return 0;
    }

    if (action_id == START_MENU_ITEM_PROGRAMAS) {
        close_start_menu(state, "Programas destacados na shell grafica.");
        return 0;
    }
    if (action_id == START_MENU_ITEM_DOCUMENTOS) {
        close_start_menu(state, "Documentos prontos para navegar.");
        return 0;
    }
    if (action_id == START_MENU_ITEM_DEFINICOES) {
        close_start_menu(state, "Definicoes em estilo classico.");
        return 0;
    }
    if (action_id == START_MENU_ITEM_AJUDA) {
        close_start_menu(state, "Ajuda: arrasta a janela e usa o Iniciar.");
        return 0;
    }
    if (action_id == START_MENU_ITEM_BACK_TO_DOS) {
        dismiss_start_menu(state);
        return 1;
    }

    return 0;
}

void clamp_rect_to_desktop(const demo_state_t* state, ui_rect_t* rect) {
    if (!state || !rect) {
        return;
    }

    if (rect->x < 0) {
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->y = 0;
    }
    if (rect->x + rect->w > state->sw) {
        rect->x = state->sw - rect->w;
    }
    if (rect->y + rect->h > state->sh - TASKBAR_H) {
        rect->y = (state->sh - TASKBAR_H) - rect->h;
    }
    if (rect->x < 0) {
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->y = 0;
    }
}
