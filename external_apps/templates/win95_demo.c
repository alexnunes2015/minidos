#include "minidos_ui.h"

#define KEY_TAB   9
#define KEY_ENTER 13
#define KEY_ESC   27
#define KEY_SPACE 32
#define TASKBAR_H 28
#define START_BUTTON_W 74
#define START_BUTTON_H 20
#define START_MENU_W 220
#define START_MENU_H 188
#define START_MENU_STRIP_W 28
#define START_MENU_ITEM_NONE (-1)
#define START_MENU_ITEM_COUNT 4
#define CLOCK_TEXT_LEN 9
#define CLOCK_REFRESH_MS 1000

enum {
    START_MENU_ITEM_PROGRAMAS = 0,
    START_MENU_ITEM_DOCUMENTOS = 1,
    START_MENU_ITEM_DEFINICOES = 2,
    START_MENU_ITEM_AJUDA = 3,
    START_MENU_ITEM_BACK_TO_DOS = 4,
};

static const char* g_start_menu_items[START_MENU_ITEM_COUNT] = {
    "Programas",
    "Documentos",
    "Definicoes",
    "Ajuda",
};

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
    int start_menu_open;
    int start_button_pressed;
    int start_menu_pressed_item;
    int start_menu_hot_item;
    char mouse_text[48];
    char status_text[96];
    char input_text[64];
    char clock_text[CLOCK_TEXT_LEN];
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

static void append_two_digits(char* out, unsigned int value) {
    out[0] = (char)('0' + ((value / 10u) % 10u));
    out[1] = (char)('0' + (value % 10u));
}

static void update_clock_text(demo_state_t* state, const minidos_app_api_t* api) {
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

static ui_rect_t taskbar_rect(const demo_state_t* state) {
    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(0, state->sh - TASKBAR_H, state->sw, TASKBAR_H);
}

static ui_rect_t start_button_rect(const demo_state_t* state) {
    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(4, state->sh - 24, START_BUTTON_W, START_BUTTON_H);
}

static ui_rect_t start_menu_rect(const demo_state_t* state) {
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

static ui_rect_t start_menu_item_rect(const demo_state_t* state, int item_index) {
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

static ui_rect_t start_menu_exit_rect(const demo_state_t* state) {
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

static void dismiss_start_menu(demo_state_t* state) {
    if (!state) {
        return;
    }

    state->start_menu_open = 0;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
}

static void open_start_menu(demo_state_t* state) {
    if (!state) {
        return;
    }

    state->start_menu_open = 1;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
    update_status_text(state, "Menu Iniciar aberto.");
}

static void close_start_menu(demo_state_t* state, const char* reason) {
    dismiss_start_menu(state);
    if (reason) {
        update_status_text(state, reason);
    }
}

static int start_menu_hit_test(const demo_state_t* state, int x, int y) {
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

static int handle_start_menu_action(demo_state_t* state, int action_id) {
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
    if (win->window.bounds.y + win->window.bounds.h > state->sh - TASKBAR_H) {
        win->window.bounds.y = (state->sh - TASKBAR_H) - win->window.bounds.h;
    }
    if (win->window.bounds.x < 0) {
        win->window.bounds.x = 0;
    }
    if (win->window.bounds.y < 0) {
        win->window.bounds.y = 0;
    }
}

static void draw_start_menu_item(const minidos_app_api_t* api, const demo_state_t* state,
    int item_id, const char* label, unsigned int icon_color) {
    ui_rect_t item_rect;
    ui_rect_t icon_rect;
    const ui_theme_t* theme;
    unsigned int bg;
    unsigned int fg;

    if (!api || !state || !label) {
        return;
    }

    theme = &state->wm.theme;
    item_rect = start_menu_item_rect(state, item_id);
    bg = theme->field_bg;
    fg = theme->text;

    if (state->start_menu_hot_item == item_id) {
        bg = theme->selection_bg;
        fg = theme->selection_text;
        ui_fill_rect(api, item_rect, bg);
    } else {
        ui_fill_rect(api, item_rect, bg);
    }

    icon_rect = ui_rect_make(item_rect.x + 4, item_rect.y + 3, 16, 16);
    ui_fill_rect(api, icon_rect, icon_color);
    ui_frame_rect(api, icon_rect, theme->dark_shadow);
    ui_draw_text(api, item_rect.x + 28, item_rect.y + 7, label, fg, bg);
    ui_draw_text(api, item_rect.x + item_rect.w - 12, item_rect.y + 7, ">", fg, bg);
}

static void draw_start_menu(const minidos_app_api_t* api, const demo_state_t* state) {
    ui_rect_t menu;
    ui_rect_t strip;
    ui_rect_t divider;
    ui_rect_t exit_rect;
    ui_button_t exit_button;
    const ui_theme_t* theme;

    if (!api || !state || !state->start_menu_open) {
        return;
    }

    theme = &state->wm.theme;
    menu = start_menu_rect(state);
    strip = ui_rect_make(menu.x + 4, menu.y + 4, START_MENU_STRIP_W, menu.h - 8);
    divider = ui_rect_make(menu.x + START_MENU_STRIP_W + 12, menu.y + menu.h - 48, menu.w - START_MENU_STRIP_W - 24, 2);
    exit_rect = start_menu_exit_rect(state);

    ui_draw_panel(api, theme, menu, 1);
    ui_fill_rect(api, strip, theme->title_active_bg);
    ui_draw_text(api, strip.x + 4, strip.y + 12, "MiniDOS", theme->title_active_text, theme->title_active_bg);
    ui_draw_text(api, strip.x + 9, strip.y + 28, "95", theme->title_active_text, theme->title_active_bg);

    draw_start_menu_item(api, state, START_MENU_ITEM_PROGRAMAS, g_start_menu_items[START_MENU_ITEM_PROGRAMAS], ui_rgb(255, 0, 0));
    draw_start_menu_item(api, state, START_MENU_ITEM_DOCUMENTOS, g_start_menu_items[START_MENU_ITEM_DOCUMENTOS], ui_rgb(255, 255, 0));
    draw_start_menu_item(api, state, START_MENU_ITEM_DEFINICOES, g_start_menu_items[START_MENU_ITEM_DEFINICOES], ui_rgb(0, 128, 255));
    draw_start_menu_item(api, state, START_MENU_ITEM_AJUDA, g_start_menu_items[START_MENU_ITEM_AJUDA], ui_rgb(0, 180, 0));

    ui_fill_rect(api, ui_rect_make(divider.x, divider.y, divider.w, 1), theme->shadow);
    ui_fill_rect(api, ui_rect_make(divider.x, divider.y + 1, divider.w, 1), theme->light);

    exit_button.bounds = exit_rect;
    exit_button.label = "Voltar para DOS";
    exit_button.pressed = (state->start_menu_pressed_item == START_MENU_ITEM_BACK_TO_DOS);
    exit_button.focused = (state->start_menu_hot_item == START_MENU_ITEM_BACK_TO_DOS);
    exit_button.enabled = 1;
    ui_draw_button(api, theme, &exit_button);
}

static void draw_taskbar_overlay(const minidos_app_api_t* api, const demo_state_t* state) {
    ui_rect_t taskbar;
    ui_rect_t start_rect;
    ui_rect_t task_rect;
    ui_rect_t brand_rect;
    ui_rect_t clock_rect;
    ui_button_t start_button;
    const ui_theme_t* theme;

    if (!api || !state) {
        return;
    }

    theme = &state->wm.theme;
    taskbar = taskbar_rect(state);
    start_rect = start_button_rect(state);
    task_rect = ui_rect_make(start_rect.x + start_rect.w + 6, taskbar.y + 4, 170, 20);
    clock_rect = ui_rect_make(state->sw - 88, taskbar.y + 4, 80, 20);
    brand_rect = ui_rect_make(clock_rect.x - 94, taskbar.y + 4, 86, 20);

    ui_draw_panel(api, theme, taskbar, 1);

    start_button.bounds = start_rect;
    start_button.label = "Iniciar";
    start_button.pressed = state->start_button_pressed || state->start_menu_open;
    start_button.focused = 0;
    start_button.enabled = 1;
    ui_draw_button(api, theme, &start_button);

    ui_draw_panel(api, theme, task_rect, 0);
    ui_draw_text(api, task_rect.x + 8, task_rect.y + 6, "MiniDOS 95 Demo", theme->text, theme->face);

    ui_draw_panel(api, theme, brand_rect, 0);
    ui_draw_label_centered(api, brand_rect, "A:\\ GUI", theme->text, theme->face);

    ui_draw_panel(api, theme, clock_rect, 0);
    ui_draw_label_centered(api, clock_rect, state->clock_text, theme->text, theme->face);
}

static void render(const minidos_app_api_t* api, const demo_state_t* state) {
    if (!api || !state) {
        return;
    }

    ui_wm_draw(api, &state->wm, state->sw, state->sh,
        "MiniDOS 95");

    draw_taskbar_overlay(api, state);
    draw_start_menu(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

static int handle_activated_control(demo_state_t* state, int control_id) {
    if (control_id == state->button_ok_id) {
        dismiss_start_menu(state);
        update_status_text(state, "OK clicado. Fluxo grafico pronto.");
        return 0;
    }
    if (control_id == state->button_cancel_id) {
        dismiss_start_menu(state);
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

static int handle_keyboard(demo_state_t* state, char c) {
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

static int handle_mouse(demo_state_t* state, const app_mouse_state_t* previous_mouse) {
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
    int over_start;
    int over_menu;
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
                activated = 1;
                out_control_id = state->mouse_pressed_control_id;
            }
        }

        state->mouse_pressed_control_id = 0;
    }

    if (out_hit_close && activated) {
        return 1;
    }

    if (activated && out_control_id != 0) {
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
    state->start_menu_open = 0;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
    (void)ui_screen_size(api, &state->sw, &state->sh);

    ui_wm_init(&state->wm, ui_theme_classic());

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

    if (!api) {
        return 1;
    }

    init_demo(&state, api);
    render(api, &state);

    while (1) {
        int event_mask = app_wait_event_timeout(api, state.mouse.seq, CLOCK_REFRESH_MS);

        if (event_mask & APP_EVENT_MOUSE) {
            app_mouse_state_t previous_mouse = state.mouse;
            (void)app_mouse_state(api, &state.mouse);
            update_mouse_label_text(&state);
            if (handle_mouse(&state, &previous_mouse)) {
                app_gfx_clear(api, 0x000000u);
                ui_present(api);
                return 0;
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
            }
        }

        if (event_mask & (APP_EVENT_MOUSE | APP_EVENT_KEY | APP_EVENT_TIMER)) {
            update_clock_text(&state, api);
        }

        render(api, &state);
    }
}
