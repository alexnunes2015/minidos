#include "minidos_ui.h"

#define KEY_TAB   9
#define KEY_ENTER 13
#define KEY_ESC   27
#define KEY_SPACE 32
#define CURSOR_W  6
#define CURSOR_H  12

static const char k_intro_line_1[] = "GUI CLASSICA DISPONIVEL.";
static const char k_intro_line_2[] = "Rato: clique, arrasto na barra de titulo, TAB/ENTER ainda funcionam.";
static const char k_intro_line_3[] = "ESC, Q, Cancel ou X saem da demo.";

enum {
    HIT_NONE = 0,
    HIT_TITLE = 1,
    HIT_CLOSE = 2,
    HIT_OK = 3,
    HIT_CANCEL = 4,
};

typedef struct {
    ui_theme_t theme;
    ui_window_t window;
    app_mouse_state_t mouse;
    int sw;
    int sh;
    int active_control;
    int pointer_hot;
    int pointer_down_target;
    int dragging;
    int drag_offset_x;
    int drag_offset_y;
} demo_state_t;

typedef struct {
    ui_rect_t client;
    ui_rect_t text_box;
    ui_rect_t status_box;
    ui_rect_t label_1;
    ui_rect_t label_2;
    ui_rect_t label_3;
    ui_button_t ok_button;
    ui_button_t cancel_button;
} demo_layout_t;

static void str_copy(char* dst, const char* src, int max_len) {
    int i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_equal(const char* a, const char* b) {
    int i = 0;

    if (!a || !b) {
        return a == b;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

static void clamp_window(demo_state_t* state) {
    if (!state) {
        return;
    }

    if (state->window.bounds.x < 0) {
        state->window.bounds.x = 0;
    }
    if (state->window.bounds.y < 0) {
        state->window.bounds.y = 0;
    }
    if (state->window.bounds.x + state->window.bounds.w > state->sw) {
        state->window.bounds.x = state->sw - state->window.bounds.w;
    }
    if (state->window.bounds.y + state->window.bounds.h > state->sh - 28) {
        state->window.bounds.y = (state->sh - 28) - state->window.bounds.h;
    }
    if (state->window.bounds.x < 0) {
        state->window.bounds.x = 0;
    }
    if (state->window.bounds.y < 0) {
        state->window.bounds.y = 0;
    }
}

static int rect_is_empty(ui_rect_t rect) {
    return rect.w <= 0 || rect.h <= 0;
}

static int rects_intersect(ui_rect_t a, ui_rect_t b) {
    if (rect_is_empty(a) || rect_is_empty(b)) {
        return 0;
    }

    return a.x < (b.x + b.w) && (a.x + a.w) > b.x
        && a.y < (b.y + b.h) && (a.y + a.h) > b.y;
}

static ui_rect_t rect_union(ui_rect_t a, ui_rect_t b) {
    int right;
    int bottom;
    ui_rect_t out;

    if (rect_is_empty(a)) {
        return b;
    }
    if (rect_is_empty(b)) {
        return a;
    }

    out.x = (a.x < b.x) ? a.x : b.x;
    out.y = (a.y < b.y) ? a.y : b.y;
    right = ((a.x + a.w) > (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    bottom = ((a.y + a.h) > (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
    out.w = right - out.x;
    out.h = bottom - out.y;
    return out;
}

static ui_rect_t rect_intersect(ui_rect_t a, ui_rect_t b) {
    int x = (a.x > b.x) ? a.x : b.x;
    int y = (a.y > b.y) ? a.y : b.y;
    int x2 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    int y2 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
    if (x2 <= x || y2 <= y) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(x, y, x2 - x, y2 - y);
}

static void rect_include(ui_rect_t* dst, ui_rect_t add) {
    if (!dst || rect_is_empty(add)) {
        return;
    }

    if (rect_is_empty(*dst)) {
        *dst = add;
        return;
    }

    *dst = rect_union(*dst, add);
}

static ui_rect_t clamp_rect_to_screen(ui_rect_t rect, int sw, int sh) {
    int right = rect.x + rect.w;
    int bottom = rect.y + rect.h;

    if (rect.x < 0) {
        rect.x = 0;
    }
    if (rect.y < 0) {
        rect.y = 0;
    }
    if (right > sw) {
        right = sw;
    }
    if (bottom > sh) {
        bottom = sh;
    }

    rect.w = right - rect.x;
    rect.h = bottom - rect.y;
    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static ui_rect_t text_rect_make(int x, int y, const char* text) {
    return ui_rect_make(x, y, ui_strlen(text) * UI_CHAR_W, UI_CHAR_H);
}

static ui_rect_t cursor_rect_make(int x, int y) {
    return ui_rect_make(x, y, CURSOR_W, CURSOR_H);
}

static ui_rect_t cursor_rect_from_mouse(const app_mouse_state_t* mouse) {
    if (!mouse || !mouse->present) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return cursor_rect_make(mouse->x, mouse->y);
}

static void build_layout(const demo_state_t* state, demo_layout_t* layout) {
    if (!state || !layout) {
        return;
    }

    layout->client = ui_window_client_rect(&state->window);
    layout->label_1 = text_rect_make(layout->client.x + 12, layout->client.y + 12, k_intro_line_1);
    layout->label_2 = text_rect_make(layout->client.x + 12, layout->client.y + 28, k_intro_line_2);
    layout->label_3 = text_rect_make(layout->client.x + 12, layout->client.y + 44, k_intro_line_3);
    layout->text_box = ui_rect_make(layout->client.x + 12, layout->client.y + 68, layout->client.w - 24, 28);
    layout->status_box = ui_rect_make(layout->client.x + 12, layout->client.y + 106, layout->client.w - 24, 40);

    layout->ok_button.bounds = ui_rect_make(layout->client.x + layout->client.w - 172, layout->client.y + layout->client.h - 36, 76, 24);
    layout->ok_button.label = "OK";
    layout->ok_button.pressed = state->pointer_down_target == HIT_OK && ui_mouse_left_down(&state->mouse);
    layout->ok_button.focused = state->active_control == HIT_OK;
    layout->ok_button.enabled = 1;

    layout->cancel_button.bounds = ui_rect_make(layout->client.x + layout->client.w - 88, layout->client.y + layout->client.h - 36, 76, 24);
    layout->cancel_button.label = "Cancel";
    layout->cancel_button.pressed = state->pointer_down_target == HIT_CANCEL && ui_mouse_left_down(&state->mouse);
    layout->cancel_button.focused = state->active_control == HIT_CANCEL;
    layout->cancel_button.enabled = 1;
}

static int hit_test(const demo_state_t* state, int x, int y) {
    demo_layout_t layout;

    build_layout(state, &layout);

    if (ui_window_hit_close(&state->window, x, y)) {
        return HIT_CLOSE;
    }
    if (ui_button_contains(&layout.ok_button, x, y)) {
        return HIT_OK;
    }
    if (ui_button_contains(&layout.cancel_button, x, y)) {
        return HIT_CANCEL;
    }
    if (ui_window_hit_title(&state->window, x, y)) {
        return HIT_TITLE;
    }
    return HIT_NONE;
}

static int activate_hit(int hit, char* status, int status_len) {
    if (hit == HIT_OK) {
        str_copy(status, "OK clicado. Fluxo grafico pronto para evoluir.", status_len);
        return 0;
    }
    if (hit == HIT_CANCEL || hit == HIT_CLOSE) {
        return 1;
    }
    return 0;
}

static int handle_keyboard(demo_state_t* state, char c, char* status, int status_len) {
    if (c == 'q' || c == 'Q' || c == KEY_ESC) {
        return 1;
    }

    if (c == KEY_TAB) {
        if (state->active_control == HIT_OK) {
            state->active_control = HIT_CANCEL;
        } else {
            state->active_control = HIT_OK;
        }
        str_copy(status, "Foco movido por teclado.", status_len);
        return 0;
    }

    if (c == KEY_SPACE || c == KEY_ENTER) {
        return activate_hit(state->active_control, status, status_len);
    }

    return 0;
}

static int handle_mouse(demo_state_t* state, const app_mouse_state_t* previous, char* status, int status_len) {
    state->pointer_hot = hit_test(state, state->mouse.x, state->mouse.y);

    if (state->pointer_hot == HIT_OK || state->pointer_hot == HIT_CANCEL) {
        state->active_control = state->pointer_hot;
    }

    if (ui_mouse_left_pressed(previous, &state->mouse)) {
        state->pointer_down_target = state->pointer_hot;
        if (state->pointer_hot == HIT_TITLE) {
            state->dragging = 1;
            state->drag_offset_x = state->mouse.x - state->window.bounds.x;
            state->drag_offset_y = state->mouse.y - state->window.bounds.y;
            str_copy(status, "A arrastar a janela.", status_len);
        }
    }

    if (state->dragging && ui_mouse_left_down(&state->mouse)) {
        state->window.bounds.x = state->mouse.x - state->drag_offset_x;
        state->window.bounds.y = state->mouse.y - state->drag_offset_y;
        clamp_window(state);
    }

    if (ui_mouse_left_released(previous, &state->mouse)) {
        int release_hot = hit_test(state, state->mouse.x, state->mouse.y);

        state->dragging = 0;
        if (state->pointer_down_target != HIT_NONE && state->pointer_down_target == release_hot) {
            int hit = state->pointer_down_target;
            state->pointer_down_target = HIT_NONE;
            return activate_hit(hit, status, status_len);
        }
        state->pointer_down_target = HIT_NONE;
    }

    return 0;
}

static void draw_window_contents(const minidos_app_api_t* api, const demo_state_t* state, const char* status, const demo_layout_t* layout) {
    if (!layout) {
        return;
    }

    ui_draw_label(api, layout->label_1.x, layout->label_1.y, k_intro_line_1, &state->theme, state->theme.field_bg);
    ui_draw_label(api, layout->label_2.x, layout->label_2.y, k_intro_line_2, &state->theme, state->theme.field_bg);
    ui_draw_label(api, layout->label_3.x, layout->label_3.y, k_intro_line_3, &state->theme, state->theme.field_bg);

    ui_draw_text_box(api, &state->theme, layout->text_box,
        state->mouse.present ? "Mouse PS/2 ativo" : "Mouse PS/2 nao detetado",
        state->active_control == HIT_NONE);

    ui_draw_panel(api, &state->theme, layout->status_box, 0);
    ui_draw_label(api, layout->status_box.x + 8, layout->status_box.y + 8, status, &state->theme, state->theme.face);

    ui_draw_button(api, &state->theme, &layout->ok_button);
    ui_draw_button(api, &state->theme, &layout->cancel_button);
}

static void draw_demo_full(const minidos_app_api_t* api, const demo_state_t* state, const char* status) {
    demo_layout_t layout;

    build_layout(state, &layout);
    ui_draw_desktop(api, &state->theme, state->sw, state->sh, state->mouse.present ? "MiniDOS Mouse" : "MiniDOS UI");
    ui_draw_window(api, &state->theme, &state->window);
    draw_window_contents(api, state, status, &layout);
}

static void redraw_desktop_region(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t dirty) {
    ui_button_t start_button;
    ui_rect_t taskbar;
    ui_rect_t brand;
    ui_rect_t accent;

    if (rect_is_empty(dirty)) {
        return;
    }

    ui_fill_rect(api, dirty, state->theme.desktop_bg);

    accent = ui_rect_make(0, 0, state->sw, 2);
    if (rects_intersect(dirty, accent)) {
        ui_fill_rect(api, accent, state->theme.desktop_accent);
    }

    taskbar = ui_rect_make(0, state->sh - 28, state->sw, 28);
    if (!rects_intersect(dirty, taskbar)) {
        return;
    }

    ui_draw_panel(api, &state->theme, taskbar, 1);

    start_button.bounds = ui_rect_make(4, state->sh - 24, 58, 20);
    start_button.label = "Start";
    start_button.pressed = 0;
    start_button.focused = 0;
    start_button.enabled = 1;
    ui_draw_button(api, &state->theme, &start_button);

    brand = ui_rect_make(state->sw - 130, state->sh - 24, 122, 18);
    ui_draw_panel(api, &state->theme, brand, 0);
    ui_draw_label_centered(api, brand,
        state->mouse.present ? "MiniDOS Mouse" : "MiniDOS UI",
        state->theme.text,
        state->theme.face);
}

static void redraw_title_bar(const minidos_app_api_t* api, const demo_state_t* state) {
    ui_rect_t title_rect = ui_window_title_bar_rect(&state->window);
    ui_rect_t close_rect = ui_window_close_button_rect(&state->window);
    unsigned int tbg = state->window.active ? state->theme.title_active_bg : state->theme.title_inactive_bg;
    unsigned int tfg = state->window.active ? state->theme.title_active_text : state->theme.title_inactive_text;

    ui_fill_rect(api, title_rect, tbg);
    ui_draw_text(api, title_rect.x + 6, title_rect.y + 4,
        state->window.title ? state->window.title : "", tfg, tbg);

    if (state->window.has_close_button && !rect_is_empty(close_rect)) {
        ui_button_t close_btn;
        close_btn.bounds = close_rect;
        close_btn.label = "X";
        close_btn.pressed = 0;
        close_btn.focused = 0;
        close_btn.enabled = 1;
        ui_draw_button(api, &state->theme, &close_btn);
    }
}

static void redraw_window_region(const minidos_app_api_t* api, const demo_state_t* state, const char* status, ui_rect_t dirty) {
    demo_layout_t layout;
    ui_rect_t title_rect;
    ui_rect_t close_rect;
    ui_rect_t client_dirty;
    int dirty_right;
    int dirty_bottom;

    build_layout(state, &layout);
    if (!rects_intersect(dirty, state->window.bounds)) {
        return;
    }

    title_rect = ui_window_title_bar_rect(&state->window);
    close_rect = ui_window_close_button_rect(&state->window);
    dirty_right = dirty.x + dirty.w - 1;
    dirty_bottom = dirty.y + dirty.h - 1;

    /* Dirty extends outside the outer window boundary (e.g. window was
     * dragged, or cursor crosses the window edge from the desktop).
     * A full repaint is required. */
    if (!ui_rect_contains(&state->window.bounds, dirty.x, dirty.y)
        || !ui_rect_contains(&state->window.bounds, dirty_right, dirty_bottom)) {
        ui_draw_window(api, &state->theme, &state->window);
        draw_window_contents(api, state, status, &layout);
        return;
    }

    /* --- dirty is fully inside window.bounds --- */

    /* Title bar / close button dirtied: redraw only those elements.
     * This is the common case when the cursor moves over the title bar;
     * it must NOT trigger a full window + contents repaint. */
    if (rects_intersect(dirty, title_rect) || rects_intersect(dirty, close_rect)) {
        redraw_title_bar(api, state);
    }

    /* Window border strips (between window.bounds edge and layout.client)
     * dirtied but not by a title-bar overlap: redraw the panel bevel and
     * restore the title that ui_draw_panel would overwrite. */
    if (!ui_rect_contains(&layout.client, dirty.x, dirty.y)
        || !ui_rect_contains(&layout.client, dirty_right, dirty_bottom)) {
        if (!rects_intersect(dirty, title_rect) && !rects_intersect(dirty, close_rect)) {
            /* Border strip only – cheap: redraw panel bevel then title. */
            ui_draw_panel(api, &state->theme, state->window.bounds, 1);
            redraw_title_bar(api, state);
        }
    }

    /* Partial client area redraw clipped to layout.client */
    client_dirty = rect_intersect(dirty, layout.client);
    if (rect_is_empty(client_dirty)) {
        return;
    }

    ui_fill_rect(api, client_dirty, state->theme.field_bg);

    if (rects_intersect(client_dirty, layout.label_1)) {
        ui_draw_label(api, layout.label_1.x, layout.label_1.y, k_intro_line_1, &state->theme, state->theme.field_bg);
    }
    if (rects_intersect(client_dirty, layout.label_2)) {
        ui_draw_label(api, layout.label_2.x, layout.label_2.y, k_intro_line_2, &state->theme, state->theme.field_bg);
    }
    if (rects_intersect(client_dirty, layout.label_3)) {
        ui_draw_label(api, layout.label_3.x, layout.label_3.y, k_intro_line_3, &state->theme, state->theme.field_bg);
    }
    if (rects_intersect(client_dirty, layout.text_box)) {
        ui_draw_text_box(api, &state->theme, layout.text_box,
            state->mouse.present ? "Mouse PS/2 ativo" : "Mouse PS/2 nao detetado",
            state->active_control == HIT_NONE);
    }
    if (rects_intersect(client_dirty, layout.status_box)) {
        ui_draw_panel(api, &state->theme, layout.status_box, 0);
        ui_draw_label(api, layout.status_box.x + 8, layout.status_box.y + 8, status, &state->theme, state->theme.face);
    }
    if (rects_intersect(client_dirty, layout.ok_button.bounds)) {
        ui_draw_button(api, &state->theme, &layout.ok_button);
    }
    if (rects_intersect(client_dirty, layout.cancel_button.bounds)) {
        ui_draw_button(api, &state->theme, &layout.cancel_button);
    }
}

static void redraw_scene_region(const minidos_app_api_t* api, const demo_state_t* state, const char* status, ui_rect_t dirty) {
    dirty = clamp_rect_to_screen(dirty, state->sw, state->sh);
    if (rect_is_empty(dirty)) {
        return;
    }

    redraw_desktop_region(api, state, dirty);
    redraw_window_region(api, state, status, dirty);
}

int app_main(const minidos_app_api_t* api) {
    char status[96];
    char previous_status[96];
    demo_state_t state;

    if (!api) {
        return 1;
    }

    state.theme = ui_theme_classic();
    state.sw = 640;
    state.sh = 480;
    state.active_control = HIT_OK;
    state.pointer_hot = HIT_NONE;
    state.pointer_down_target = HIT_NONE;
    state.dragging = 0;
    state.drag_offset_x = 0;
    state.drag_offset_y = 0;
    (void)ui_screen_size(api, &state.sw, &state.sh);

    state.window.bounds = ui_rect_make((state.sw - 420) / 2, (state.sh - 220) / 2, 420, 220);
    state.window.title = "MiniDOS Setup";
    state.window.active = 1;
    state.window.has_close_button = 1;
    (void)app_mouse_state(api, &state.mouse);
    str_copy(status, state.mouse.present ? "Mouse pronto. Clica em OK, Cancel ou na barra de titulo." : "Sem mouse. Teclado continua funcional.", sizeof(status));
    draw_demo_full(api, &state, status);
    if (state.mouse.present) {
        ui_draw_cursor(api, state.mouse.x, state.mouse.y, state.theme.light, state.theme.dark_shadow);
    }

    while (1) {
        app_mouse_state_t previous_mouse = state.mouse;
        ui_rect_t previous_window_bounds = state.window.bounds;
        ui_rect_t dirty = ui_rect_make(0, 0, 0, 0);
        demo_layout_t layout;
        int previous_active_control = state.active_control;
        int previous_pointer_down_target = state.pointer_down_target;
        int event_mask;

        str_copy(previous_status, status, sizeof(previous_status));
        event_mask = app_wait_event(api, state.mouse.seq);

        if (event_mask & APP_EVENT_MOUSE) {
            (void)app_mouse_state(api, &state.mouse);
            if (handle_mouse(&state, &previous_mouse, status, (int)sizeof(status))) {
                app_gfx_clear(api, 0x000000u);
                return 0;
            }
        }

        if (event_mask & APP_EVENT_KEY) {
            char c = 0;
            while (app_get_char_nonblock(api, &c)) {
                if (handle_keyboard(&state, c, status, (int)sizeof(status))) {
                    app_gfx_clear(api, 0x000000u);
                    return 0;
                }
            }
        }

        build_layout(&state, &layout);

        if (previous_window_bounds.x != state.window.bounds.x
            || previous_window_bounds.y != state.window.bounds.y
            || previous_window_bounds.w != state.window.bounds.w
            || previous_window_bounds.h != state.window.bounds.h) {
            rect_include(&dirty, previous_window_bounds);
            rect_include(&dirty, state.window.bounds);
        }

        if (!str_equal(previous_status, status)) {
            rect_include(&dirty, layout.status_box);
        }

        if (previous_active_control != state.active_control
            || previous_pointer_down_target != state.pointer_down_target) {
            rect_include(&dirty, layout.ok_button.bounds);
            rect_include(&dirty, layout.cancel_button.bounds);
        }

        if (event_mask & APP_EVENT_MOUSE) {
            rect_include(&dirty, cursor_rect_from_mouse(&previous_mouse));
            rect_include(&dirty, cursor_rect_from_mouse(&state.mouse));
        }

        redraw_scene_region(api, &state, status, dirty);
        if (state.mouse.present && !rect_is_empty(dirty)) {
            ui_draw_cursor(api, state.mouse.x, state.mouse.y, state.theme.light, state.theme.dark_shadow);
        }
    }
}
