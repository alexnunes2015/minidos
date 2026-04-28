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
    return ui_rect_make(state->sw - (TASKBAR_TRAY_W + 8), taskbar.y + 4, TASKBAR_TRAY_W, 20);
}

ui_rect_t window_desktop_bounds(const demo_state_t* state) {
    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(0, 0, state->sw, state->sh - TASKBAR_H);
}

static int taskbar_window_count(const demo_state_t* state) {
    int i;
    int count = 0;

    if (!state) {
        return 0;
    }

    for (i = 0; i < state->wm.window_count; i++) {
        if (state->wm.windows[i].id != 0
            && state->wm.windows[i].visible
            && state->wm.windows[i].window.has_minimize_button) {
            count++;
        }
    }

    return count;
}

ui_rect_t taskbar_button_rect(const demo_state_t* state, int window_id) {
    ui_rect_t taskbar;
    ui_rect_t start_rect;
    ui_rect_t clock_rect;
    int i;
    int index = 0;
    int count;
    int available_w;
    int button_w;

    if (!state || window_id == 0) {
        return ui_rect_make(0, 0, 0, 0);
    }

    count = taskbar_window_count(state);
    if (count <= 0) {
        return ui_rect_make(0, 0, 0, 0);
    }

    for (i = 0; i < state->wm.window_count; i++) {
        const ui_wm_window_t* win = &state->wm.windows[i];

        if (win->id == 0 || !win->visible || !win->window.has_minimize_button) {
            continue;
        }
        if (win->id == window_id) {
            break;
        }
        index++;
    }
    if (i >= state->wm.window_count) {
        return ui_rect_make(0, 0, 0, 0);
    }

    taskbar = taskbar_rect(state);
    start_rect = start_button_rect(state);
    clock_rect = taskbar_clock_rect(state);
    available_w = clock_rect.x - (start_rect.x + start_rect.w + 10 + QUICKLAUNCH_W) - 8;
    if (available_w < TASK_BUTTON_MIN_W) {
        available_w = TASK_BUTTON_MIN_W;
    }
    button_w = (available_w - ((count - 1) * 4)) / count;
    if (button_w > TASK_BUTTON_MAX_W) {
        button_w = TASK_BUTTON_MAX_W;
    }
    if (button_w < TASK_BUTTON_MIN_W) {
        button_w = TASK_BUTTON_MIN_W;
    }

    return ui_rect_make(start_rect.x + start_rect.w + 6 + QUICKLAUNCH_W + (index * (button_w + 4)),
        taskbar.y + 4,
        button_w,
        20);
}

int taskbar_button_hit_test(const demo_state_t* state, int x, int y) {
    int i;

    if (!state) {
        return 0;
    }

    for (i = 0; i < state->wm.window_count; i++) {
        const ui_wm_window_t* win = &state->wm.windows[i];
        ui_rect_t rect;

        if (win->id == 0 || !win->visible || !win->window.has_minimize_button) {
            continue;
        }
        rect = taskbar_button_rect(state, win->id);
        if (ui_rect_contains(&rect, x, y)) {
            return win->id;
        }
    }

    return 0;
}

int main_window_is_visible(const demo_state_t* state) {
    const ui_wm_window_t* win;

    if (!state) {
        return 0;
    }

    win = ui_wm_find_window_const(&state->wm, state->window_id);
    return win && win->visible && !win->window.minimized;
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
    if (!win || !win->visible || win->window.minimized) {
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
    if (!win || !win->visible || win->window.minimized) {
        return ui_rect_make(0, 0, 0, 0);
    }

    return ui_window_title_bar_rect(&win->window);
}

static int cursor_window_region(const demo_state_t* state, ui_rect_t cursor_rect) {
    int i;

    if (!state || ui_rect_is_empty(cursor_rect)) {
        return CURSOR_REGION_DESKTOP;
    }

    for (i = 0; i < state->wm.window_count; i++) {
        const ui_wm_window_t* win = &state->wm.windows[i];
        ui_rect_t title_bar_rect;
        ui_rect_t client_rect;

        if ((!win->visible || win->window.minimized)
            || ui_rect_is_empty(ui_rect_intersect(cursor_rect, win->window.bounds))) {
            continue;
        }

        if (window_resize_hit_test(state, win->id, cursor_rect.x, cursor_rect.y)
            || window_resize_hit_test(state, win->id, cursor_rect.x + cursor_rect.w - 1, cursor_rect.y)
            || window_resize_hit_test(state, win->id, cursor_rect.x, cursor_rect.y + cursor_rect.h - 1)
            || window_resize_hit_test(state, win->id, cursor_rect.x + cursor_rect.w - 1, cursor_rect.y + cursor_rect.h - 1)) {
            return CURSOR_REGION_WINDOW_BORDER;
        }

        title_bar_rect = ui_window_title_bar_rect(&win->window);
        if (!ui_rect_is_empty(title_bar_rect)
            && !ui_rect_is_empty(ui_rect_intersect(cursor_rect, title_bar_rect))) {
            return CURSOR_REGION_WINDOW_TITLE;
        }

        client_rect = ui_window_client_rect(&win->window);
        if (!ui_rect_is_empty(client_rect)
            && !ui_rect_is_empty(ui_rect_intersect(cursor_rect, client_rect))) {
            return CURSOR_REGION_WINDOW_CLIENT;
        }

        return CURSOR_REGION_WINDOW_BORDER;
    }

    return CURSOR_REGION_DESKTOP;
}

int cursor_touches_title_bar(const demo_state_t* state,
    ui_rect_t previous_cursor_rect,
    ui_rect_t current_cursor_rect) {
    ui_rect_t title_bar_rect;
    int i;

    if (!state) {
        return 0;
    }

    for (i = 0; i < state->wm.window_count; i++) {
        const ui_wm_window_t* win = &state->wm.windows[i];

        if (!win->visible || win->window.minimized) {
            continue;
        }

        title_bar_rect = ui_window_title_bar_rect(&win->window);
        if (!ui_rect_is_empty(ui_rect_intersect(previous_cursor_rect, title_bar_rect))
            || !ui_rect_is_empty(ui_rect_intersect(current_cursor_rect, title_bar_rect))) {
            return 1;
        }
    }

    return 0;
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
    if (!state || (!state->dragging && !state->resizing)) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return state->drag_preview_bounds;
}

int window_resize_hit_test(const demo_state_t* state, int window_id, int x, int y) {
    const ui_wm_window_t* win;
    ui_rect_t bounds;
    int edges = 0;

    if (!state || window_id == 0) {
        return 0;
    }

    win = ui_wm_find_window_const(&state->wm, window_id);
    if (!win || !win->visible || win->window.minimized || win->window.maximized
        || !win->window.has_maximize_button) {
        return 0;
    }

    bounds = win->window.bounds;
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.w || y >= bounds.y + bounds.h) {
        return 0;
    }
    if (ui_window_hit_close(&win->window, x, y)
        || ui_window_hit_minimize(&win->window, x, y)
        || ui_window_hit_maximize(&win->window, x, y)) {
        return 0;
    }

    if (x < bounds.x + WINDOW_RESIZE_MARGIN) {
        edges |= RESIZE_EDGE_LEFT;
    }
    if (x >= bounds.x + bounds.w - WINDOW_RESIZE_MARGIN) {
        edges |= RESIZE_EDGE_RIGHT;
    }
    if (y < bounds.y + WINDOW_RESIZE_MARGIN) {
        edges |= RESIZE_EDGE_TOP;
    }
    if (y >= bounds.y + bounds.h - WINDOW_RESIZE_MARGIN) {
        edges |= RESIZE_EDGE_BOTTOM;
    }

    return edges;
}

void add_window_damage_for_cursor(ui_dirty_list_t* dirty,
    const demo_state_t* state,
    ui_rect_t previous_cursor_rect,
    ui_rect_t current_cursor_rect) {
    ui_rect_t window_rect;
    int i;

    if (!dirty || !state) {
        return;
    }

    for (i = 0; i < state->wm.window_count; i++) {
        const ui_wm_window_t* win = &state->wm.windows[i];

        if (!win->visible || win->window.minimized) {
            continue;
        }

        window_rect = win->window.bounds;
        if (!ui_rect_is_empty(ui_rect_intersect(previous_cursor_rect, window_rect))
            || !ui_rect_is_empty(ui_rect_intersect(current_cursor_rect, window_rect))) {
            ui_dirty_list_add(dirty, window_rect);
        }
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
