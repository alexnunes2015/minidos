#include "win95_demo.h"

static void draw_inset_field_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, ui_rect_t clip) {
    if (!api || !theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    ui_fill_rect_clipped(api, rect, theme->field_bg, clip);
    ui_bevel_rect_clipped(api, rect, theme->shadow, theme->light, clip);
    if (rect.w > 2 && rect.h > 2) {
        ui_bevel_rect_clipped(api, ui_rect_inset(rect, 1), theme->dark_shadow, theme->face_alt, clip);
    }
}

static void draw_status_panel_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, ui_rect_t clip) {
    if (!api || !theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    ui_draw_panel_clipped(api, theme, rect, 0, clip);
}

static void draw_text_centered_clipped(const minidos_app_api_t* api, ui_rect_t rect,
    const char* text, unsigned int fg, unsigned int bg, ui_rect_t clip) {
    int text_w;
    int draw_x;
    int draw_y;

    if (!api || !text) {
        return;
    }

    text_w = ui_strlen(text) * UI_CHAR_W;
    draw_x = rect.x;
    draw_y = rect.y;
    if (rect.w > text_w) {
        draw_x += (rect.w - text_w) / 2;
    }
    if (rect.h > UI_CHAR_H) {
        draw_y += (rect.h - UI_CHAR_H) / 2;
    }
    ui_draw_text_clipped(api, draw_x, draw_y, text, fg, bg, clip);
}

static void draw_text_right_clipped(const minidos_app_api_t* api, int x, int y,
    const char* text, unsigned int fg, unsigned int bg, int max_chars, ui_rect_t clip) {
    char visible[128];
    int len;
    int start = 0;
    int i;

    if (!api || !text || max_chars <= 0) {
        return;
    }

    len = ui_strlen(text);
    if (len > max_chars) {
        start = len - max_chars;
        len = max_chars;
    }
    if (len >= (int)sizeof(visible)) {
        start += len - ((int)sizeof(visible) - 1);
        len = (int)sizeof(visible) - 1;
    }
    for (i = 0; i < len; i++) {
        visible[i] = text[start + i];
    }
    visible[len] = '\0';
    ui_draw_text_clipped(api, x, y, visible, fg, bg, clip);
}

static void draw_button_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    const ui_button_t* button, ui_rect_t clip) {
    ui_rect_t inner;
    unsigned int fg;

    if (!api || !theme || !button) {
        return;
    }

    ui_fill_rect_clipped(api, button->bounds, theme->face, clip);
    if (button->pressed) {
        ui_bevel_rect_clipped(api, button->bounds, theme->shadow, theme->light, clip);
        if (button->bounds.w > 2 && button->bounds.h > 2) {
            ui_bevel_rect_clipped(api, ui_rect_inset(button->bounds, 1), theme->dark_shadow, theme->face_alt, clip);
        }
    } else {
        ui_bevel_rect_clipped(api, button->bounds, theme->light, theme->dark_shadow, clip);
        if (button->bounds.w > 2 && button->bounds.h > 2) {
            ui_bevel_rect_clipped(api, ui_rect_inset(button->bounds, 1), theme->face_alt, theme->shadow, clip);
        }
    }

    inner = ui_rect_inset(button->bounds, 3);
    ui_fill_rect_clipped(api, inner, theme->face, clip);
    fg = button->enabled ? theme->text : theme->text_disabled;
    if (button->focused && button->bounds.w > 8 && button->bounds.h > 8) {
        ui_frame_rect_clipped(api, ui_rect_inset(button->bounds, 4), theme->dark_shadow, clip);
    }
    draw_text_centered_clipped(api,
        ui_rect_make(inner.x + (button->pressed ? 1 : 0), inner.y + (button->pressed ? 1 : 0), inner.w, inner.h),
        button->label ? button->label : "", fg, theme->face, clip);
}

static void draw_text_box_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, const char* text, int focused, ui_rect_t clip) {
    unsigned int fill;
    int max_chars;
    int len = 0;
    int visible_len = 0;
    int cursor_x;
    unsigned int ticks;

    if (!api || !theme) {
        return;
    }

    ui_draw_panel_clipped(api, theme, rect, 0, clip);
    rect = ui_rect_inset(rect, 2);
    fill = focused ? theme->light : theme->field_bg;
    ui_fill_rect_clipped(api, rect, fill, clip);
    ui_frame_rect_clipped(api, rect, focused ? theme->selection_bg : theme->shadow, clip);
    if (text) {
        len = ui_strlen(text);
        max_chars = (rect.w - 8) / UI_CHAR_W;
        draw_text_right_clipped(api, rect.x + 4, rect.y + 4, text, theme->field_text, fill, max_chars, clip);
        if (max_chars > 0) {
            visible_len = len > max_chars ? max_chars : len;
        }
    }

    ticks = app_get_ticks(api);
    if (focused && (((ticks / 20u) & 1u) == 0u)) {
        cursor_x = rect.x + 4 + (visible_len * UI_CHAR_W);
        if (cursor_x > rect.x + rect.w - 3) {
            cursor_x = rect.x + rect.w - 3;
        }
        if (cursor_x < rect.x + 3) {
            cursor_x = rect.x + 3;
        }
        ui_fill_rect_clipped(api, ui_rect_make(cursor_x, rect.y + 3, 1, rect.h - 6), theme->field_text, clip);
    }
}

static void draw_checkbox_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, const char* label, int checked, int focused, int enabled, ui_rect_t clip) {
    ui_rect_t box = ui_rect_make(rect.x, rect.y + ((rect.h - 13) / 2), 13, 13);
    unsigned int fg;

    if (!api || !theme) {
        return;
    }

    fg = enabled ? theme->text : theme->text_disabled;
    ui_draw_panel_clipped(api, theme, box, 0, clip);
    ui_fill_rect_clipped(api, ui_rect_inset(box, 2), theme->field_bg, clip);
    if (checked) {
        ui_draw_text_clipped(api, box.x + 2, box.y + 2, "X", fg, theme->field_bg, clip);
    }
    if (focused) {
        ui_frame_rect_clipped(api, ui_rect_make(box.x - 2, box.y - 2, rect.w > 18 ? rect.w : 18, box.h + 4),
            theme->dark_shadow, clip);
    }
    if (label) {
        ui_draw_text_clipped(api, rect.x + 18, rect.y + ((rect.h - UI_CHAR_H) / 2), label, fg, theme->field_bg, clip);
    }
}

static void draw_radio_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, const char* label, int checked, int focused, int enabled, ui_rect_t clip) {
    ui_rect_t circle = ui_rect_make(rect.x, rect.y + ((rect.h - 13) / 2), 13, 13);
    unsigned int fg;

    if (!api || !theme) {
        return;
    }

    fg = enabled ? theme->text : theme->text_disabled;
    ui_fill_rect_clipped(api, circle, theme->face, clip);
    ui_frame_rect_clipped(api, circle, theme->dark_shadow, clip);
    ui_fill_rect_clipped(api, ui_rect_inset(circle, 1), theme->field_bg, clip);
    if (checked) {
        ui_fill_rect_clipped(api, ui_rect_make(circle.x + 4, circle.y + 4, 5, 5), fg, clip);
    }
    if (focused) {
        ui_frame_rect_clipped(api, ui_rect_make(circle.x - 2, circle.y - 2, rect.w > 18 ? rect.w : 18, circle.h + 4),
            theme->dark_shadow, clip);
    }
    if (label) {
        ui_draw_text_clipped(api, rect.x + 18, rect.y + ((rect.h - UI_CHAR_H) / 2), label, fg, theme->field_bg, clip);
    }
}

static void draw_dropdown_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, const char* const* items, int item_count, int selected_index,
    int expanded, int focused, int hot_index, ui_rect_t clip) {
    ui_rect_t arrow_rect;
    const char* text = "";
    int i;

    if (!api || !theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    if (items && selected_index >= 0 && selected_index < item_count && items[selected_index]) {
        text = items[selected_index];
    }

    ui_draw_panel_clipped(api, theme, rect, 0, clip);
    ui_fill_rect_clipped(api, ui_rect_inset(rect, 2), theme->field_bg, clip);
    arrow_rect = ui_rect_make(rect.x + rect.w - 20, rect.y + 2, 18, rect.h - 4);
    ui_draw_panel_clipped(api, theme, arrow_rect, 1, clip);
    ui_draw_text_clipped(api, arrow_rect.x + 5, arrow_rect.y + ((arrow_rect.h - UI_CHAR_H) / 2),
        expanded ? "^" : "v", theme->text, theme->face, clip);
    draw_text_right_clipped(api, rect.x + 4, rect.y + ((rect.h - UI_CHAR_H) / 2),
        text, theme->field_text, theme->field_bg, (rect.w - 28) / UI_CHAR_W, clip);

    if (focused) {
        ui_frame_rect_clipped(api, ui_rect_inset(rect, 3), theme->selection_bg, clip);
    }

    if (expanded && item_count > 0) {
        ui_rect_t popup = ui_dropdown_popup_rect(rect, item_count);
        ui_draw_panel_clipped(api, theme, popup, 1, clip);
        for (i = 0; i < item_count; i++) {
            ui_rect_t item_rect = ui_dropdown_item_rect(rect, i);
            int highlighted = (i == hot_index) || (hot_index < 0 && i == selected_index);
            unsigned int bg = highlighted ? theme->selection_bg : theme->field_bg;
            unsigned int fg = highlighted ? theme->selection_text : theme->field_text;
            ui_fill_rect_clipped(api, item_rect, bg, clip);
            ui_draw_text_clipped(api, item_rect.x + 4, item_rect.y + ((item_rect.h - UI_CHAR_H) / 2),
                (items && items[i]) ? items[i] : "", fg, bg, clip);
        }
    }
}

static void draw_menu_widget_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, const char* const* items, int item_count, int selected_index, int focused, ui_rect_t clip) {
    int i;

    if (!api || !theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    ui_draw_panel_clipped(api, theme, rect, 1, clip);
    for (i = 0; i < item_count; i++) {
        ui_rect_t item_rect = ui_menu_item_rect(rect, i);
        unsigned int bg = (i == selected_index) ? theme->selection_bg : theme->face;
        unsigned int fg = (i == selected_index) ? theme->selection_text : theme->text;

        if (item_rect.y + item_rect.h > rect.y + rect.h - 1) {
            break;
        }
        ui_fill_rect_clipped(api, item_rect, bg, clip);
        ui_draw_text_clipped(api, item_rect.x + 6, item_rect.y + ((item_rect.h - UI_CHAR_H) / 2),
            (items && items[i]) ? items[i] : "", fg, bg, clip);
    }

    if (focused) {
        ui_frame_rect_clipped(api, ui_rect_inset(rect, 3), theme->dark_shadow, clip);
    }
}

static void draw_scrollbar_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, int min_value, int max_value, int page_size, int value, int focused, ui_rect_t clip) {
    ui_rect_t dec_rect;
    ui_rect_t inc_rect;
    ui_rect_t track_rect;
    ui_rect_t thumb_rect;

    if (!api || !theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    dec_rect = ui_scrollbar_decrement_rect(rect);
    inc_rect = ui_scrollbar_increment_rect(rect);
    track_rect = ui_scrollbar_track_rect(rect);
    thumb_rect = ui_scrollbar_thumb_rect(rect, min_value, max_value, page_size, value);

    ui_draw_panel_clipped(api, theme, rect, 1, clip);
    ui_fill_rect_clipped(api, track_rect, theme->face_alt, clip);
    ui_draw_panel_clipped(api, theme, dec_rect, 1, clip);
    ui_draw_panel_clipped(api, theme, inc_rect, 1, clip);
    draw_text_centered_clipped(api, dec_rect, "^", theme->text, theme->face, clip);
    draw_text_centered_clipped(api, inc_rect, "v", theme->text, theme->face, clip);
    ui_draw_panel_clipped(api, theme, thumb_rect, 1, clip);

    if (focused) {
        ui_frame_rect_clipped(api, ui_rect_inset(rect, 2), theme->selection_bg, clip);
    }
}

static void draw_window_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    const ui_window_t* window, ui_rect_t clip) {
    ui_rect_t title_rect;
    ui_rect_t client_rect;
    ui_button_t button;
    unsigned int title_bg;
    unsigned int title_fg;
    int title_text_x = 6;

    if (!api || !theme || !window) {
        return;
    }

    if (!window->maximized) {
        ui_draw_panel_clipped(api, theme, window->bounds, 1, clip);
    } else {
        ui_fill_rect_clipped(api, window->bounds, theme->face, clip);
    }

    title_rect = ui_window_title_bar_rect(window);
    title_bg = window->active ? theme->title_active_bg : theme->title_inactive_bg;
    title_fg = window->active ? theme->title_active_text : theme->title_inactive_text;
    ui_fill_rect_clipped(api, title_rect, title_bg, clip);

    if (window->icon_id != UI_WINDOW_ICON_NONE) {
        ui_rect_t icon = ui_rect_make(title_rect.x + 2, title_rect.y + 1, 14, 14);
        if (window->icon_id == UI_WINDOW_ICON_FOLDER) {
            ui_fill_rect_clipped(api, ui_rect_make(icon.x + 1, icon.y + 5, icon.w - 2, icon.h - 6),
                ui_rgb(236, 196, 52), clip);
            ui_fill_rect_clipped(api, ui_rect_make(icon.x + 2, icon.y + 3, 6, 3),
                ui_rgb(244, 212, 96), clip);
            ui_frame_rect_clipped(api, ui_rect_make(icon.x + 1, icon.y + 5, icon.w - 2, icon.h - 6),
                ui_rgb(160, 128, 32), clip);
            ui_frame_rect_clipped(api, ui_rect_make(icon.x + 2, icon.y + 3, 6, 3),
                ui_rgb(160, 128, 32), clip);
        } else {
            ui_fill_rect_clipped(api, ui_rect_inset(icon, 1), ui_rgb(0, 96, 192), clip);
            ui_frame_rect_clipped(api, icon, theme->dark_shadow, clip);
            ui_frame_rect_clipped(api, ui_rect_inset(icon, 1), theme->light, clip);
        }
        title_text_x = 18;
    }

    {
        /* Keep long titles inside the title bar and clear of the caption buttons. */
        ui_rect_t title_text_clip = title_rect;
        int reserved = 0;

        if (window->has_close_button && title_rect.w >= 20) {
            reserved = 20;
        }
        if ((window->has_minimize_button || window->has_maximize_button) && title_rect.w >= 56) {
            reserved = 56;
        }
        title_text_clip.w -= reserved;
        if (title_text_clip.w < 0) {
            title_text_clip.w = 0;
        }
        ui_draw_text_clipped(api, title_rect.x + title_text_x, title_rect.y + 4,
            window->title ? window->title : "", title_fg, title_bg,
            ui_rect_intersect(title_text_clip, clip));
    }

    client_rect = ui_window_client_rect(window);
    ui_fill_rect_clipped(api, client_rect, theme->field_bg, clip);

    if (window->has_minimize_button && title_rect.w >= 56) {
        button.bounds = ui_window_minimize_button_rect(window);
        button.label = "_";
        button.pressed = 0;
        button.focused = 0;
        button.enabled = 1;
        draw_button_clipped(api, theme, &button, clip);
    }

    if (window->has_maximize_button && title_rect.w >= 56) {
        button.bounds = ui_window_maximize_button_rect(window);
        button.label = window->maximized ? "2" : "^";
        button.pressed = 0;
        button.focused = 0;
        button.enabled = 1;
        draw_button_clipped(api, theme, &button, clip);
    }

    if (window->has_close_button && title_rect.w >= 20) {
        button.bounds = ui_window_close_button_rect(window);
        button.label = "X";
        button.pressed = 0;
        button.focused = 0;
        button.enabled = 1;
        draw_button_clipped(api, theme, &button, clip);
    }
}

static void append_text(char* out, int max_len, const char* text) {
    int pos = 0;

    if (!out || max_len <= 1 || !text) {
        return;
    }

    while (pos < (max_len - 1) && out[pos] != '\0') {
        pos++;
    }
    while (pos < (max_len - 1) && *text != '\0') {
        out[pos++] = *text++;
    }
    out[pos] = '\0';
}

static void set_prefixed_line(char* out, int out_len, const char* prefix, const char* value) {
    if (!out || out_len <= 1) {
        return;
    }
    out[0] = '\0';
    str_copy(out, prefix ? prefix : "", out_len);
    append_text(out, out_len, value ? value : "");
}

static void draw_explorer_chrome_clipped(const minidos_app_api_t* api, const demo_state_t* state,
    const ui_wm_window_t* win, ui_rect_t clip) {
    const explorer_state_t* explorer;
    const ui_listview_item_t* selected_item = 0;
    const ui_theme_t* theme;
    ui_rect_t client_abs;
    ui_rect_t client;
    ui_rect_t menu;
    ui_rect_t toolbar;
    ui_rect_t addr;
    ui_rect_t sidebar;
    ui_rect_t menu_clip;
    ui_rect_t toolbar_clip;
    ui_rect_t sidebar_text_clip;
    ui_rect_t divider;
    char line1[52];
    char line2[52];
    char line3[52];
    char line4[52];

    if (!api || !state || !win) {
        return;
    }

    explorer = explorer_for_window((demo_state_t*)state, win->id);
    theme = &state->wm.theme;
    client_abs = ui_window_client_rect(&win->window);
    if (ui_rect_is_empty(ui_rect_intersect(client_abs, clip))) {
        return;
    }

    client = ui_rect_make(client_abs.x, client_abs.y, client_abs.w, client_abs.h);
    menu = ui_rect_make(client.x, client.y, client.w, EXPLORER_MENU_H);
    toolbar = ui_rect_make(client.x, client.y + EXPLORER_MENU_H, client.w, EXPLORER_TOOLBAR_H);
    addr = ui_rect_make(client.x, client.y + EXPLORER_MENU_H + EXPLORER_TOOLBAR_H, client.w, EXPLORER_ADDR_H);
    sidebar = ui_rect_make(client.x + 8, client.y + EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + EXPLORER_ADDR_H + 6,
        EXPLORER_SIDEBAR_W, client.h - (EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + EXPLORER_ADDR_H) - 34);
    menu_clip = ui_rect_intersect(menu, clip);
    toolbar_clip = ui_rect_intersect(toolbar, clip);

    ui_fill_rect_clipped(api, menu, theme->face, clip);
    ui_fill_rect_clipped(api, ui_rect_make(menu.x, menu.y + menu.h - 1, menu.w, 1), theme->shadow, clip);
    ui_draw_text_clipped(api, menu.x + 8, menu.y + 5, "File   Edit   View   Go   Favorites   Help",
        theme->text, theme->face, menu_clip);

    ui_draw_panel_clipped(api, theme, toolbar, 1, clip);
    divider = ui_rect_make(toolbar.x + 90, toolbar.y + 3, 2, toolbar.h - 6);
    ui_fill_rect_clipped(api, ui_rect_make(divider.x, divider.y, 1, divider.h), theme->shadow, clip);
    ui_fill_rect_clipped(api, ui_rect_make(divider.x + 1, divider.y, 1, divider.h), theme->light, clip);
    ui_draw_text_clipped(api, divider.x + 8, toolbar.y + 6, "Navegacao",
        theme->text, theme->face, toolbar_clip);

    ui_fill_rect_clipped(api, addr, theme->face, clip);
    ui_fill_rect_clipped(api, ui_rect_make(addr.x, addr.y + addr.h - 1, addr.w, 1), theme->shadow, clip);

    if (sidebar.h > 0) {
        line1[0] = '\0';
        line2[0] = '\0';
        line3[0] = '\0';
        line4[0] = '\0';
        if (explorer && explorer->listview.selected_index >= 0
            && explorer->listview.selected_index < explorer->listview.item_count) {
            selected_item = &explorer->listview.items[explorer->listview.selected_index];
        }
        if (!explorer || explorer->showing_drives) {
            str_copy(line1, "Computer", (int)sizeof(line1));
            str_copy(line2, "Selecione um drive.", (int)sizeof(line2));
            str_copy(line3, "Back/Forward: [ ]", (int)sizeof(line3));
            str_copy(line4, "Enter abre.", (int)sizeof(line4));
        } else if (selected_item && selected_item->is_dir) {
            set_prefixed_line(line1, (int)sizeof(line1), "Local: ", explorer->path_text);
            set_prefixed_line(line2, (int)sizeof(line2), "Pasta: ", selected_item->name);
            str_copy(line3, "Enter abre a pasta.", (int)sizeof(line3));
            str_copy(line4, "Backspace sobe.", (int)sizeof(line4));
        } else if (selected_item) {
            set_prefixed_line(line1, (int)sizeof(line1), "Local: ", explorer->path_text);
            set_prefixed_line(line2, (int)sizeof(line2), "Ficheiro: ", selected_item->name);
            str_copy(line3, "Abertura de ficheiros", (int)sizeof(line3));
            str_copy(line4, "sera adicionada.", (int)sizeof(line4));
        } else {
            set_prefixed_line(line1, (int)sizeof(line1), "Local: ", explorer->path_text);
            str_copy(line2, "Selecione um item.", (int)sizeof(line2));
            str_copy(line3, "Duplo clique ou Enter.", (int)sizeof(line3));
            str_copy(line4, "Backspace sobe.", (int)sizeof(line4));
        }

        ui_draw_panel_clipped(api, theme, sidebar, 0, clip);
        sidebar_text_clip = ui_rect_intersect(ui_rect_inset(sidebar, 3), clip);
        ui_draw_text_clipped(api, sidebar.x + 10, sidebar.y + 10, line1,
            theme->text, theme->face, sidebar_text_clip);
        ui_draw_text_clipped(api, sidebar.x + 10, sidebar.y + 22, line2,
            theme->text, theme->face, sidebar_text_clip);
        ui_draw_text_clipped(api, sidebar.x + 10, sidebar.y + 34, line3,
            theme->shadow, theme->face, sidebar_text_clip);
        ui_draw_text_clipped(api, sidebar.x + 10, sidebar.y + 46, line4,
            theme->shadow, theme->face, sidebar_text_clip);
    }
}

static void draw_showcase_chrome_clipped(const minidos_app_api_t* api, const demo_state_t* state,
    const ui_wm_window_t* win, ui_rect_t clip) {
    ui_rect_t client;
    ui_rect_t client_clip;
    ui_rect_t left_group;
    ui_rect_t right_group;
    ui_rect_t notes;

    if (!api || !state || !win || win->id != state->window_id) {
        return;
    }

    client = ui_window_client_rect(&win->window);
    if (ui_rect_is_empty(ui_rect_intersect(client, clip))) {
        return;
    }
    client_clip = ui_rect_intersect(client, clip);

    left_group = ui_rect_make(client.x + 8, client.y + 104, 200, 152);
    right_group = ui_rect_make(client.x + 228, client.y + 104, 190, 152);
    notes = ui_rect_make(client.x + 8, client.y + 8, client.w - 16, 42);

    left_group = ui_rect_intersect(left_group, client);
    right_group = ui_rect_intersect(right_group, client);
    notes = ui_rect_intersect(notes, client);

    if (!ui_rect_is_empty(left_group)) {
        ui_draw_panel_clipped(api, &state->wm.theme, left_group, 0, client_clip);
    }
    if (!ui_rect_is_empty(right_group)) {
        ui_draw_panel_clipped(api, &state->wm.theme, right_group, 0, client_clip);
    }
    if (!ui_rect_is_empty(notes)) {
        ui_draw_panel_clipped(api, &state->wm.theme, notes, 0, client_clip);
    }

    if (!ui_rect_is_empty(left_group)) {
        ui_draw_text_clipped(api, left_group.x + 8, left_group.y + 8, "Checks e Menu",
            state->wm.theme.text, state->wm.theme.face, client_clip);
    }
    if (!ui_rect_is_empty(right_group)) {
        ui_draw_text_clipped(api, right_group.x + 8, right_group.y + 8, "Radio, Combo e Scroll",
            state->wm.theme.text, state->wm.theme.face, client_clip);
    }
    if (!ui_rect_is_empty(notes)) {
        ui_draw_text_clipped(api, notes.x + 8, notes.y + 8, "Janela de validacao dos widgets da runtime UI.",
            state->wm.theme.text, state->wm.theme.face, client_clip);
        ui_draw_text_clipped(api, notes.x + 8, notes.y + 24, "TAB move o foco; ENTER/ESPACO ativam o controlo focado.",
            state->wm.theme.text, state->wm.theme.face, client_clip);
    }
}

void redraw_region(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t rect) {
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
            if (!state->wm.windows[i].visible || state->wm.windows[i].window.minimized || drawn[i]) {
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
            draw_window_clipped(api, &state->wm.theme, &win->window, rect);
        } else {
            ui_fill_rect(api, ui_rect_intersect(rect, client_rect), state->wm.theme.field_bg);
        }

        {
            const explorer_state_t* explorer = explorer_for_window((demo_state_t*)state, win->id);
            if (explorer) {
                draw_explorer_chrome_clipped(api, state, win, rect);
            } else if (win->id == state->window_id) {
                draw_showcase_chrome_clipped(api, state, win, rect);
            }
        }

        for (c = 0; c < state->wm.control_count; c++) {
            const ui_control_t* control = &state->wm.controls[c];
            ui_rect_t abs_bounds;
            ui_rect_t text_clip;

            if (!control->visible || control->window_id != win->id) {
                continue;
            }
            abs_bounds = ui_wm_control_visible_bounds(&state->wm, control);
            if (ui_rect_is_empty(ui_rect_intersect(rect, abs_bounds))) {
                continue;
            }

            if (control->type == UI_CONTROL_LABEL) {
                const explorer_state_t* explorer = explorer_for_control((demo_state_t*)state, control->id);
                int text_y = abs_bounds.y;

                if (abs_bounds.h > UI_CHAR_H) {
                    text_y += (abs_bounds.h - UI_CHAR_H) / 2;
                }

                if (explorer && control->id == explorer->path_value_id) {
                    draw_inset_field_clipped(api, &state->wm.theme, abs_bounds, rect);
                    text_clip = ui_rect_intersect(ui_rect_inset(abs_bounds, 2), rect);
                    ui_draw_text_clipped(api, abs_bounds.x + 4, text_y,
                        control->text ? control->text : "",
                        state->wm.theme.field_text, state->wm.theme.field_bg, text_clip);
                } else if (explorer && (control->id == explorer->status_label_id
                    || control->id == explorer->status_count_id
                    || control->id == explorer->status_extra_id)) {
                    draw_status_panel_clipped(api, &state->wm.theme, abs_bounds, rect);
                    text_clip = ui_rect_intersect(ui_rect_inset(abs_bounds, 2), rect);
                    ui_draw_text_clipped(api, abs_bounds.x + 4, text_y,
                        control->text ? control->text : "",
                        state->wm.theme.text, state->wm.theme.face, text_clip);
                } else {
                    text_clip = ui_rect_intersect(abs_bounds, rect);
                    ui_draw_text_clipped(api, abs_bounds.x, abs_bounds.y,
                        control->text ? control->text : "",
                        state->wm.theme.text, state->wm.theme.field_bg, text_clip);
                }
            } else if (control->type == UI_CONTROL_BUTTON) {
                ui_button_t button;
                button.bounds = abs_bounds;
                button.label = control->text ? control->text : "";
                button.pressed = control->pressed;
                button.focused = control->focused;
                button.enabled = control->enabled;
                draw_button_clipped(api, &state->wm.theme, &button, rect);
            } else if (control->type == UI_CONTROL_TEXTINPUT) {
                draw_text_box_clipped(api, &state->wm.theme, ui_wm_control_visible_bounds(&state->wm, control),
                    control->text ? control->text : "", control->focused, rect);
            } else if (control->type == UI_CONTROL_LISTVIEW && control->listview) {
                const explorer_state_t* explorer = explorer_for_control((demo_state_t*)state, control->id);
                if (explorer) {
                    draw_explorer_grid(api, state, explorer, ui_wm_control_visible_bounds(&state->wm, control), rect);
                } else {
                    ui_draw_listview_clipped(api, &g_ui_listview_line_buf,
                        ui_wm_control_visible_bounds(&state->wm, control),
                        control->listview, &state->wm.theme, rect);
                }
            } else if (control->type == UI_CONTROL_CHECKBOX) {
                draw_checkbox_clipped(api, &state->wm.theme, ui_wm_control_visible_bounds(&state->wm, control),
                    control->text ? control->text : "", control->checked, control->focused, control->enabled, rect);
            } else if (control->type == UI_CONTROL_RADIO) {
                draw_radio_clipped(api, &state->wm.theme, ui_wm_control_visible_bounds(&state->wm, control),
                    control->text ? control->text : "", control->checked, control->focused, control->enabled, rect);
            } else if (control->type == UI_CONTROL_DROPDOWN) {
                /* Clip to the collapsed box here: an open popup is painted by
                 * the dedicated pass below (above sibling controls), so
                 * drawing it now would just be overdrawn and wasted. */
                ui_rect_t box_bounds = ui_wm_control_abs_bounds(&state->wm, control);
                draw_dropdown_clipped(api, &state->wm.theme, box_bounds,
                    control->items, control->item_count, control->selected_index, control->open, control->focused,
                    control->hot_index, ui_rect_intersect(ui_rect_intersect(rect, client_rect), box_bounds));
            } else if (control->type == UI_CONTROL_MENU) {
                draw_menu_widget_clipped(api, &state->wm.theme, ui_wm_control_visible_bounds(&state->wm, control),
                    control->items, control->item_count, control->selected_index, control->focused, rect);
            } else if (control->type == UI_CONTROL_SCROLLBAR) {
                draw_scrollbar_clipped(api, &state->wm.theme, ui_wm_control_visible_bounds(&state->wm, control),
                    control->min_value, control->max_value, control->page_size, control->value, control->focused, rect);
            }
        }

        /* Draw expanded dropdown popups last so they stay above sibling controls. */
        for (c = 0; c < state->wm.control_count; c++) {
            const ui_control_t* control = &state->wm.controls[c];
            ui_rect_t popup_bounds;

            if (!control->visible || control->window_id != win->id
                || control->type != UI_CONTROL_DROPDOWN || !control->open) {
                continue;
            }

            popup_bounds = ui_wm_control_visible_bounds(&state->wm, control);
            if (ui_rect_is_empty(ui_rect_intersect(rect, popup_bounds))) {
                continue;
            }

            draw_dropdown_clipped(api, &state->wm.theme, ui_wm_control_abs_bounds(&state->wm, control),
                control->items, control->item_count, control->selected_index, control->open,
                control->focused, control->hot_index, ui_rect_intersect(rect, client_rect));
        }
    }
}

void render_clock_update(const minidos_app_api_t* api, const demo_state_t* state) {
    ui_rect_t clock_rect;
    ui_rect_t cursor_rect;

    if (!api || !state) {
        return;
    }

    clock_rect = taskbar_clock_rect(state);
    draw_taskbar_overlay_clipped(api, state, clock_rect);

    if (state->mouse.present) {
        cursor_rect = cursor_rect_at(state->mouse.x, state->mouse.y);
        if (!ui_rect_is_empty(ui_rect_intersect(clock_rect, cursor_rect))) {
            ui_draw_cursor(api, state->mouse.x, state->mouse.y,
                state->wm.theme.light, state->wm.theme.dark_shadow);
        }
    }

    ui_present(api);
}

static ui_rect_t resize_marker_rect_at(int x, int y) {
    return ui_rect_make(x + 10, y + 10, 54, 14);
}

void render_partial_motion(const minidos_app_api_t* api,
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
    if (state->resize_hover_edges || state->resize_edges) {
        ui_dirty_list_add(&dirty, resize_marker_rect_at(previous_mouse->x, previous_mouse->y));
        ui_dirty_list_add(&dirty, resize_marker_rect_at(state->mouse.x, state->mouse.y));
    }
    add_window_damage_for_cursor(&dirty, state, previous_cursor_rect, current_cursor_rect);

    bar_rect = taskbar_rect(state);
    menu_rect = start_menu_paint_rect(state);

    for (i = 0; i < dirty.count; i++) {
        redraw_region(api, state, dirty.rects[i]);
    }

    for (i = 0; i < dirty.count; i++) {
        if (!ui_rect_is_empty(ui_rect_intersect(dirty.rects[i], bar_rect))) {
            draw_taskbar_overlay_clipped(api, state, dirty.rects[i]);
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

    draw_resize_hint(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

void render_partial_drag(const minidos_app_api_t* api,
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
    if (state->resize_hover_edges || state->resize_edges) {
        ui_dirty_list_add(&dirty, resize_marker_rect_at(previous_mouse->x, previous_mouse->y));
        ui_dirty_list_add(&dirty, resize_marker_rect_at(state->mouse.x, state->mouse.y));
    }
    add_window_damage_for_cursor(&dirty, state, previous_cursor_rect, current_cursor_rect);

    bar_rect = taskbar_rect(state);
    menu_rect = start_menu_paint_rect(state);

    for (i = 0; i < dirty.count; i++) {
        redraw_region(api, state, dirty.rects[i]);
    }

    for (i = 0; i < dirty.count; i++) {
        if (!ui_rect_is_empty(ui_rect_intersect(dirty.rects[i], bar_rect))) {
            draw_taskbar_overlay_clipped(api, state, dirty.rects[i]);
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

    draw_resize_hint(api, state);
    draw_drag_outline(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

void render_dirty_regions(const minidos_app_api_t* api, const demo_state_t* state,
    const ui_dirty_list_t* dirty) {
    ui_rect_t bar_rect;
    ui_rect_t menu_rect;
    int draw_menu;
    int i;

    if (!api || !state || !dirty) {
        return;
    }

    bar_rect = taskbar_rect(state);
    menu_rect = start_menu_paint_rect(state);
    draw_menu = 0;

    for (i = 0; i < dirty->count; i++) {
        redraw_region(api, state, dirty->rects[i]);
        if (!ui_rect_is_empty(ui_rect_intersect(dirty->rects[i], bar_rect))) {
            draw_taskbar_overlay_clipped(api, state, dirty->rects[i]);
        }
        if (state->start_menu_open
            && !ui_rect_is_empty(ui_rect_intersect(dirty->rects[i], menu_rect))) {
            draw_menu = 1;
        }
    }

    if (draw_menu) {
        draw_start_menu(api, state);
    }

    draw_resize_hint(api, state);
    draw_drag_outline(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}

void render(const minidos_app_api_t* api, const demo_state_t* state) {
    if (!api || !state) {
        return;
    }

    redraw_region(api, state, ui_rect_make(0, 0, state->sw, state->sh));

    draw_taskbar_overlay(api, state);
    draw_start_menu(api, state);
    draw_resize_hint(api, state);
    draw_drag_outline(api, state);

    if (state->mouse.present) {
        ui_draw_cursor(api, state->mouse.x, state->mouse.y,
            state->wm.theme.light, state->wm.theme.dark_shadow);
    }

    ui_present(api);
}
