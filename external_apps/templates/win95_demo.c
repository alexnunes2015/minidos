#include "minidos_ui.h"

#define KEY_TAB   9
#define KEY_ENTER 13
#define KEY_ESC   27
#define KEY_SPACE 32

typedef struct {
    ui_window_manager_t wm;
    app_mouse_state_t mouse;
    int sw;
    int sh;
    int window_id;
    int label_mouse_id;
    int label_status_id;
    int button_ok_id;
    int button_cancel_id;
    int input_id;
    int dragging;
    int mouse_pressed_control_id;
    int drag_offset_x;
    int drag_offset_y;
    char mouse_text[48];
    char status_text[96];
    char input_text[64];
} demo_state_t;

static void str_copy(char* dst, const char* src, int max_len) {
    int i = 0;
    while (i < (max_len - 1) && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void update_mouse_label_text(demo_state_t* state) {
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

static void str_append(char* dst, int max_len, const char* src) {
    int i = 0;
    int j;

    if (!dst || max_len <= 0 || !src) {
        return;
    }

    while (i < (max_len - 1) && dst[i] != '\0') {
        i++;
    }

    j = 0;
    while (i < (max_len - 1) && src[j] != '\0') {
        dst[i++] = src[j++];
    }
    dst[i] = '\0';
}

static void str_append_uint(char* dst, int max_len, unsigned int value) {
    char digits[16];
    int i = 0;

    if (!dst || max_len <= 0) {
        return;
    }

    if (value == 0) {
        str_append(dst, max_len, "0");
        return;
    }

    while (value > 0 && i < (int)sizeof(digits)) {
        digits[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0) {
        char digit_text[2];
        digit_text[0] = digits[--i];
        digit_text[1] = '\0';
        str_append(dst, max_len, digit_text);
    }
}

static void str_append_int(char* dst, int max_len, int value) {
    if (value < 0) {
        str_append(dst, max_len, "-");
        str_append_uint(dst, max_len, (unsigned int)(-value));
        return;
    }
    str_append_uint(dst, max_len, (unsigned int)value);
}

static void log_mouse_debug(const minidos_app_api_t* api, const char* phase, int activated,
    int out_window_id, int out_control_id, int out_hit_close, const demo_state_t* state) {
    char message[160];

    if (!api || !phase || !state) {
        return;
    }

    message[0] = '\0';
    str_append(message, (int)sizeof(message), "[win95ui] ");
    str_append(message, (int)sizeof(message), phase);
    str_append(message, (int)sizeof(message), " act=");
    str_append_int(message, (int)sizeof(message), activated);
    str_append(message, (int)sizeof(message), " win=");
    str_append_int(message, (int)sizeof(message), out_window_id);
    str_append(message, (int)sizeof(message), " ctrl=");
    str_append_int(message, (int)sizeof(message), out_control_id);
    str_append(message, (int)sizeof(message), " close=");
    str_append_int(message, (int)sizeof(message), out_hit_close);
    str_append(message, (int)sizeof(message), " focus=");
    str_append_int(message, (int)sizeof(message), state->wm.focused_control_id);
    str_append(message, (int)sizeof(message), " mx=");
    str_append_int(message, (int)sizeof(message), state->mouse.x);
    str_append(message, (int)sizeof(message), " my=");
    str_append_int(message, (int)sizeof(message), state->mouse.y);
    app_puts(api, message);
}

static void log_key_debug(const minidos_app_api_t* api, char c, const demo_state_t* state) {
    char message[160];

    if (!api || !state) {
        return;
    }

    message[0] = '\0';
    str_append(message, (int)sizeof(message), "[win95ui] key code=");
    str_append_int(message, (int)sizeof(message), (unsigned char)c);
    str_append(message, (int)sizeof(message), " focus=");
    str_append_int(message, (int)sizeof(message), state->wm.focused_control_id);
    str_append(message, (int)sizeof(message), " char=");
    if (c >= 32 && c <= 126) {
        {
            char text[2];
            text[0] = c;
            text[1] = '\0';
            str_append(message, (int)sizeof(message), text);
        }
    } else {
        str_append(message, (int)sizeof(message), "?");
    }
    app_puts(api, message);
}

static void update_status_text(demo_state_t* state, const char* text) {
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

static void clamp_window(demo_state_t* state) {
    ui_wm_window_t* win;

    if (!state) {
        return;
    }

    win = ui_wm_find_window(&state->wm, state->window_id);
    if (!win) {
        return;
    }

    if (win->window.bounds.x < 0) {
        win->window.bounds.x = 0;
    }
    if (win->window.bounds.y < 0) {
        win->window.bounds.y = 0;
    }
    if (win->window.bounds.x + win->window.bounds.w > state->sw) {
        win->window.bounds.x = state->sw - win->window.bounds.w;
    }
    if (win->window.bounds.y + win->window.bounds.h > state->sh - 28) {
        win->window.bounds.y = (state->sh - 28) - win->window.bounds.h;
    }
    if (win->window.bounds.x < 0) {
        win->window.bounds.x = 0;
    }
    if (win->window.bounds.y < 0) {
        win->window.bounds.y = 0;
    }
}

static void render(const minidos_app_api_t* api, const demo_state_t* state) {
    if (!api || !state) {
        return;
    }

    ui_wm_draw(api, &state->wm, state->sw, state->sh,
        state->mouse.present ? "MiniDOS Mouse" : "MiniDOS UI");

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

static int handle_activated_control(demo_state_t* state, int control_id) {
    if (control_id == state->button_ok_id) {
        update_status_text(state, "OK clicado. Fluxo grafico pronto.");
        return 0;
    }
    if (control_id == state->button_cancel_id) {
        return 1;
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

static int handle_keyboard(demo_state_t* state, const minidos_app_api_t* api, char c) {
    log_key_debug(api, c, state);

    if (c == 'q' || c == 'Q' || c == KEY_ESC) {
        app_puts(api, "[win95ui] exit by keyboard shortcut");
        return 1;
    }

    if (c == KEY_TAB) {
        cycle_focus(state);
        return 0;
    }

    if (c == KEY_ENTER || c == KEY_SPACE) {
        if (state->wm.focused_control_id == state->button_ok_id || state->wm.focused_control_id == state->button_cancel_id) {
            app_puts(api, "[win95ui] activate focused button by keyboard");
            return handle_activated_control(state, state->wm.focused_control_id);
        }
    }

    (void)ui_wm_dispatch_key(&state->wm, c);
    return 0;
}

static int handle_mouse(demo_state_t* state, const minidos_app_api_t* api, const app_mouse_state_t* previous_mouse) {
    int out_window_id = 0;
    int out_control_id = 0;
    int out_hit_close = 0;
    int activated;
    int left_down;
    int left_pressed;
    int left_released;
    ui_wm_window_t* win;
    const ui_control_t* pressed_control;
    ui_rect_t pressed_bounds;

    if (!state || !previous_mouse) {
        return 0;
    }

    left_down = ui_mouse_left_down(&state->mouse);
    left_pressed = ui_mouse_left_pressed(previous_mouse, &state->mouse);
    left_released = ui_mouse_left_released(previous_mouse, &state->mouse);

    activated = ui_wm_dispatch_mouse(&state->wm,
        state->mouse.x,
        state->mouse.y,
        left_down,
        left_pressed,
        left_released,
        &out_window_id,
        &out_control_id,
        &out_hit_close);

    if (left_pressed || left_released) {
        log_mouse_debug(api, left_pressed ? "mouse-down" : "mouse-up",
            activated, out_window_id, out_control_id, out_hit_close, state);
    }

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
        update_status_text(state, "A arrastar a janela.");
    }

    if (state->dragging && left_down && win) {
        win->window.bounds.x = state->mouse.x - state->drag_offset_x;
        win->window.bounds.y = state->mouse.y - state->drag_offset_y;
        clamp_window(state);
    }

    if (left_released) {
        state->dragging = 0;

        if (!activated
            && state->mouse_pressed_control_id != 0) {
            pressed_control = ui_wm_find_control_const(&state->wm, state->mouse_pressed_control_id);
            pressed_bounds = ui_wm_control_abs_bounds(&state->wm, pressed_control);
            if (pressed_control && ui_rect_contains(&pressed_bounds, state->mouse.x, state->mouse.y)) {
                app_puts(api, "[win95ui] fallback button activation");
                activated = 1;
                out_control_id = state->mouse_pressed_control_id;
            }
        }

        state->mouse_pressed_control_id = 0;
    }

    if (out_hit_close && activated) {
        app_puts(api, "[win95ui] exit by close button");
        return 1;
    }

    if (activated && out_control_id != 0) {
        if (out_control_id == state->button_cancel_id) {
            app_puts(api, "[win95ui] cancel button activated");
        } else if (out_control_id == state->button_ok_id) {
            app_puts(api, "[win95ui] ok button activated");
        } else if (out_control_id == state->input_id) {
            app_puts(api, "[win95ui] textbox reported activation");
        }
        return handle_activated_control(state, out_control_id);
    }

    return 0;
}

static void init_demo(demo_state_t* state, const minidos_app_api_t* api) {
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
    (void)ui_screen_size(api, &state->sw, &state->sh);

    ui_wm_init(&state->wm, ui_theme_classic());

    x = (state->sw - window_w) / 2;
    y = (state->sh - window_h) / 2;
    state->window_id = ui_wm_create_window(&state->wm,
        ui_rect_make(x, y, window_w, window_h),
        "MiniDOS Setup",
        1);

    (void)ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 12, ui_strlen("GUI CLASSICA DISPONIVEL.") * UI_CHAR_W, UI_CHAR_H),
        "GUI CLASSICA DISPONIVEL.");
    (void)ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 28, ui_strlen("Rato: clique e arraste. TAB/ENTER navegam.") * UI_CHAR_W, UI_CHAR_H),
        "Rato: clique e arraste. TAB/ENTER navegam.");
    (void)ui_wm_add_label(&state->wm, state->window_id,
        ui_rect_make(12, 44, ui_strlen("ESC, Q, Cancel ou X saem da demo.") * UI_CHAR_W, UI_CHAR_H),
        "ESC, Q, Cancel ou X saem da demo.");

    str_copy(state->input_text, "MiniDOS GUI", (int)sizeof(state->input_text));
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
        "Cancel");

    ui_wm_set_focus_control(&state->wm, state->button_ok_id);

    (void)app_mouse_state(api, &state->mouse);
    update_mouse_label_text(state);
    update_status_text(state,
        state->mouse.present
            ? "Mouse pronto. Usa OK, Cancel ou barra."
            : "Sem mouse. Teclado continua funcional.");
}

int app_main(const minidos_app_api_t* api) {
    demo_state_t state;

    if (!api) {
        return 1;
    }

    init_demo(&state, api);
    app_puts(api, "[win95ui] mouse debug logging enabled");
    render(api, &state);

    while (1) {
        int event_mask = app_wait_event(api, state.mouse.seq);

        if (event_mask & APP_EVENT_MOUSE) {
            app_mouse_state_t previous_mouse = state.mouse;
            (void)app_mouse_state(api, &state.mouse);
            update_mouse_label_text(&state);
            if (handle_mouse(&state, api, &previous_mouse)) {
                app_gfx_clear(api, 0x000000u);
                ui_present(api);
                return 0;
            }
        }

        if (event_mask & APP_EVENT_KEY) {
            char c = 0;
            while (app_get_char_nonblock(api, &c)) {
                if (handle_keyboard(&state, api, c)) {
                    app_gfx_clear(api, 0x000000u);
                    ui_present(api);
                    return 0;
                }
            }
        }

        render(api, &state);
    }
}