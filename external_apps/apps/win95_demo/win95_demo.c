#define MINIDOS_UI_IMPLEMENTATION
#include "win95_demo.h"

static const char* const g_showcase_speed_items[] = {
    "Lento",
    "Normal",
    "Turbo",
};

static const char* const g_showcase_menu_items[] = {
    "Novo ficheiro",
    "Copiar",
    "Colar",
    "Propriedades",
};

void str_copy(char* dst, const char* src, int max_len) {
    int i = 0;
    while (i < (max_len - 1) && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int str_equal(const char* a, const char* b) {
    int i = 0;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

int rect_equal(ui_rect_t a, ui_rect_t b) {
    return a.x == b.x
        && a.y == b.y
        && a.w == b.w
        && a.h == b.h;
}

void enter_resource_home(const minidos_app_api_t* api) {
    if (!api) {
        return;
    }

    /* Anchor resource lookup to A:\AIOS regardless of the shell's cwd. */
    if (!app_chdir(api, "\\")) {
        return;
    }
    if (!app_chdir(api, RESOURCE_HOME_DIR_1)) {
        return;
    }
}

void update_mouse_label_text(demo_state_t* state) {
    ui_control_t* control;

    if (!state) {
        return;
    }

    str_copy(state->mouse_text,
        state->mouse.present ? "Mouse PS/2 ativo" : "Mouse PS/2 nao detetado",
        (int)sizeof(state->mouse_text));

    control = ui_wm_find_control(&state->wm, state->label_mouse_id);
    if (control) {
        control->text = state->mouse_text;
    }
}

void update_status_text(demo_state_t* state, const char* text) {
    ui_control_t* control;

    if (!state) {
        return;
    }

    str_copy(state->status_text, text ? text : "", (int)sizeof(state->status_text));
    control = ui_wm_find_control(&state->wm, state->label_status_id);
    if (control) {
        control->text = state->status_text;
    }
}

void update_value_label_text(demo_state_t* state) {
    ui_control_t* control;
    ui_control_t* dropdown;
    ui_control_t* menu;
    ui_control_t* scrollbar;
    ui_control_t* sound;
    ui_control_t* grid;
    ui_control_t* classic;
    const char* speed = "-";
    const char* action = "-";
    const char* theme = "Cloud";
    const char* sound_state = "off";
    const char* grid_state = "off";
    char value[96];
    int i = 0;
    int zoom = 0;

    if (!state) {
        return;
    }

    dropdown = ui_wm_find_control(&state->wm, state->dropdown_speed_id);
    menu = ui_wm_find_control(&state->wm, state->menu_actions_id);
    scrollbar = ui_wm_find_control(&state->wm, state->scrollbar_zoom_id);
    sound = ui_wm_find_control(&state->wm, state->checkbox_sound_id);
    grid = ui_wm_find_control(&state->wm, state->checkbox_grid_id);
    classic = ui_wm_find_control(&state->wm, state->radio_theme_classic_id);

    if (dropdown && dropdown->items && dropdown->selected_index >= 0 && dropdown->selected_index < dropdown->item_count) {
        speed = dropdown->items[dropdown->selected_index];
    }
    if (menu && menu->items && menu->selected_index >= 0 && menu->selected_index < menu->item_count) {
        action = menu->items[menu->selected_index];
    }
    if (scrollbar) {
        zoom = scrollbar->value;
    }
    if (sound && sound->checked) {
        sound_state = "on";
    }
    if (grid && grid->checked) {
        grid_state = "on";
    }
    if (classic && classic->checked) {
        theme = "Classic";
    }

    while (i < (int)sizeof(value) - 1 && "Tema:"[i] != '\0') {
        value[i] = "Tema:"[i];
        i++;
    }
    value[i++] = ' ';
    str_copy(&value[i], theme, (int)sizeof(value) - i);
    i = ui_strlen(value);
    str_copy(&value[i], "  Zoom:", (int)sizeof(value) - i);
    i = ui_strlen(value);
    value[i++] = ' ';
    value[i++] = (char)('0' + ((zoom / 10) % 10));
    value[i++] = (char)('0' + (zoom % 10));
    value[i++] = '%';
    value[i++] = ' ';
    value[i++] = ' ';
    str_copy(&value[i], "Vel:", (int)sizeof(value) - i);
    i = ui_strlen(value);
    value[i++] = ' ';
    str_copy(&value[i], speed, (int)sizeof(value) - i);
    i = ui_strlen(value);
    str_copy(&value[i], "  Menu:", (int)sizeof(value) - i);
    i = ui_strlen(value);
    value[i++] = ' ';
    str_copy(&value[i], action, (int)sizeof(value) - i);
    i = ui_strlen(value);
    str_copy(&value[i], "  Som:", (int)sizeof(value) - i);
    i = ui_strlen(value);
    value[i++] = ' ';
    str_copy(&value[i], sound_state, (int)sizeof(value) - i);
    i = ui_strlen(value);
    str_copy(&value[i], "  Grelha:", (int)sizeof(value) - i);
    i = ui_strlen(value);
    value[i++] = ' ';
    str_copy(&value[i], grid_state, (int)sizeof(value) - i);

    control = ui_wm_find_control(&state->wm, state->label_value_id);
    if (control) {
        str_copy(state->value_text, value, (int)sizeof(state->value_text));
        control->text = state->value_text;
    }
}

void sync_showcase_state(demo_state_t* state) {
    update_mouse_label_text(state);
    update_value_label_text(state);
}

void create_showcase_window(demo_state_t* state) {
    ui_rect_t bounds;

    if (!state || state->window_id != 0) {
        return;
    }

    bounds = ui_rect_make((state->sw - 508) / 2, 52, 508, 320);
    if (bounds.x < 8) {
        bounds.x = 8;
    }
    if (bounds.y < 8) {
        bounds.y = 8;
    }

    state->window_id = ui_wm_create_window_ex(&state->wm, bounds, "Componentes UI", 1, 1, 1);
    state->label_mouse_id = ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(18, 56, 472, 16), "Mouse");
    state->label_status_id = ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(18, 286, 472, 16), state->status_text);
    state->label_value_id = ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(18, 266, 472, 16), "");
    state->input_id = ui_wm_add_textinput(&state->wm, state->window_id,
        ui_rect_make(18, 76, 204, 24), state->input_text, (int)sizeof(state->input_text));
    state->dropdown_speed_id = ui_wm_add_dropdown(&state->wm, state->window_id,
        ui_rect_make(246, 76, 156, 24), g_showcase_speed_items,
        (int)(sizeof(g_showcase_speed_items) / sizeof(g_showcase_speed_items[0])), 1);
    state->checkbox_sound_id = ui_wm_add_checkbox(&state->wm, state->window_id,
        ui_rect_make(18, 132, 176, 20), "Ativar sons", 1);
    state->checkbox_grid_id = ui_wm_add_checkbox(&state->wm, state->window_id,
        ui_rect_make(18, 156, 176, 20), "Mostrar grelha", 0);
    state->radio_theme_classic_id = ui_wm_add_radio(&state->wm, state->window_id,
        ui_rect_make(246, 132, 156, 20), "Tema Classic", 1, 1);
    state->radio_theme_cloud_id = ui_wm_add_radio(&state->wm, state->window_id,
        ui_rect_make(246, 156, 156, 20), "Tema Cloud", 1, 0);
    state->menu_actions_id = ui_wm_add_menu(&state->wm, state->window_id,
        ui_rect_make(18, 180, 176, 76), g_showcase_menu_items,
        (int)(sizeof(g_showcase_menu_items) / sizeof(g_showcase_menu_items[0])), 0);
    state->scrollbar_zoom_id = ui_wm_add_scrollbar(&state->wm, state->window_id,
        ui_rect_make(446, 76, 22, 180), 0, 20, 4, 8);
    state->button_ok_id = ui_wm_add_button(&state->wm, state->window_id,
        ui_rect_make(246, 180, 90, 24), "Aplicar");
    state->button_cancel_id = ui_wm_add_button(&state->wm, state->window_id,
        ui_rect_make(344, 180, 90, 24), "Fechar");
    ui_wm_set_focus_control(&state->wm, state->input_id);
    sync_showcase_state(state);
}

void append_two_digits(char* out, unsigned int value) {
    out[0] = (char)('0' + ((value / 10u) % 10u));
    out[1] = (char)('0' + (value % 10u));
}

void update_clock_text(demo_state_t* state, const minidos_app_api_t* api) {
    app_time_t time;

    if (!state) {
        return;
    }

    if (!api || !app_get_time(api, &time)) {
        str_copy(state->clock_text, "--:--", (int)sizeof(state->clock_text));
        return;
    }

    append_two_digits(&state->clock_text[0], time.hours);
    state->clock_text[2] = ':';
    append_two_digits(&state->clock_text[3], time.minutes);
    state->clock_text[5] = '\0';
}

static ui_rect_t window_rect_by_id(const demo_state_t* state, int window_id) {
    const ui_wm_window_t* win;

    if (!state || window_id == 0) {
        return ui_rect_make(0, 0, 0, 0);
    }

    win = ui_wm_find_window_const(&state->wm, window_id);
    if (!win || !win->visible || win->window.minimized) {
        return ui_rect_make(0, 0, 0, 0);
    }

    return win->window.bounds;
}

static void copy_window_title_by_id(const demo_state_t* state, int window_id, char* out, int out_len) {
    const ui_wm_window_t* win;

    if (!out || out_len <= 0) {
        return;
    }
    out[0] = '\0';
    if (!state || window_id == 0) {
        return;
    }

    win = ui_wm_find_window_const(&state->wm, window_id);
    if (!win || !win->window.title) {
        return;
    }

    str_copy(out, win->window.title, out_len);
}

/* Bounds of the focused textinput, or an empty rect when none is focused.
 * Used to damage the caret area when the blink phase flips. */
static ui_rect_t focused_textinput_rect(const demo_state_t* state) {
    const ui_control_t* control;
    const ui_wm_window_t* win;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    control = ui_wm_find_control_const(&state->wm, state->wm.focused_control_id);
    if (!control || control->type != UI_CONTROL_TEXTINPUT || !control->visible) {
        return ui_rect_make(0, 0, 0, 0);
    }

    win = ui_wm_find_window_const(&state->wm, control->window_id);
    if (!win || !win->visible || win->window.minimized) {
        return ui_rect_make(0, 0, 0, 0);
    }

    return ui_wm_control_visible_bounds(&state->wm, control);
}

static void add_desktop_item_dirty(ui_dirty_list_t* dirty, const demo_state_t* state, int item_index) {
    if (!dirty || !state || item_index < 0 || item_index >= state->desktop_item_count) {
        return;
    }

    ui_dirty_list_add(dirty, state->desktop_items[item_index].bounds);
}

void enter_desktop_home(const minidos_app_api_t* api) {
    if (!api) {
        return;
    }

    if (!app_chdir(api, "\\")) {
        return;
    }
    if (!app_chdir(api, "USER")) {
        return;
    }
    if (!app_chdir(api, "ADM")) {
        return;
    }
    (void)app_chdir(api, "Desktop");
}

void draw_text_transparent_clipped(const minidos_app_api_t* api, int x, int y, const char* text,
    unsigned int fg, ui_rect_t clip) {
    int i;
    int row;
    int col;

    if (!api || !text) {
        return;
    }

    for (i = 0; text[i] != '\0'; i++) {
        ui_rect_t char_rect = ui_rect_make(x + (i * UI_CHAR_W), y, UI_CHAR_W, UI_CHAR_H);
        const unsigned char* glyph;

        if (text[i] < 32 || text[i] > 126) {
            continue;
        }
        if (ui_rect_is_empty(ui_rect_intersect(char_rect, clip))) {
            continue;
        }

        glyph = ui_font_8x8[text[i] - 32];
        for (row = 0; row < UI_CHAR_H; row++) {
            unsigned char bits = glyph[row];
            for (col = 0; col < UI_CHAR_W; col++) {
                if (!(bits & (0x80u >> col))) {
                    continue;
                }

                {
                    app_gfx_rect_t pixel_rect;
                    pixel_rect.x = char_rect.x + col;
                    pixel_rect.y = char_rect.y + row;
                    pixel_rect.w = 1;
                    pixel_rect.h = 1;
                    pixel_rect.color = fg;
                    if (!ui_rect_contains(&clip, pixel_rect.x, pixel_rect.y)) {
                        continue;
                    }
                    (void)app_gfx_rect(api, &pixel_rect);
                }
            }
        }
    }
}

void init_demo(demo_state_t* state, const minidos_app_api_t* api) {
    state->sw = 640;
    state->sh = 480;
    state->window_id = 0;
    state->label_mouse_id = 0;
    state->label_status_id = 0;
    state->label_value_id = 0;
    state->button_ok_id = 0;
    state->button_cancel_id = 0;
    state->input_id = 0;
    state->checkbox_sound_id = 0;
    state->checkbox_grid_id = 0;
    state->radio_theme_classic_id = 0;
    state->radio_theme_cloud_id = 0;
    state->dropdown_speed_id = 0;
    state->menu_actions_id = 0;
    state->scrollbar_zoom_id = 0;
    state->dragging = 0;
    state->resizing = 0;
    state->dragging_window_id = 0;
    state->resizing_window_id = 0;
    state->resize_edges = 0;
    state->resize_hover_window_id = 0;
    state->resize_hover_edges = 0;
    state->mouse_pressed_control_id = 0;
    state->title_button_pressed_window_id = 0;
    state->title_button_pressed_action = 0;
    state->last_title_click_window_id = 0;
    state->last_title_click_ticks = 0;
    state->taskbar_pressed_window_id = 0;
    state->drag_offset_x = 0;
    state->drag_offset_y = 0;
    state->resize_start_mouse_x = 0;
    state->resize_start_mouse_y = 0;
    state->resize_start_bounds = ui_rect_make(0, 0, 0, 0);
    state->drag_preview_bounds = ui_rect_make(0, 0, 0, 0);
    state->layout_version = 0;
    state->caret_blink_phase = 0;
    state->start_menu_open = 0;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
    state->selected_desktop_item = -1;
    state->last_desktop_click_index = -1;
    state->last_desktop_click_ticks = 0;
    state->desktop_item_count = 0;
    explorer_init_all(state);
    (void)ui_screen_size(api, &state->sw, &state->sh);

    ui_wm_init(&state->wm, ui_theme_classic());

    /* Preload wallpaper surface once; full and partial redraws then scale in-kernel. */
    if (ui_wallpaper_surface_load(api, WALLPAPER_BMP_PATH)) {
        state->wm.theme.desktop_bg_bitmap = WALLPAPER_BMP_PATH;
    }

    str_copy(state->input_text, "MiniDOS 95", (int)sizeof(state->input_text));
    load_desktop_items(api, state);
    create_showcase_window(state);

    (void)app_mouse_state(api, &state->mouse);
    update_clock_text(state, api);
    update_status_text(state,
        state->mouse.present
            ? "Janela de teste aberta. Experimenta os componentes."
            : "Sem mouse. Teclado continua funcional.");
    sync_showcase_state(state);
}

int app_main(const minidos_app_api_t* api) {
    demo_state_t state;
    char previous_clock_text[CLOCK_TEXT_LEN];

    if (!api) {
        return 1;
    }

    enter_resource_home(api);
    init_demo(&state, api);
    render(api, &state);

    while (1) {
        /* Wake often enough to animate the caret while a textinput is focused. */
        int wait_ms = ui_rect_is_empty(focused_textinput_rect(&state))
            ? CLOCK_REFRESH_MS : CARET_BLINK_POLL_MS;
        int event_mask = app_wait_event_timeout(api, state.mouse.seq, wait_ms);
        int need_full_render = 0;
        int need_partial_render = 0;
        ui_dirty_list_t partial_dirty;

        ui_dirty_list_init(&partial_dirty);

        if (event_mask & APP_EVENT_MOUSE) {
            app_mouse_state_t previous_mouse = state.mouse;
            ui_rect_t previous_window_rect = current_window_rect(&state);
            ui_rect_t previous_drag_rect = current_drag_preview_rect(&state);
            ui_rect_t previous_explorer_rect = explorer_window_rect(&state);
            int previous_active_window_id = state.wm.active_window_id;
            ui_rect_t previous_active_window_rect = window_rect_by_id(&state, previous_active_window_id);
            char previous_active_title[EXPLORER_TITLE_LEN];
            int previous_layout_version = state.layout_version;
            int previous_start_menu_open = state.start_menu_open;
            int previous_start_button_pressed = state.start_button_pressed;
            int previous_start_menu_pressed_item = state.start_menu_pressed_item;
            int previous_start_menu_hot_item = state.start_menu_hot_item;
            int previous_selected_desktop_item = state.selected_desktop_item;

            copy_window_title_by_id(&state, previous_active_window_id,
                previous_active_title, (int)sizeof(previous_active_title));
            (void)app_mouse_state(api, &state.mouse);
            update_mouse_label_text(&state);
            if (handle_mouse(api, &state, &previous_mouse)) {
                app_gfx_clear(api, 0x000000u);
                ui_present(api);
                return 0;
            }
            if (state.mouse.x != previous_mouse.x || state.mouse.y != previous_mouse.y) {
                ui_rect_t previous_cursor_rect = cursor_rect_at(previous_mouse.x, previous_mouse.y);
                ui_rect_t current_cursor_rect = cursor_rect_at(state.mouse.x, state.mouse.y);
                ui_rect_t next_window_rect = current_window_rect(&state);
                ui_rect_t next_drag_rect = current_drag_preview_rect(&state);
                int motion_changed_layout = !rect_equal(previous_window_rect, next_window_rect)
                    || !rect_equal(previous_drag_rect, next_drag_rect)
                    || previous_start_menu_open != state.start_menu_open
                    || previous_start_button_pressed != state.start_button_pressed
                    || previous_start_menu_pressed_item != state.start_menu_pressed_item
                    || previous_start_menu_hot_item != state.start_menu_hot_item;

                if (!motion_changed_layout
                    && !cursor_touches_title_bar(&state, previous_cursor_rect, current_cursor_rect)
                    && !cursor_crosses_window_chrome(&state, previous_cursor_rect, current_cursor_rect)) {
                    render_partial_motion(api, &state, &previous_mouse);
                    continue;
                }
                if (!rect_equal(previous_drag_rect, next_drag_rect)
                    && rect_equal(previous_window_rect, next_window_rect)
                    && previous_start_menu_open == state.start_menu_open
                    && previous_start_button_pressed == state.start_button_pressed
                    && previous_start_menu_pressed_item == state.start_menu_pressed_item
                    && previous_start_menu_hot_item == state.start_menu_hot_item) {
                    render_partial_drag(api, &state, previous_drag_rect, &previous_mouse);
                    continue;
                }

                need_full_render = 1;
            }

            {
                ui_rect_t current_window_rect_value = current_window_rect(&state);
                ui_rect_t current_explorer_rect = explorer_window_rect(&state);
                ui_rect_t current_drag_rect = current_drag_preview_rect(&state);
                int current_active_window_id = state.wm.active_window_id;
                ui_rect_t current_active_window_rect = window_rect_by_id(&state, current_active_window_id);
                char current_active_title[EXPLORER_TITLE_LEN];
                int menu_changed = previous_start_menu_open != state.start_menu_open
                    || previous_start_button_pressed != state.start_button_pressed
                    || previous_start_menu_pressed_item != state.start_menu_pressed_item
                    || previous_start_menu_hot_item != state.start_menu_hot_item;
                int desktop_selection_changed = previous_selected_desktop_item != state.selected_desktop_item;
                int layout_changed = previous_layout_version != state.layout_version;
                int main_window_changed = !rect_equal(previous_window_rect, current_window_rect_value);
                int explorer_window_changed = !rect_equal(previous_explorer_rect, current_explorer_rect);
                int active_window_changed = previous_active_window_id != current_active_window_id
                    || !rect_equal(previous_active_window_rect, current_active_window_rect);
                int title_changed;

                copy_window_title_by_id(&state, current_active_window_id,
                    current_active_title, (int)sizeof(current_active_title));
                title_changed = !str_equal(previous_active_title, current_active_title);

                if (menu_changed || !rect_equal(previous_drag_rect, current_drag_rect)) {
                    need_full_render = 1;
                }

                if (desktop_selection_changed) {
                    add_desktop_item_dirty(&partial_dirty, &state, previous_selected_desktop_item);
                    add_desktop_item_dirty(&partial_dirty, &state, state.selected_desktop_item);
                    need_partial_render = 1;
                }

                if (layout_changed || main_window_changed || explorer_window_changed
                    || active_window_changed || title_changed) {
                    ui_dirty_list_add(&partial_dirty, previous_window_rect);
                    ui_dirty_list_add(&partial_dirty, current_window_rect_value);
                    ui_dirty_list_add(&partial_dirty, previous_explorer_rect);
                    ui_dirty_list_add(&partial_dirty, current_explorer_rect);
                    ui_dirty_list_add(&partial_dirty, previous_active_window_rect);
                    ui_dirty_list_add(&partial_dirty, current_active_window_rect);
                    need_partial_render = 1;

                    if (main_window_changed || explorer_window_changed) {
                        ui_dirty_list_add(&partial_dirty, taskbar_rect(&state));
                    } else {
                        if (active_window_changed) {
                            ui_dirty_list_add(&partial_dirty, taskbar_button_rect(&state, previous_active_window_id));
                            ui_dirty_list_add(&partial_dirty, taskbar_button_rect(&state, current_active_window_id));
                        }
                        if (title_changed) {
                            ui_dirty_list_add(&partial_dirty, taskbar_button_rect(&state, current_active_window_id));
                        }
                    }
                }
            }

            if (need_partial_render && partial_dirty.count <= 0) {
                need_full_render = 1;
            }
        }

        if (event_mask & APP_EVENT_KEY) {
            char c = 0;
            while (app_get_char_nonblock(api, &c)) {
                if (handle_keyboard(api, &state, c)) {
                    app_gfx_clear(api, 0x000000u);
                    ui_present(api);
                    return 0;
                }
                need_full_render = 1;
            }
        }

        if (event_mask & (APP_EVENT_MOUSE | APP_EVENT_KEY | APP_EVENT_TIMER)) {
            str_copy(previous_clock_text, state.clock_text, (int)sizeof(previous_clock_text));
            update_clock_text(&state, api);
            if (!str_equal(previous_clock_text, state.clock_text)) {
                if (need_partial_render) {
                    ui_dirty_list_add(&partial_dirty, taskbar_clock_rect(&state));
                } else if (!need_full_render) {
                    render_clock_update(api, &state);
                } else {
                    need_full_render = 1;
                }
            }
        }

        /* Caret blink: the text box draws the caret from the current tick
         * phase, but nothing damages its region when the phase flips while
         * the UI is idle. Track the phase and repaint the focused textinput
         * whenever it changes. */
        {
            unsigned int caret_phase = (app_get_ticks(api) / 20u) & 1u;

            if (caret_phase != state.caret_blink_phase) {
                ui_rect_t caret_rect = focused_textinput_rect(&state);

                state.caret_blink_phase = caret_phase;
                if (!need_full_render && !ui_rect_is_empty(caret_rect)) {
                    ui_dirty_list_add(&partial_dirty, caret_rect);
                    need_partial_render = 1;
                }
            }
        }

        if (need_full_render) {
            render(api, &state);
        } else if (need_partial_render) {
            render_dirty_regions(api, &state, &partial_dirty);
        }
    }
}
