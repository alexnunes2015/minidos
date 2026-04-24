#define MINIDOS_UI_IMPLEMENTATION
#include "win95_demo.h"

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
        str_copy(state->clock_text, "--:--:--", (int)sizeof(state->clock_text));
        return;
    }

    append_two_digits(&state->clock_text[0], time.hours);
    state->clock_text[2] = ':';
    append_two_digits(&state->clock_text[3], time.minutes);
    state->clock_text[5] = ':';
    append_two_digits(&state->clock_text[6], time.seconds);
    state->clock_text[8] = '\0';
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
    int window_w = 420;
    int window_h = 220;
    int x;
    int y;

    state->sw = 640;
    state->sh = 480;
    state->dragging = 0;
    state->mouse_pressed_control_id = 0;
    state->drag_offset_x = 0;
    state->drag_offset_y = 0;
    state->drag_preview_bounds = ui_rect_make(0, 0, 0, 0);
    state->start_menu_open = 0;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
    state->selected_desktop_item = -1;
    state->desktop_item_count = 0;
    (void)ui_screen_size(api, &state->sw, &state->sh);

    ui_wm_init(&state->wm, ui_theme_classic());

    /* Preload wallpaper surface once; full and partial redraws then scale in-kernel. */
    if (ui_wallpaper_surface_load(api, WALLPAPER_BMP_PATH)) {
        state->wm.theme.desktop_bg_bitmap = WALLPAPER_BMP_PATH;
    }

    load_desktop_items(api, state);

    x = (state->sw - window_w) / 2;
    y = (state->sh - window_h) / 2;
    state->window_id = ui_wm_create_window(&state->wm,
        ui_rect_make(x, y, window_w, window_h),
        "MiniDOS 95",
        1);

    (void)ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 12, ui_strlen("MENU INICIAR DISPONIVEL.") * UI_CHAR_W, UI_CHAR_H),
        "MENU INICIAR DISPONIVEL.");
    (void)ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 28, ui_strlen("Rato: clique, arraste e abre o Iniciar.") * UI_CHAR_W, UI_CHAR_H),
        "Rato: clique, arraste e abre o Iniciar.");
    (void)ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 44, ui_strlen("ESC, Q, Fechar ou Voltar para DOS saem.") * UI_CHAR_W, UI_CHAR_H),
        "ESC, Q, Fechar ou Voltar para DOS saem.");

    str_copy(state->input_text, "MiniDOS 95", (int)sizeof(state->input_text));
    state->input_id = ui_wm_add_textinput(&state->wm, state->window_id,
        ui_rect_make(12, 68, window_w - 32, 28),
        state->input_text,
        (int)sizeof(state->input_text));

    state->label_mouse_id = ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 108, 280, UI_CHAR_H),
        "");

    state->label_status_id = ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 124, 360, UI_CHAR_H),
        "");

    state->button_ok_id = ui_wm_add_button(&state->wm, state->window_id,
        ui_rect_make(window_w - 180, 158, 76, 24),
        "OK");

    state->button_cancel_id = ui_wm_add_button(&state->wm, state->window_id,
        ui_rect_make(window_w - 96, 158, 76, 24),
        "Fechar");

    ui_wm_set_focus_control(&state->wm, state->button_ok_id);

    (void)app_mouse_state(api, &state->mouse);
    update_mouse_label_text(state);
    update_clock_text(state, api);
    update_status_text(state,
        state->mouse.present
            ? "Mouse pronto. Usa OK, Fechar ou o menu Iniciar."
            : "Sem mouse. Teclado continua funcional.");
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
        int event_mask = app_wait_event_timeout(api, state.mouse.seq, CLOCK_REFRESH_MS);
        int need_full_render = 0;

        if (event_mask & APP_EVENT_MOUSE) {
            app_mouse_state_t previous_mouse = state.mouse;
            ui_rect_t previous_window_rect = current_window_rect(&state);
            ui_rect_t previous_drag_rect = current_drag_preview_rect(&state);
            int previous_start_menu_open = state.start_menu_open;
            int previous_start_button_pressed = state.start_button_pressed;
            int previous_start_menu_pressed_item = state.start_menu_pressed_item;
            int previous_start_menu_hot_item = state.start_menu_hot_item;
            int previous_selected_desktop_item = state.selected_desktop_item;
            (void)app_mouse_state(api, &state.mouse);
            update_mouse_label_text(&state);
            if (handle_mouse(&state, &previous_mouse)) {
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
            if (previous_start_menu_open != state.start_menu_open
                || previous_start_button_pressed != state.start_button_pressed
                || previous_start_menu_pressed_item != state.start_menu_pressed_item
                || previous_start_menu_hot_item != state.start_menu_hot_item
                || previous_selected_desktop_item != state.selected_desktop_item
                || !rect_equal(previous_window_rect, current_window_rect(&state))
                || !rect_equal(previous_drag_rect, current_drag_preview_rect(&state))) {
                need_full_render = 1;
            }
        }

        if (event_mask & APP_EVENT_KEY) {
            char c = 0;
            while (app_get_char_nonblock(api, &c)) {
                if (handle_keyboard(&state, c)) {
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
                if (!need_full_render) {
                    render_clock_update(api, &state);
                } else {
                    need_full_render = 1;
                }
            }
        }

        if (need_full_render) {
            render(api, &state);
        }
    }
}
