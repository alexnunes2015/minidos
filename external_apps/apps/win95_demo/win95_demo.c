#include "minidos_ui.h"

#define KEY_TAB   9
#define KEY_ENTER 13
#define KEY_ESC   27
#define KEY_SPACE 32
#define TASKBAR_H 28
/* User-supplied wallpaper BMP (optional; loaded once at startup) */
#define RESOURCE_HOME_DIR_1 "AIOS"
#define WALLPAPER_BMP_PATH "BG.BMP"
#define START_BUTTON_W 86
#define START_BUTTON_H 20
#define START_MENU_W 220
#define START_MENU_H 188
#define START_MENU_STRIP_W 28
#define START_MENU_ITEM_NONE (-1)
#define START_MENU_ITEM_COUNT 4
#define DESKTOP_MAX_ITEMS 16
#define DESKTOP_CELL_W 96
#define DESKTOP_CELL_H 70
#define DESKTOP_ICON_W 36
#define DESKTOP_ICON_H 28
#define CLOCK_TEXT_LEN 9
#define CLOCK_REFRESH_MS 1000

enum {
    START_MENU_ITEM_PROGRAMAS = 0,
    START_MENU_ITEM_DOCUMENTOS = 1,
    START_MENU_ITEM_DEFINICOES = 2,
    START_MENU_ITEM_AJUDA = 3,
    START_MENU_ITEM_BACK_TO_DOS = 4,
};

typedef struct {
    const char* label;
    const char* detail;
    const char* glyph;
    unsigned int accent;
} start_menu_entry_t;

static const start_menu_entry_t g_start_menu_items[START_MENU_ITEM_COUNT] = {
    {"Programas", "Apps e demos", "P", 0xC04040u},
    {"Documentos", "Ficheiros e notas", "D", 0xD0A020u},
    {"Definicoes", "Tema e video", "C", 0x2C76D2u},
    {"Ajuda", "Dicas rapidas", "?", 0x2C9A58u},
};

typedef struct {
    char name[13];
    int is_dir;
    ui_rect_t bounds;
} desktop_item_t;

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
    ui_rect_t drag_preview_bounds;
    int start_menu_open;
    int start_button_pressed;
    int start_menu_pressed_item;
    int start_menu_hot_item;
    int desktop_item_count;
    desktop_item_t desktop_items[DESKTOP_MAX_ITEMS];
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

static int str_equal(const char* a, const char* b) {
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

static int rect_equal(ui_rect_t a, ui_rect_t b) {
    return a.x == b.x
        && a.y == b.y
        && a.w == b.w
        && a.h == b.h;
}

static void enter_resource_home(const minidos_app_api_t* api) {
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

static void enter_desktop_home(const minidos_app_api_t* api) {
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

static void desktop_item_icon_tag(const desktop_item_t* item, char out[4]) {
    const char* ext = 0;
    int i;

    if (!out) {
        return;
    }

    if (!item) {
        out[0] = 'F';
        out[1] = 'I';
        out[2] = 'L';
        out[3] = '\0';
        return;
    }

    if (item->is_dir) {
        out[0] = 'D';
        out[1] = 'I';
        out[2] = 'R';
        out[3] = '\0';
        return;
    }

    for (i = 0; item->name[i] != '\0'; i++) {
        if (item->name[i] == '.') {
            ext = &item->name[i + 1];
        }
    }

    if (!ext || ext[0] == '\0') {
        out[0] = 'F';
        out[1] = 'I';
        out[2] = 'L';
        out[3] = '\0';
        return;
    }

    if (ext[0] == 'E' && ext[1] == 'L' && ext[2] == 'F' && ext[3] == '\0') {
        out[0] = 'E';
        out[1] = 'X';
        out[2] = 'E';
        out[3] = '\0';
        return;
    }
    if ((ext[0] == 'T' && ext[1] == 'X' && ext[2] == 'T' && ext[3] == '\0')
        || (ext[0] == 'M' && ext[1] == 'D' && ext[2] == '\0')) {
        out[0] = 'T';
        out[1] = 'X';
        out[2] = 'T';
        out[3] = '\0';
        return;
    }
    if ((ext[0] == 'B' && ext[1] == 'I' && ext[2] == 'N' && ext[3] == '\0')
        || (ext[0] == 'D' && ext[1] == 'A' && ext[2] == 'T' && ext[3] == '\0')) {
        out[0] = 'B';
        out[1] = 'I';
        out[2] = 'N';
        out[3] = '\0';
        return;
    }
    if ((ext[0] == 'B' && ext[1] == 'M' && ext[2] == 'P' && ext[3] == '\0')
        || (ext[0] == 'P' && ext[1] == 'N' && ext[2] == 'G' && ext[3] == '\0')) {
        out[0] = 'I';
        out[1] = 'M';
        out[2] = 'G';
        out[3] = '\0';
        return;
    }

    out[0] = ext[0];
    out[1] = ext[1] ? ext[1] : ' ';
    out[2] = ext[2] ? ext[2] : ' ';
    out[3] = '\0';
}

static unsigned int desktop_item_color(const desktop_item_t* item) {
    const char* ext = 0;
    int i;

    if (!item) {
        return ui_rgb(192, 192, 192);
    }
    if (item->is_dir) {
        return ui_rgb(224, 192, 64);
    }

    for (i = 0; item->name[i] != '\0'; i++) {
        if (item->name[i] == '.') {
            ext = &item->name[i + 1];
        }
    }

    if (!ext) {
        return ui_rgb(160, 160, 160);
    }
    if (ext[0] == 'E' && ext[1] == 'L' && ext[2] == 'F' && ext[3] == '\0') {
        return ui_rgb(64, 128, 255);
    }
    if ((ext[0] == 'T' && ext[1] == 'X' && ext[2] == 'T' && ext[3] == '\0')
        || (ext[0] == 'M' && ext[1] == 'D' && ext[2] == '\0')) {
        return ui_rgb(128, 224, 128);
    }
    if ((ext[0] == 'B' && ext[1] == 'I' && ext[2] == 'N' && ext[3] == '\0')
        || (ext[0] == 'D' && ext[1] == 'A' && ext[2] == 'T' && ext[3] == '\0')) {
        return ui_rgb(192, 192, 192);
    }
    if ((ext[0] == 'B' && ext[1] == 'M' && ext[2] == 'P' && ext[3] == '\0')
        || (ext[0] == 'P' && ext[1] == 'N' && ext[2] == 'G' && ext[3] == '\0')) {
        return ui_rgb(224, 96, 192);
    }
    return ui_rgb(160, 160, 160);
}

static void desktop_item_display_name(const desktop_item_t* item, char out[13]) {
    int i;
    int j = 0;

    if (!out) {
        return;
    }

    out[0] = '\0';
    if (!item) {
        return;
    }

    for (i = 0; item->name[i] != '\0' && j < 12; i++) {
        if (item->name[i] == '.') {
            break;
        }
        out[j++] = item->name[i];
    }
    out[j] = '\0';
}

static void draw_text_transparent_clipped(const minidos_app_api_t* api, int x, int y, const char* text,
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

static void layout_desktop_items(demo_state_t* state) {
    int i;
    int columns;
    int left;
    int top;

    if (!state) {
        return;
    }

    columns = (state->sw - 24) / DESKTOP_CELL_W;
    if (columns < 1) {
        columns = 1;
    }
    if (columns > 6) {
        columns = 6;
    }

    left = 16;
    top = 18;

    for (i = 0; i < state->desktop_item_count; i++) {
        int col = i % columns;
        int row = i / columns;

        state->desktop_items[i].bounds = ui_rect_make(
            left + (col * DESKTOP_CELL_W),
            top + (row * DESKTOP_CELL_H),
            DESKTOP_CELL_W - 6,
            DESKTOP_CELL_H - 6);
    }
}

static void load_desktop_items(const minidos_app_api_t* api, demo_state_t* state) {
    int i;
    int count = 0;

    if (!api || !state) {
        return;
    }

    enter_desktop_home(api);
    for (i = 0; i < DESKTOP_MAX_ITEMS; i++) {
        char name[13];
        int is_dir = 0;

        name[0] = '\0';
        if (!app_list_entry(api, (unsigned int)i, name, &is_dir)) {
            break;
        }
        if (name[0] == '\0'
            || (name[0] == '.' && name[1] == '\0')
            || (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
            continue;
        }
        str_copy(state->desktop_items[count].name, name, (int)sizeof(state->desktop_items[count].name));
        state->desktop_items[count].is_dir = is_dir;
        state->desktop_items[count].bounds = ui_rect_make(0, 0, 0, 0);
        count++;
    }
    state->desktop_item_count = count;
    layout_desktop_items(state);
}

static void draw_desktop_item(const minidos_app_api_t* api, const demo_state_t* state,
    const desktop_item_t* item, ui_rect_t clip) {
    ui_rect_t icon_rect;
    ui_rect_t label_rect;
    ui_rect_t body_rect;
    char display_name[13];
    char tag[4];
    unsigned int icon_color;

    if (!api || !state || !item) {
        return;
    }

    body_rect = item->bounds;
    if (ui_rect_is_empty(ui_rect_intersect(body_rect, clip))) {
        return;
    }

    icon_color = desktop_item_color(item);
    desktop_item_icon_tag(item, tag);
    desktop_item_display_name(item, display_name);

    icon_rect = ui_rect_make(body_rect.x + 6, body_rect.y + 4, DESKTOP_ICON_W, DESKTOP_ICON_H);
    label_rect = ui_rect_make(body_rect.x, body_rect.y + DESKTOP_ICON_H + 10, body_rect.w, UI_CHAR_H);

    ui_frame_rect_clipped(api, icon_rect, icon_color, clip);
    ui_fill_rect_clipped(api, ui_rect_make(icon_rect.x + 1, icon_rect.y + 1, icon_rect.w - 2, 4), state->wm.theme.light, clip);
    draw_text_transparent_clipped(api, icon_rect.x + 8, icon_rect.y + 9, tag,
        state->wm.theme.dark_shadow, clip);

    draw_text_transparent_clipped(api, label_rect.x, label_rect.y, display_name,
        state->wm.theme.text, clip);
}

static void draw_desktop_items(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t clip) {
    int i;

    if (!api || !state) {
        return;
    }

    for (i = 0; i < state->desktop_item_count; i++) {
        draw_desktop_item(api, state, &state->desktop_items[i], clip);
    }
}

static void dismiss_start_menu(demo_state_t* state);

static void close_main_window(demo_state_t* state) {
    if (!state) {
        return;
    }

    dismiss_start_menu(state);
    ui_wm_close_window(&state->wm, state->window_id);
}

static void redraw_region(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t rect) {
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
            if (!state->wm.windows[i].visible || drawn[i]) {
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
                ui_draw_text_clipped(api, abs_bounds.x, abs_bounds.y,
                    control->text ? control->text : "",
                    state->wm.theme.text, state->wm.theme.field_bg, rect);
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
                ui_draw_listview(api, &g_ui_listview_line_buf, abs_bounds, control->listview, &state->wm.theme);
            }
        }
    }
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

static ui_rect_t taskbar_clock_rect(const demo_state_t* state) {
    ui_rect_t taskbar;

    if (!state) {
        return ui_rect_make(0, 0, 0, 0);
    }

    taskbar = taskbar_rect(state);
    return ui_rect_make(state->sw - 88, taskbar.y + 4, 80, 20);
}

static int main_window_is_visible(const demo_state_t* state) {
    const ui_wm_window_t* win;

    if (!state) {
        return 0;
    }

    win = ui_wm_find_window_const(&state->wm, state->window_id);
    return win && win->visible;
}

static ui_rect_t cursor_rect_at(int x, int y) {
    return ui_rect_make(x - UI_CURSOR_HOTSPOT_X,
        y - UI_CURSOR_HOTSPOT_Y,
        UI_CURSOR_BITMAP_WIDTH,
        UI_CURSOR_BITMAP_HEIGHT);
}

static ui_rect_t current_window_rect(const demo_state_t* state) {
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

static ui_rect_t current_title_bar_rect(const demo_state_t* state) {
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

static int cursor_touches_title_bar(const demo_state_t* state,
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

static ui_rect_t current_drag_preview_rect(const demo_state_t* state) {
    if (!state || !state->dragging) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return state->drag_preview_bounds;
}

static void add_window_damage_for_cursor(ui_dirty_list_t* dirty,
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

static void clamp_rect_to_desktop(const demo_state_t* state, ui_rect_t* rect) {
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

static void draw_drag_outline(const minidos_app_api_t* api, const demo_state_t* state) {
    if (!api || !state || !state->dragging || ui_rect_is_empty(state->drag_preview_bounds)) {
        return;
    }

    ui_frame_rect(api, state->drag_preview_bounds, state->wm.theme.selection_bg);
    if (state->drag_preview_bounds.w > 4 && state->drag_preview_bounds.h > 4) {
        ui_frame_rect(api, ui_rect_inset(state->drag_preview_bounds, 2), state->wm.theme.light);
    }
}

static void draw_start_menu_item(const minidos_app_api_t* api, const demo_state_t* state,
    int item_id, const char* label, unsigned int icon_color) {
    const start_menu_entry_t* entry;
    ui_rect_t item_rect;
    ui_rect_t icon_rect;
    ui_rect_t icon_inner;
    ui_rect_t arrow_rect;
    const ui_theme_t* theme;
    unsigned int bg;
    unsigned int fg;
    unsigned int detail_fg;
    int pressed;
    int hot;

    if (!api || !state || !label) {
        return;
    }
    if (item_id < 0 || item_id >= START_MENU_ITEM_COUNT) {
        return;
    }

    theme = &state->wm.theme;
    entry = &g_start_menu_items[item_id];
    item_rect = start_menu_item_rect(state, item_id);
    pressed = (state->start_menu_pressed_item == item_id);
    hot = (state->start_menu_hot_item == item_id);
    bg = theme->face;
    fg = theme->text;
    detail_fg = theme->shadow;

    if (hot) {
        bg = theme->selection_bg;
        fg = theme->selection_text;
        detail_fg = ui_rgb(216, 224, 255);
    }
    ui_fill_rect(api, item_rect, bg);
    ui_bevel_rect(api, item_rect,
        pressed ? theme->shadow : theme->light,
        pressed ? theme->light : theme->dark_shadow);

    icon_rect = ui_rect_make(item_rect.x + 4, item_rect.y + 2, 18, 18);
    ui_draw_panel(api, theme, icon_rect, !pressed);
    icon_inner = ui_rect_inset(icon_rect, 2);
    ui_fill_rect(api, icon_inner, icon_color);
    ui_fill_rect(api, ui_rect_make(icon_inner.x, icon_inner.y, icon_inner.w, 3), theme->light);
    draw_text_transparent_clipped(api, icon_inner.x + ((icon_inner.w - UI_CHAR_W) / 2),
        icon_inner.y + ((icon_inner.h - UI_CHAR_H) / 2) + 1, entry->glyph,
        theme->dark_shadow, icon_inner);

    ui_draw_text(api, item_rect.x + 28, item_rect.y + 2, label, fg, bg);
    ui_draw_text(api, item_rect.x + 28, item_rect.y + 12, entry->detail, detail_fg, bg);

    arrow_rect = ui_rect_make(item_rect.x + item_rect.w - 16, item_rect.y + 5, 10, 10);
    if (hot) {
        ui_fill_rect(api, arrow_rect, ui_rgb(32, 64, 160));
        ui_frame_rect(api, arrow_rect, theme->title_active_text);
    }
    draw_text_transparent_clipped(api, arrow_rect.x + 1, arrow_rect.y + 1, ">",
        hot ? theme->title_active_text : theme->shadow, item_rect);
}

static unsigned int blend_rgb(unsigned int start_color, unsigned int end_color, int step, int steps) {
    unsigned int sr;
    unsigned int sg;
    unsigned int sb;
    unsigned int er;
    unsigned int eg;
    unsigned int eb;
    unsigned int r;
    unsigned int g;
    unsigned int b;

    if (steps <= 1) {
        return end_color;
    }

    sr = (start_color >> 16) & 0xFFu;
    sg = (start_color >> 8) & 0xFFu;
    sb = start_color & 0xFFu;
    er = (end_color >> 16) & 0xFFu;
    eg = (end_color >> 8) & 0xFFu;
    eb = end_color & 0xFFu;

    r = (unsigned int)(sr + ((int)(er - sr) * step) / (steps - 1));
    g = (unsigned int)(sg + ((int)(eg - sg) * step) / (steps - 1));
    b = (unsigned int)(sb + ((int)(eb - sb) * step) / (steps - 1));
    return ui_rgb(r, g, b);
}

static void fill_vertical_gradient(const minidos_app_api_t* api, ui_rect_t rect,
    unsigned int top_color, unsigned int bottom_color) {
    int y;
    int steps;

    if (!api || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    steps = rect.h;
    for (y = 0; y < rect.h; y++) {
        ui_fill_rect(api, ui_rect_make(rect.x, rect.y + y, rect.w, 1),
            blend_rgb(top_color, bottom_color, y, steps));
    }
}

static void draw_vertical_text(const minidos_app_api_t* api, ui_rect_t rect, const char* text,
    unsigned int fg, unsigned int top_color, unsigned int bottom_color) {
    int i;
    int len;
    int scale;
    int char_extent;
    int char_gap;
    int text_h;
    int draw_y;

    if (!api || !text || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    len = ui_strlen(text);
    if (len <= 0) {
        return;
    }

    (void)top_color;
    (void)bottom_color;

    scale = 2;
    char_extent = UI_CHAR_W * scale;
    char_gap = scale;
    text_h = (len * char_extent) + ((len - 1) * char_gap);
    draw_y = rect.y;
    if (rect.h > text_h) {
        draw_y += (rect.h - text_h) / 2;
    }

    for (i = len - 1; i >= 0; i--) {
        int glyph_index;
        const unsigned char* glyph;
        int row;
        int col;
        int char_x;
        int char_y;
        int dest_x;
        int dest_y;

        if (text[i] == ' ') {
            draw_y += char_extent + char_gap;
            continue;
        }
        if (text[i] < 32 || text[i] > 126) {
            draw_y += char_extent + char_gap;
            continue;
        }

        glyph_index = text[i] - 32;
        glyph = ui_font_8x8[glyph_index];
        char_x = rect.x + (rect.w - char_extent) / 2;
        char_y = draw_y;

        /* Rotate each glyph 90 degrees and scale it so the banner reads vertically. */
        for (row = 0; row < UI_CHAR_H; row++) {
            unsigned char bits = glyph[row];
            for (col = 0; col < UI_CHAR_W; col++) {
                if (!(bits & (0x80u >> col))) {
                    continue;
                }
                dest_x = char_x + row * scale;
                dest_y = char_y + (UI_CHAR_W - 1 - col) * scale;
                ui_fill_rect(api, ui_rect_make(dest_x, dest_y, scale, scale), fg);
            }
        }
        draw_y += char_extent + char_gap;
    }
}

static void draw_logo_mark(const minidos_app_api_t* api, int x, int y, int scale, int pressed) {
    int offset;
    int cell;
    int gap;

    if (!api || scale <= 0) {
        return;
    }

    offset = pressed ? 1 : 0;
    cell = scale;
    gap = scale > 2 ? 1 : 0;

    ui_frame_rect(api, ui_rect_make(x + offset - 1, y + offset - 1,
        (cell * 2) + gap + 2, (cell * 2) + gap + 2), 0x000000u);
    ui_fill_rect(api, ui_rect_make(x + offset, y + offset, cell, cell), ui_rgb(214, 64, 64));
    ui_fill_rect(api, ui_rect_make(x + offset + cell + gap, y + offset, cell, cell), ui_rgb(40, 104, 214));
    ui_fill_rect(api, ui_rect_make(x + offset, y + offset + cell + gap, cell, cell), ui_rgb(48, 164, 92));
    ui_fill_rect(api, ui_rect_make(x + offset + cell + gap, y + offset + cell + gap, cell, cell), ui_rgb(236, 196, 52));
}

static void draw_start_button(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t rect) {
    const ui_theme_t* theme;
    ui_rect_t inner;
    unsigned int button_bg;
    int pressed;

    if (!api || !state) {
        return;
    }

    theme = &state->wm.theme;
    pressed = state->start_button_pressed || state->start_menu_open;

    ui_fill_rect(api, rect, theme->face);
    if (pressed) {
        ui_bevel_rect(api, rect, theme->shadow, theme->light);
        if (rect.w > 2 && rect.h > 2) {
            ui_bevel_rect(api, ui_rect_inset(rect, 1), theme->dark_shadow, theme->face_alt);
        }
    } else {
        ui_bevel_rect(api, rect, theme->light, theme->dark_shadow);
        if (rect.w > 2 && rect.h > 2) {
            ui_bevel_rect(api, ui_rect_inset(rect, 1), theme->face_alt, theme->shadow);
        }
    }

    inner = ui_rect_inset(rect, 3);
    button_bg = pressed ? theme->face_alt : theme->face;
    ui_fill_rect(api, inner, button_bg);
    ui_fill_rect(api, ui_rect_make(inner.x, inner.y, inner.w, 2),
        pressed ? theme->face : theme->light);
    draw_logo_mark(api, inner.x + 4, inner.y + 4, 3, pressed);
    ui_draw_text(api, inner.x + 16 + (pressed ? 1 : 0), inner.y + 4 + (pressed ? 1 : 0),
        "Iniciar", theme->text, button_bg);
}

static void draw_task_button(const minidos_app_api_t* api, const demo_state_t* state,
    ui_rect_t rect, const char* label) {
    const ui_theme_t* theme;
    ui_rect_t inner;
    unsigned int button_bg;

    if (!api || !state || !label) {
        return;
    }

    theme = &state->wm.theme;
    ui_draw_panel(api, theme, rect, 0);
    inner = ui_rect_inset(rect, 2);
    button_bg = ui_rgb(214, 219, 233);
    ui_fill_rect(api, inner, button_bg);
    ui_fill_rect(api, ui_rect_make(inner.x, inner.y, 5, inner.h), theme->title_active_bg);
    draw_logo_mark(api, inner.x + 8, inner.y + 5, 3, 0);
    ui_draw_text(api, inner.x + 24, inner.y + 5, label, theme->text, button_bg);
}

static void draw_menu_info_panel(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t rect) {
    const ui_theme_t* theme;
    ui_rect_t inner;
    ui_rect_t badge_rect;

    if (!api || !state) {
        return;
    }

    theme = &state->wm.theme;
    ui_draw_panel(api, theme, rect, 0);
    inner = ui_rect_inset(rect, 2);
    ui_fill_rect(api, inner, theme->face);
    ui_fill_rect(api, ui_rect_make(inner.x, inner.y, 5, inner.h), theme->title_active_bg);
    ui_draw_text(api, inner.x + 10, inner.y + 2, "Atalho", theme->shadow, theme->face);
    ui_draw_text(api, inner.x + 10, inner.y + 12, "ESC fecha", theme->text, theme->face);

    badge_rect = ui_rect_make(inner.x + inner.w - 72, inner.y + 3, 68, inner.h - 6);
    ui_draw_panel(api, theme, badge_rect, 1);
    ui_draw_label_centered(api, ui_rect_inset(badge_rect, 2), state->clock_text, theme->text, theme->face);
}

static void draw_start_menu(const minidos_app_api_t* api, const demo_state_t* state) {
    ui_rect_t menu;
    ui_rect_t strip;
    ui_rect_t divider;
    ui_rect_t info_rect;
    ui_rect_t exit_rect;
    ui_button_t exit_button;
    const ui_theme_t* theme;

    if (!api || !state || !state->start_menu_open) {
        return;
    }

    theme = &state->wm.theme;
    menu = start_menu_rect(state);
    strip = ui_rect_make(menu.x + 4, menu.y + 4, START_MENU_STRIP_W, menu.h - 8);
    info_rect = ui_rect_make(menu.x + START_MENU_STRIP_W + 14, menu.y + 114,
        menu.w - START_MENU_STRIP_W - 28, 26);
    divider = ui_rect_make(menu.x + START_MENU_STRIP_W + 12, menu.y + menu.h - 48, menu.w - START_MENU_STRIP_W - 24, 2);
    exit_rect = start_menu_exit_rect(state);

    ui_fill_rect(api, ui_rect_make(menu.x + 2, menu.y + 2, menu.w, menu.h), ui_rgb(48, 48, 48));
    ui_draw_panel(api, theme, menu, 1);
    fill_vertical_gradient(api, strip, ui_rgb(0, 0, 104), ui_rgb(0, 96, 192));
    draw_vertical_text(api, strip, "AIOS 95", theme->title_active_text, ui_rgb(0, 0, 104), ui_rgb(0, 96, 192));
    draw_logo_mark(api, strip.x + 5, strip.y + strip.h - 20, 5, 0);

    draw_start_menu_item(api, state, START_MENU_ITEM_PROGRAMAS, g_start_menu_items[START_MENU_ITEM_PROGRAMAS].label, g_start_menu_items[START_MENU_ITEM_PROGRAMAS].accent);
    draw_start_menu_item(api, state, START_MENU_ITEM_DOCUMENTOS, g_start_menu_items[START_MENU_ITEM_DOCUMENTOS].label, g_start_menu_items[START_MENU_ITEM_DOCUMENTOS].accent);
    draw_start_menu_item(api, state, START_MENU_ITEM_DEFINICOES, g_start_menu_items[START_MENU_ITEM_DEFINICOES].label, g_start_menu_items[START_MENU_ITEM_DEFINICOES].accent);
    draw_start_menu_item(api, state, START_MENU_ITEM_AJUDA, g_start_menu_items[START_MENU_ITEM_AJUDA].label, g_start_menu_items[START_MENU_ITEM_AJUDA].accent);

    draw_menu_info_panel(api, state, info_rect);

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
    ui_rect_t clock_rect;
    const ui_theme_t* theme;

    if (!api || !state) {
        return;
    }

    theme = &state->wm.theme;
    taskbar = taskbar_rect(state);
    start_rect = start_button_rect(state);
    task_rect = ui_rect_make(start_rect.x + start_rect.w + 6, taskbar.y + 4, 170, 20);
    clock_rect = taskbar_clock_rect(state);
    ui_draw_panel(api, theme, taskbar, 1);
    ui_fill_rect(api, ui_rect_make(taskbar.x + 2, taskbar.y + 2, taskbar.w - 4, 1), theme->light);
    ui_fill_rect(api, ui_rect_make(taskbar.x + 2, taskbar.y + taskbar.h - 3, taskbar.w - 4, 1), theme->shadow);

    draw_start_button(api, state, start_rect);

    if (main_window_is_visible(state)) {
        draw_task_button(api, state, task_rect, "MiniDOS 95 Demo");
    }

    ui_draw_panel(api, theme, clock_rect, 0);
    ui_fill_rect(api, ui_rect_inset(clock_rect, 2), ui_rgb(224, 224, 224));
    ui_draw_label_centered(api, ui_rect_inset(clock_rect, 2), state->clock_text, theme->text, ui_rgb(224, 224, 224));
}

static void render_clock_update(const minidos_app_api_t* api, const demo_state_t* state) {
    ui_rect_t clock_rect;
    ui_rect_t cursor_rect;

    if (!api || !state) {
        return;
    }

    clock_rect = taskbar_clock_rect(state);
    ui_draw_panel(api, &state->wm.theme, clock_rect, 0);
    ui_fill_rect(api, ui_rect_inset(clock_rect, 2), ui_rgb(224, 224, 224));
    ui_draw_label_centered(api, ui_rect_inset(clock_rect, 2), state->clock_text,
        state->wm.theme.text, ui_rgb(224, 224, 224));

    if (state->mouse.present) {
        cursor_rect = cursor_rect_at(state->mouse.x, state->mouse.y);
        if (!ui_rect_is_empty(ui_rect_intersect(clock_rect, cursor_rect))) {
            ui_draw_cursor(api, state->mouse.x, state->mouse.y,
                state->wm.theme.light, state->wm.theme.dark_shadow);
        }
    }

    ui_present(api);
}

static void render_partial_motion(const minidos_app_api_t* api,
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

    /* Layer compositor: all dirty regions first */
    for (i = 0; i < dirty.count; i++) {
        redraw_region(api, state, dirty.rects[i]);
    }

    /* Overlays: drawn once after all compositor passes to avoid
     * a later redraw_region call painting over an already-drawn overlay */
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

    /* Overlay: cursor (always on top) */
    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

static void render_partial_drag(const minidos_app_api_t* api,
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

    /* Layer compositor: all dirty regions first */
    for (i = 0; i < dirty.count; i++) {
        redraw_region(api, state, dirty.rects[i]);
    }

    /* Overlays: drawn once after all compositor passes */
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

    /* Overlay: drag outline */
    draw_drag_outline(api, state);

    /* Overlay: cursor (always on top) */
    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

static void render(const minidos_app_api_t* api, const demo_state_t* state) {
    if (!api || !state) {
        return;
    }

    redraw_region(api, state, ui_rect_make(0, 0, state->sw, state->sh));

    draw_taskbar_overlay(api, state);
    draw_start_menu(api, state);
    draw_drag_outline(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
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
    state->drag_preview_bounds = ui_rect_make(0, 0, 0, 0);
    state->start_menu_open = 0;
    state->start_button_pressed = 0;
    state->start_menu_pressed_item = START_MENU_ITEM_NONE;
    state->start_menu_hot_item = START_MENU_ITEM_NONE;
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
                    && !cursor_touches_title_bar(&state, previous_cursor_rect, current_cursor_rect)) {
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
            }
            need_full_render = 1;
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
