#include "win95_demo.h"
#include "win95_icons.h"

typedef struct {
    const char* label;
    const char* detail;
    int icon_id;
} start_menu_entry_t;

static const start_menu_entry_t g_start_menu_items[START_MENU_ITEM_COUNT] = {
    {"Programas", "Apps e demos", WIN95_ICON_TERMINAL},
    {"Documentos", "Ficheiros e notas", WIN95_ICON_NOTEPAD},
    {"Definicoes", "Tema e video", WIN95_ICON_SETTINGS},
    {"Ajuda", "Dicas rapidas", WIN95_ICON_COMPUTER},
};

void draw_drag_outline(const minidos_app_api_t* api, const demo_state_t* state) {
    if (!api || !state || !state->dragging || ui_rect_is_empty(state->drag_preview_bounds)) {
        return;
    }

    ui_frame_rect(api, state->drag_preview_bounds, state->wm.theme.selection_bg);
    if (state->drag_preview_bounds.w > 4 && state->drag_preview_bounds.h > 4) {
        ui_frame_rect(api, ui_rect_inset(state->drag_preview_bounds, 2), state->wm.theme.light);
    }
}

static void draw_start_menu_item(const minidos_app_api_t* api, const demo_state_t* state,
    int item_id, const char* label) {
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
    ui_fill_rect(api, icon_inner, hot ? ui_rgb(218, 228, 252) : theme->face);
    draw_win95_icon_clipped(api, icon_inner, entry->icon_id, icon_inner);

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
    ui_rect_t icon_rect;
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
    icon_rect = ui_rect_make(inner.x + 8, inner.y + 2, 16, 16);
    draw_win95_icon_clipped(api, icon_rect, WIN95_ICON_APP, inner);
    ui_draw_text(api, inner.x + 28, inner.y + 5, label, theme->text, button_bg);
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

void draw_start_menu(const minidos_app_api_t* api, const demo_state_t* state) {
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

    draw_start_menu_item(api, state, START_MENU_ITEM_PROGRAMAS, g_start_menu_items[START_MENU_ITEM_PROGRAMAS].label);
    draw_start_menu_item(api, state, START_MENU_ITEM_DOCUMENTOS, g_start_menu_items[START_MENU_ITEM_DOCUMENTOS].label);
    draw_start_menu_item(api, state, START_MENU_ITEM_DEFINICOES, g_start_menu_items[START_MENU_ITEM_DEFINICOES].label);
    draw_start_menu_item(api, state, START_MENU_ITEM_AJUDA, g_start_menu_items[START_MENU_ITEM_AJUDA].label);

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

void draw_taskbar_overlay(const minidos_app_api_t* api, const demo_state_t* state) {
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
    ui_fill_rect(api, taskbar, theme->face);
    ui_fill_rect(api, ui_rect_make(taskbar.x, taskbar.y, taskbar.w, 1), theme->light);
    ui_fill_rect(api, ui_rect_make(taskbar.x, taskbar.y + 1, taskbar.w, 1), theme->face_alt);

    draw_start_button(api, state, start_rect);

    if (main_window_is_visible(state)) {
        draw_task_button(api, state, task_rect, "MiniDOS 95 Demo");
    }

    ui_draw_panel(api, theme, clock_rect, 0);
    ui_fill_rect(api, ui_rect_inset(clock_rect, 2), ui_rgb(224, 224, 224));
    ui_draw_label_centered(api, ui_rect_inset(clock_rect, 2), state->clock_text, theme->text, ui_rgb(224, 224, 224));
}
