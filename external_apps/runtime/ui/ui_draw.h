#ifndef MINIDOS_UI_DRAW_H
#define MINIDOS_UI_DRAW_H

#include "ui_rect.h"
#include "ui_bitmap.h"

static inline unsigned int ui_rgb(unsigned int r, unsigned int g, unsigned int b) {
    return ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

static inline int ui_draw_bitmap(const minidos_app_api_t* api, const char* path, int dst_x, int dst_y, int dst_w, int dst_h) {
    ui_bitmap_cache_t* cache = &g_ui_bitmap_cache;
    const unsigned char* file_data;
    int draw_w;
    int draw_h;
    unsigned int src_w;
    int abs_src_h;
    int top_down;
    unsigned int row_stride;
    unsigned int bytes_per_pixel;
    const unsigned char* pixel_base;
    app_gfx_rect_t pixel_rect;

    /* Fast path: decode into wallpaper surface and blit in one syscall */
    if (ui_wallpaper_surface_load(api, path)) {
        draw_w = dst_w > 0 ? dst_w : g_ui_wallpaper_surface.width;
        draw_h = dst_h > 0 ? dst_h : g_ui_wallpaper_surface.height;
        return ui_wallpaper_surface_blit_scaled(api, dst_x, dst_y, draw_w, draw_h, -1, 0, 0, 0);
    }

    /* Slow fallback: pixel-by-pixel for images too large for the surface cache */
    if (!ui_bitmap_cache_load(api, path)) {
        return 0;
    }

    file_data = cache->data;
    src_w = (unsigned int)cache->src_w;
    abs_src_h = cache->abs_src_h;
    top_down = cache->top_down;
    row_stride = cache->row_stride;
    bytes_per_pixel = cache->bytes_per_pixel;
    pixel_base = file_data + cache->data_offset;

    draw_w = dst_w > 0 ? dst_w : (int)src_w;
    draw_h = dst_h > 0 ? dst_h : abs_src_h;
    if (draw_w <= 0 || draw_h <= 0) {
        return 0;
    }

    pixel_rect.w = 1;
    pixel_rect.h = 1;

    for (int dy = 0; dy < draw_h; dy++) {
        int src_y = (dy * abs_src_h) / draw_h;
        if (src_y >= abs_src_h) {
            src_y = abs_src_h - 1;
        }
        int row_index = top_down ? src_y : (abs_src_h - 1 - src_y);
        const unsigned char* row_ptr = pixel_base + (unsigned int)row_index * row_stride;

        for (int dx = 0; dx < draw_w; dx++) {
            int src_x = (dx * (int)src_w) / draw_w;
            if (src_x >= (int)src_w) {
                src_x = (int)src_w - 1;
            }

            const unsigned char* pixel = row_ptr + (unsigned int)src_x * bytes_per_pixel;
            unsigned char blue = pixel[0];
            unsigned char green = pixel[1];
            unsigned char red = pixel[2];
            unsigned char alpha = (bytes_per_pixel == 4) ? pixel[3] : 0xFF;

            if (bytes_per_pixel == 4) {
                if (alpha == 0) {
                    continue;
                }
            } else {
                if (ui_rgb(red, green, blue) == UI_BITMAP_TRANSPARENT_COLOR) {
                    continue;
                }
            }

            pixel_rect.x = dst_x + dx;
            pixel_rect.y = dst_y + dy;
            pixel_rect.color = ui_rgb(red, green, blue);
            (void)app_gfx_rect(api, &pixel_rect);
        }
    }

    return 1;
}

static inline int ui_draw_bitmap_clipped(const minidos_app_api_t* api, const char* path,
    int dst_x, int dst_y, int dst_w, int dst_h, ui_rect_t clip) {
    ui_bitmap_cache_t* cache = &g_ui_bitmap_cache;
    const unsigned char* pixel_base;
    ui_rect_t dest_rect;
    ui_rect_t draw_rect;
    unsigned int src_w;
    int abs_src_h;
    int top_down;
    unsigned int row_stride;
    unsigned int bytes_per_pixel;
    app_gfx_rect_t pixel_rect;
    int draw_w;
    int draw_h;

    /* Fast path: use surface blit with kernel-side clipping */
    if (ui_wallpaper_surface_load(api, path)) {
        draw_w = dst_w > 0 ? dst_w : g_ui_wallpaper_surface.width;
        draw_h = dst_h > 0 ? dst_h : g_ui_wallpaper_surface.height;
        return ui_wallpaper_surface_blit_scaled(api, dst_x, dst_y, draw_w, draw_h,
            clip.x, clip.y, clip.w, clip.h);
    }

    /* Slow fallback: pixel-by-pixel for images too large for the surface cache */
    if (!ui_bitmap_cache_load(api, path)) {
        return 0;
    }

    src_w = (unsigned int)cache->src_w;
    abs_src_h = cache->abs_src_h;
    top_down = cache->top_down;
    row_stride = cache->row_stride;
    bytes_per_pixel = cache->bytes_per_pixel;
    pixel_base = cache->data + cache->data_offset;
    dest_rect = ui_rect_make(dst_x, dst_y, dst_w > 0 ? dst_w : (int)src_w, dst_h > 0 ? dst_h : abs_src_h);
    draw_rect = ui_rect_intersect(dest_rect, clip);

    if (ui_rect_is_empty(draw_rect)) {
        return 1;
    }

    pixel_rect.w = 1;
    pixel_rect.h = 1;

    for (int y = draw_rect.y; y < draw_rect.y + draw_rect.h; y++) {
        int local_y = y - dest_rect.y;
        int src_y = (local_y * abs_src_h) / dest_rect.h;
        int row_index;
        const unsigned char* row_ptr;

        if (src_y >= abs_src_h) {
            src_y = abs_src_h - 1;
        }
        row_index = top_down ? src_y : (abs_src_h - 1 - src_y);
        row_ptr = pixel_base + (unsigned int)row_index * row_stride;

        for (int x = draw_rect.x; x < draw_rect.x + draw_rect.w; x++) {
            int local_x = x - dest_rect.x;
            int src_x = (local_x * (int)src_w) / dest_rect.w;
            const unsigned char* pixel;
            unsigned char blue;
            unsigned char green;
            unsigned char red;
            unsigned char alpha;

            if (src_x >= (int)src_w) {
                src_x = (int)src_w - 1;
            }

            pixel = row_ptr + (unsigned int)src_x * bytes_per_pixel;
            blue = pixel[0];
            green = pixel[1];
            red = pixel[2];
            alpha = (bytes_per_pixel == 4) ? pixel[3] : 0xFF;

            if (bytes_per_pixel == 4) {
                if (alpha == 0) {
                    continue;
                }
            } else if (ui_rgb(red, green, blue) == UI_BITMAP_TRANSPARENT_COLOR) {
                continue;
            }

            pixel_rect.x = x;
            pixel_rect.y = y;
            pixel_rect.color = ui_rgb(red, green, blue);
            (void)app_gfx_rect(api, &pixel_rect);
        }
    }

    return 1;
}

static inline int ui_mouse_left_down(const app_mouse_state_t* mouse) {
    return mouse && ((mouse->buttons & APP_MOUSE_LEFT) != 0);
}

static inline int ui_mouse_left_pressed(const app_mouse_state_t* prev, const app_mouse_state_t* cur) {
    return !ui_mouse_left_down(prev) && ui_mouse_left_down(cur);
}

static inline int ui_mouse_left_released(const app_mouse_state_t* prev, const app_mouse_state_t* cur) {
    return ui_mouse_left_down(prev) && !ui_mouse_left_down(cur);
}

static inline ui_theme_t ui_theme_classic(void) {
    ui_theme_t theme;
    theme.desktop_bg = 0x008080u;
    theme.desktop_accent = 0x004040u;
    theme.face = 0xC0C0C0u;
    theme.face_alt = 0xD4D0C8u;
    theme.light = 0xFFFFFFu;
    theme.shadow = 0x808080u;
    theme.dark_shadow = 0x000000u;
    theme.text = 0x000000u;
    theme.text_disabled = 0x808080u;
    theme.title_active_bg = 0x000080u;
    theme.title_active_text = 0xFFFFFFu;
    theme.title_inactive_bg = 0x808080u;
    theme.title_inactive_text = 0xD8D8D8u;
    theme.field_bg = 0xFFFFFFu;
    theme.field_text = 0x000000u;
    theme.selection_bg = 0x000080u;
    theme.selection_text = 0xFFFFFFu;
    theme.desktop_bg_bitmap = 0;
    return theme;
}

static inline int ui_screen_size(const minidos_app_api_t* api, int* out_w, int* out_h) {
    return app_gfx_size(api, out_w, out_h);
}

static inline void ui_clear(const minidos_app_api_t* api, unsigned int color) {
    (void)app_gfx_clear(api, color);
}

static inline void ui_present(const minidos_app_api_t* api) {
    (void)app_gfx_present(api);
}

static inline void ui_fill_rect(const minidos_app_api_t* api, ui_rect_t rect, unsigned int color) {
    app_gfx_rect_t gfx_rect;
    if (!api || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    gfx_rect.x = rect.x;
    gfx_rect.y = rect.y;
    gfx_rect.w = rect.w;
    gfx_rect.h = rect.h;
    gfx_rect.color = color;
    (void)app_gfx_rect(api, &gfx_rect);
}

static inline void ui_draw_text(const minidos_app_api_t* api, int x, int y, const char* text, unsigned int fg, unsigned int bg) {
    app_gfx_text_t gfx_text;
    if (!api || !text) {
        return;
    }
    gfx_text.x = x;
    gfx_text.y = y;
    gfx_text.text = text;
    gfx_text.fg = fg;
    gfx_text.bg = bg;
    (void)app_gfx_text(api, &gfx_text);
}

static const unsigned char ui_font_8x8[95][8];

static inline void ui_draw_char_cell_partially_clipped(const minidos_app_api_t* api, int x, int y, char ch,
    unsigned int fg, unsigned int bg, ui_rect_t clip) {
    ui_rect_t char_rect;
    ui_rect_t draw_rect;
    const unsigned char* glyph;
    int row;

    if (!api) {
        return;
    }

    char_rect = ui_rect_make(x, y, UI_CHAR_W, UI_CHAR_H);
    draw_rect = ui_rect_intersect(char_rect, clip);
    if (ui_rect_is_empty(draw_rect)) {
        return;
    }

    if (ch < 32 || ch > 126) {
        ch = ' ';
    }
    glyph = ui_font_8x8[ch - 32];

    ui_fill_rect(api, draw_rect, bg);
    for (row = 0; row < UI_CHAR_H; row++) {
        int py = y + row;
        unsigned char bits;
        int col = 0;

        if (py < draw_rect.y || py >= draw_rect.y + draw_rect.h) {
            continue;
        }

        bits = glyph[row];
        while (col < UI_CHAR_W) {
            int run_start;
            int run_len;

            while (col < UI_CHAR_W
                && (!(bits & (0x80u >> col))
                    || x + col < draw_rect.x
                    || x + col >= draw_rect.x + draw_rect.w)) {
                col++;
            }
            if (col >= UI_CHAR_W) {
                break;
            }

            run_start = col;
            run_len = 0;
            while (col < UI_CHAR_W
                && (bits & (0x80u >> col))
                && x + col >= draw_rect.x
                && x + col < draw_rect.x + draw_rect.w) {
                col++;
                run_len++;
            }
            if (run_len > 0) {
                ui_fill_rect(api, ui_rect_make(x + run_start, py, run_len, 1), fg);
            }
        }
    }
}

/* Clipped fill: only draws the intersection of rect and clip */
static inline void ui_fill_rect_clipped(const minidos_app_api_t* api, ui_rect_t rect, unsigned int color, ui_rect_t clip) {
    ui_fill_rect(api, ui_rect_intersect(rect, clip), color);
}

/* Clipped text: preserves the fast text syscall for fully visible cells and clips edge cells pixel-exactly. */
static inline void ui_draw_text_clipped(const minidos_app_api_t* api, int x, int y, const char* text,
    unsigned int fg, unsigned int bg, ui_rect_t clip) {
    int i;
    ui_rect_t char_rect;
    char single[2];

    if (!api || !text) { return; }

    single[1] = '\0';
    for (i = 0; text[i] != '\0'; i++) {
        char_rect = ui_rect_make(x + i * UI_CHAR_W, y, UI_CHAR_W, UI_CHAR_H);
        if (ui_rect_is_empty(ui_rect_intersect(char_rect, clip))) {
            continue;
        }
        if (!ui_rect_contains_rect(&clip, &char_rect)) {
            ui_draw_char_cell_partially_clipped(api, char_rect.x, char_rect.y, text[i], fg, bg, clip);
            continue;
        }
        single[0] = text[i];
        ui_draw_text(api, char_rect.x, char_rect.y, single, fg, bg);
    }
}

static inline void ui_frame_rect(const minidos_app_api_t* api, ui_rect_t rect, unsigned int color) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y, rect.w, 1), color);
    if (rect.h > 1) {
        ui_fill_rect(api, ui_rect_make(rect.x, rect.y + rect.h - 1, rect.w, 1), color);
    }
    if (rect.h > 2) {
        ui_fill_rect(api, ui_rect_make(rect.x, rect.y + 1, 1, rect.h - 2), color);
        if (rect.w > 1) {
            ui_fill_rect(api, ui_rect_make(rect.x + rect.w - 1, rect.y + 1, 1, rect.h - 2), color);
        }
    }
}

static inline void ui_bevel_rect(const minidos_app_api_t* api, ui_rect_t rect, unsigned int top_left, unsigned int bottom_right) {
    if (rect.w <= 1 || rect.h <= 1) {
        return;
    }
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y, rect.w - 1, 1), top_left);
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y, 1, rect.h - 1), top_left);
    ui_fill_rect(api, ui_rect_make(rect.x + rect.w - 1, rect.y, 1, rect.h), bottom_right);
    ui_fill_rect(api, ui_rect_make(rect.x, rect.y + rect.h - 1, rect.w, 1), bottom_right);
}

static inline void ui_draw_panel(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect, int raised) {
    if (!theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    ui_fill_rect(api, rect, theme->face);
    ui_bevel_rect(api, rect, raised ? theme->light : theme->shadow, raised ? theme->dark_shadow : theme->light);
    if (rect.w > 2 && rect.h > 2) {
        ui_bevel_rect(api, ui_rect_inset(rect, 1), raised ? theme->face_alt : theme->dark_shadow,
            raised ? theme->shadow : theme->face_alt);
    }
}

static inline void ui_draw_label(const minidos_app_api_t* api, int x, int y, const char* text, const ui_theme_t* theme, unsigned int bg) {
    unsigned int fg = theme ? theme->text : 0x000000u;
    ui_draw_text(api, x, y, text, fg, bg);
}

static inline void ui_draw_label_centered(const minidos_app_api_t* api, ui_rect_t rect, const char* text,
    unsigned int fg, unsigned int bg) {
    int text_w = ui_strlen(text) * UI_CHAR_W;
    int draw_x = rect.x;
    int draw_y = rect.y;

    if (rect.w > text_w) {
        draw_x += (rect.w - text_w) / 2;
    }
    if (rect.h > UI_CHAR_H) {
        draw_y += (rect.h - UI_CHAR_H) / 2;
    }
    ui_draw_text(api, draw_x, draw_y, text, fg, bg);
}

static inline void ui_draw_text_clipped_right(const minidos_app_api_t* api, int x, int y, const char* text,
    unsigned int fg, unsigned int bg, int max_chars);

static inline ui_rect_t ui_dropdown_popup_rect(ui_rect_t rect, int item_count) {
    int popup_h = item_count * UI_DROPDOWN_ITEM_H;
    if (popup_h < 0) {
        popup_h = 0;
    }
    return ui_rect_make(rect.x, rect.y + rect.h - 1, rect.w, popup_h + 2);
}

static inline ui_rect_t ui_dropdown_item_rect(ui_rect_t rect, int item_index) {
    return ui_rect_make(rect.x + 1, rect.y + rect.h + (item_index * UI_DROPDOWN_ITEM_H),
        rect.w - 2, UI_DROPDOWN_ITEM_H);
}

static inline ui_rect_t ui_menu_item_rect(ui_rect_t rect, int item_index) {
    return ui_rect_make(rect.x + 1, rect.y + 1 + (item_index * UI_MENU_ITEM_H),
        rect.w - 2, UI_MENU_ITEM_H);
}

static inline ui_rect_t ui_scrollbar_decrement_rect(ui_rect_t rect) {
    return ui_rect_make(rect.x, rect.y, rect.w, UI_SCROLLBAR_BUTTON_H);
}

static inline ui_rect_t ui_scrollbar_increment_rect(ui_rect_t rect) {
    return ui_rect_make(rect.x, rect.y + rect.h - UI_SCROLLBAR_BUTTON_H, rect.w, UI_SCROLLBAR_BUTTON_H);
}

static inline ui_rect_t ui_scrollbar_track_rect(ui_rect_t rect) {
    return ui_rect_make(rect.x, rect.y + UI_SCROLLBAR_BUTTON_H, rect.w, rect.h - (UI_SCROLLBAR_BUTTON_H * 2));
}

static inline ui_rect_t ui_scrollbar_thumb_rect(ui_rect_t rect, int min_value, int max_value, int page_size, int value) {
    ui_rect_t track = ui_scrollbar_track_rect(rect);
    ui_rect_t thumb = track;
    int range;
    int usable_h;
    int thumb_h;
    int pos;

    if (track.h <= 0) {
        return ui_rect_make(track.x, track.y, track.w, 0);
    }

    range = max_value - min_value;
    thumb_h = page_size > 0 ? (track.h * page_size) / (range + page_size) : UI_SCROLLBAR_MIN_THUMB_H;
    if (thumb_h < UI_SCROLLBAR_MIN_THUMB_H) {
        thumb_h = UI_SCROLLBAR_MIN_THUMB_H;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }

    usable_h = track.h - thumb_h;
    pos = 0;
    if (usable_h > 0 && range > 0) {
        if (value < min_value) {
            value = min_value;
        }
        if (value > max_value) {
            value = max_value;
        }
        pos = ((value - min_value) * usable_h) / range;
    }

    thumb.y += pos;
    thumb.h = thumb_h;
    return thumb;
}

static inline void ui_draw_checkbox(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect,
    const char* label, int checked, int focused, int enabled) {
    ui_rect_t box = ui_rect_make(rect.x, rect.y + ((rect.h - 13) / 2), 13, 13);
    unsigned int fg = enabled ? theme->text : theme->text_disabled;
    unsigned int bg = theme->field_bg;

    ui_draw_panel(api, theme, box, 0);
    ui_fill_rect(api, ui_rect_inset(box, 2), bg);
    if (checked) {
        ui_draw_text(api, box.x + 2, box.y + 2, "X", fg, bg);
    }
    if (focused) {
        ui_frame_rect(api, ui_rect_make(box.x - 2, box.y - 2, rect.w > 18 ? rect.w : 18, box.h + 4), theme->dark_shadow);
    }
    if (label) {
        ui_draw_text(api, rect.x + 18, rect.y + ((rect.h - UI_CHAR_H) / 2), label, fg, theme->field_bg);
    }
}

static inline void ui_draw_radio_button(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect,
    const char* label, int checked, int focused, int enabled) {
    ui_rect_t circle = ui_rect_make(rect.x, rect.y + ((rect.h - 13) / 2), 13, 13);
    unsigned int fg = enabled ? theme->text : theme->text_disabled;
    unsigned int bg = theme->field_bg;

    ui_fill_rect(api, circle, theme->face);
    ui_frame_rect(api, circle, theme->dark_shadow);
    ui_fill_rect(api, ui_rect_inset(circle, 1), bg);
    if (checked) {
        ui_fill_rect(api, ui_rect_make(circle.x + 4, circle.y + 4, 5, 5), fg);
    }
    if (focused) {
        ui_frame_rect(api, ui_rect_make(circle.x - 2, circle.y - 2, rect.w > 18 ? rect.w : 18, circle.h + 4), theme->dark_shadow);
    }
    if (label) {
        ui_draw_text(api, rect.x + 18, rect.y + ((rect.h - UI_CHAR_H) / 2), label, fg, theme->field_bg);
    }
}

static inline void ui_draw_dropdown(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect,
    const char* const* items, int item_count, int selected_index, int expanded, int focused, int hot_index) {
    ui_rect_t arrow_rect;
    const char* text = "";
    int i;

    if (!theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    if (items && selected_index >= 0 && selected_index < item_count && items[selected_index]) {
        text = items[selected_index];
    }

    ui_draw_panel(api, theme, rect, 0);
    ui_fill_rect(api, ui_rect_inset(rect, 2), theme->field_bg);
    arrow_rect = ui_rect_make(rect.x + rect.w - 20, rect.y + 2, 18, rect.h - 4);
    ui_draw_panel(api, theme, arrow_rect, 1);
    ui_draw_text(api, arrow_rect.x + 5, arrow_rect.y + ((arrow_rect.h - UI_CHAR_H) / 2), expanded ? "^" : "v",
        theme->text, theme->face);
    ui_draw_text_clipped_right(api, rect.x + 4, rect.y + ((rect.h - UI_CHAR_H) / 2), text,
        theme->field_text, theme->field_bg, (rect.w - 28) / UI_CHAR_W);

    if (focused) {
        ui_frame_rect(api, ui_rect_inset(rect, 3), theme->selection_bg);
    }

    if (expanded && item_count > 0) {
        ui_rect_t popup = ui_dropdown_popup_rect(rect, item_count);
        ui_draw_panel(api, theme, popup, 1);
        for (i = 0; i < item_count; i++) {
            ui_rect_t item_rect = ui_dropdown_item_rect(rect, i);
            int highlighted = (i == hot_index) || (hot_index < 0 && i == selected_index);
            unsigned int bg = highlighted ? theme->selection_bg : theme->field_bg;
            unsigned int fg = highlighted ? theme->selection_text : theme->field_text;
            ui_fill_rect(api, item_rect, bg);
            ui_draw_text(api, item_rect.x + 4, item_rect.y + ((item_rect.h - UI_CHAR_H) / 2),
                (items && items[i]) ? items[i] : "", fg, bg);
        }
    }
}

static inline void ui_draw_menu_widget(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect,
    const char* const* items, int item_count, int selected_index, int focused) {
    int i;

    if (!theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    ui_draw_panel(api, theme, rect, 1);
    for (i = 0; i < item_count; i++) {
        ui_rect_t item_rect = ui_menu_item_rect(rect, i);
        unsigned int bg = (i == selected_index) ? theme->selection_bg : theme->face;
        unsigned int fg = (i == selected_index) ? theme->selection_text : theme->text;

        if (item_rect.y + item_rect.h > rect.y + rect.h - 1) {
            break;
        }
        ui_fill_rect(api, item_rect, bg);
        ui_draw_text(api, item_rect.x + 6, item_rect.y + ((item_rect.h - UI_CHAR_H) / 2),
            (items && items[i]) ? items[i] : "", fg, bg);
    }

    if (focused) {
        ui_frame_rect(api, ui_rect_inset(rect, 3), theme->dark_shadow);
    }
}

static inline void ui_draw_scrollbar(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect,
    int min_value, int max_value, int page_size, int value, int focused) {
    ui_rect_t dec_rect;
    ui_rect_t inc_rect;
    ui_rect_t track_rect;
    ui_rect_t thumb_rect;

    if (!theme || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    dec_rect = ui_scrollbar_decrement_rect(rect);
    inc_rect = ui_scrollbar_increment_rect(rect);
    track_rect = ui_scrollbar_track_rect(rect);
    thumb_rect = ui_scrollbar_thumb_rect(rect, min_value, max_value, page_size, value);

    ui_draw_panel(api, theme, rect, 1);
    ui_fill_rect(api, track_rect, theme->face_alt);
    ui_draw_panel(api, theme, dec_rect, 1);
    ui_draw_panel(api, theme, inc_rect, 1);
    ui_draw_text(api, dec_rect.x + ((dec_rect.w - UI_CHAR_W) / 2),
        dec_rect.y + ((dec_rect.h - UI_CHAR_H) / 2), "^", theme->text, theme->face);
    ui_draw_text(api, inc_rect.x + ((inc_rect.w - UI_CHAR_W) / 2),
        inc_rect.y + ((inc_rect.h - UI_CHAR_H) / 2), "v", theme->text, theme->face);
    ui_draw_panel(api, theme, thumb_rect, 1);

    if (focused) {
        ui_frame_rect(api, ui_rect_inset(rect, 2), theme->selection_bg);
    }
}

/* --- Clipped compound primitives for layer composition --- */

static inline void ui_frame_rect_clipped(const minidos_app_api_t* api, ui_rect_t rect, unsigned int color, ui_rect_t clip) {
    if (rect.w <= 0 || rect.h <= 0) { return; }
    ui_fill_rect_clipped(api, ui_rect_make(rect.x, rect.y, rect.w, 1), color, clip);
    if (rect.h > 1) {
        ui_fill_rect_clipped(api, ui_rect_make(rect.x, rect.y + rect.h - 1, rect.w, 1), color, clip);
    }
    if (rect.h > 2) {
        ui_fill_rect_clipped(api, ui_rect_make(rect.x, rect.y + 1, 1, rect.h - 2), color, clip);
        if (rect.w > 1) {
            ui_fill_rect_clipped(api, ui_rect_make(rect.x + rect.w - 1, rect.y + 1, 1, rect.h - 2), color, clip);
        }
    }
}

static inline void ui_bevel_rect_clipped(const minidos_app_api_t* api, ui_rect_t rect,
    unsigned int top_left, unsigned int bottom_right, ui_rect_t clip) {
    if (rect.w <= 1 || rect.h <= 1) { return; }
    ui_fill_rect_clipped(api, ui_rect_make(rect.x, rect.y, rect.w - 1, 1), top_left, clip);
    ui_fill_rect_clipped(api, ui_rect_make(rect.x, rect.y, 1, rect.h - 1), top_left, clip);
    ui_fill_rect_clipped(api, ui_rect_make(rect.x + rect.w - 1, rect.y, 1, rect.h), bottom_right, clip);
    ui_fill_rect_clipped(api, ui_rect_make(rect.x, rect.y + rect.h - 1, rect.w, 1), bottom_right, clip);
}

static inline void ui_draw_panel_clipped(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, int raised, ui_rect_t clip) {
    if (!theme || rect.w <= 0 || rect.h <= 0) { return; }
    ui_fill_rect_clipped(api, rect, theme->face, clip);
    ui_bevel_rect_clipped(api, rect, raised ? theme->light : theme->shadow,
        raised ? theme->dark_shadow : theme->light, clip);
    if (rect.w > 2 && rect.h > 2) {
        ui_bevel_rect_clipped(api, ui_rect_inset(rect, 1),
            raised ? theme->face_alt : theme->dark_shadow,
            raised ? theme->shadow : theme->face_alt, clip);
    }
}

static inline void ui_draw_text_clipped_right(const minidos_app_api_t* api, int x, int y, const char* text,
    unsigned int fg, unsigned int bg, int max_chars) {
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
    ui_draw_text(api, x, y, visible, fg, bg);
}

/* ---- Listview batch line rendering via surface blit ---- */

/* 8x8 bitmap font glyphs for rendering text into pixel buffer (ASCII 32-126).
 * Each glyph is 8 bytes, one byte per row, MSB-first.
 */
static const unsigned char ui_font_8x8[95][8] = {
    /* 32 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */ {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    /* 34 '"' */ {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
    /* 35 '#' */ {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    /* 36 '$' */ {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00},
    /* 37 '%' */ {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
    /* 38 '&' */ {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    /* 39 '\''*/ {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    /* 40 '(' */ {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    /* 41 ')' */ {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    /* 42 '*' */ {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    /* 43 '+' */ {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    /* 44 ',' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    /* 45 '-' */ {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    /* 46 '.' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    /* 47 '/' */ {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    /* 48 '0' */ {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00},
    /* 49 '1' */ {0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00},
    /* 50 '2' */ {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00},
    /* 51 '3' */ {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
    /* 52 '4' */ {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00},
    /* 53 '5' */ {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
    /* 54 '6' */ {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},
    /* 55 '7' */ {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},
    /* 56 '8' */ {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},
    /* 57 '9' */ {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
    /* 58 ':' */ {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    /* 59 ';' */ {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    /* 60 '<' */ {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
    /* 61 '=' */ {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    /* 62 '>' */ {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},
    /* 63 '?' */ {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
    /* 64 '@' */ {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00},
    /* 65 'A' */ {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    /* 66 'B' */ {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},
    /* 67 'C' */ {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},
    /* 68 'D' */ {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},
    /* 69 'E' */ {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},
    /* 70 'F' */ {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},
    /* 71 'G' */ {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00},
    /* 72 'H' */ {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    /* 73 'I' */ {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    /* 74 'J' */ {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},
    /* 75 'K' */ {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},
    /* 76 'L' */ {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},
    /* 77 'M' */ {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00},
    /* 78 'N' */ {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},
    /* 79 'O' */ {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    /* 80 'P' */ {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
    /* 81 'Q' */ {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06},
    /* 82 'R' */ {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},
    /* 83 'S' */ {0x7C,0xC6,0xE0,0x7C,0x0E,0xC6,0x7C,0x00},
    /* 84 'T' */ {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00},
    /* 85 'U' */ {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    /* 86 'V' */ {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00},
    /* 87 'W' */ {0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00},
    /* 88 'X' */ {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00},
    /* 89 'Y' */ {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00},
    /* 90 'Z' */ {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},
    /* 91 '[' */ {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    /* 92 '\\'*/ {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    /* 93 ']' */ {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    /* 94 '^' */ {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    /* 95 '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    /* 96 '`' */ {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    /* 97 'a' */ {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},
    /* 98 'b' */ {0xE0,0x60,0x60,0x7C,0x66,0x66,0xDC,0x00},
    /* 99 'c' */ {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
    /*100 'd' */ {0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0x76,0x00},
    /*101 'e' */ {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
    /*102 'f' */ {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00},
    /*103 'g' */ {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8},
    /*104 'h' */ {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},
    /*105 'i' */ {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    /*106 'j' */ {0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C},
    /*107 'k' */ {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},
    /*108 'l' */ {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    /*109 'm' */ {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xC6,0x00},
    /*110 'n' */ {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00},
    /*111 'o' */ {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
    /*112 'p' */ {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},
    /*113 'q' */ {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},
    /*114 'r' */ {0x00,0x00,0xDC,0x76,0x66,0x60,0xF0,0x00},
    /*115 's' */ {0x00,0x00,0x7C,0xC0,0x7C,0x06,0xFC,0x00},
    /*116 't' */ {0x10,0x30,0x7C,0x30,0x30,0x34,0x18,0x00},
    /*117 'u' */ {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00},
    /*118 'v' */ {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00},
    /*119 'w' */ {0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00},
    /*120 'x' */ {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
    /*121 'y' */ {0x00,0x00,0xC6,0xC6,0xCE,0x76,0x06,0xFC},
    /*122 'z' */ {0x00,0x00,0xFE,0x0C,0x38,0x60,0xFE,0x00},
    /*123 '{' */ {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    /*124 '|' */ {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    /*125 '}' */ {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    /*126 '~' */ {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* Fill a rectangular region in the line buffer with a solid color */
static inline void ui_linebuf_fill(unsigned char* buf, int buf_w, int buf_h,
    int x, int y, int w, int h, unsigned int color) {
    unsigned char r = (unsigned char)((color >> 16) & 0xFF);
    unsigned char g = (unsigned char)((color >> 8) & 0xFF);
    unsigned char b = (unsigned char)(color & 0xFF);
    int row, col;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > buf_w) { w = buf_w - x; }
    if (y + h > buf_h) { h = buf_h - y; }
    if (w <= 0 || h <= 0) { return; }

    for (row = y; row < y + h; row++) {
        unsigned char* dst = buf + (row * buf_w + x) * 4;
        for (col = 0; col < w; col++) {
            dst[col * 4 + 0] = 0;  /* X */
            dst[col * 4 + 1] = r;
            dst[col * 4 + 2] = g;
            dst[col * 4 + 3] = b;
        }
    }
}

/* Render a character glyph into the line buffer */
static inline void ui_linebuf_char(unsigned char* buf, int buf_w, int buf_h,
    int cx, int cy, char ch, unsigned int fg, unsigned int bg) {
    int glyph_index;
    const unsigned char* glyph;
    unsigned char fr = (unsigned char)((fg >> 16) & 0xFF);
    unsigned char fgn = (unsigned char)((fg >> 8) & 0xFF);
    unsigned char fb = (unsigned char)(fg & 0xFF);
    unsigned char br = (unsigned char)((bg >> 16) & 0xFF);
    unsigned char bgn = (unsigned char)((bg >> 8) & 0xFF);
    unsigned char bb = (unsigned char)(bg & 0xFF);
    int row, col;

    if (ch < 32 || ch > 126) { return; }
    glyph_index = ch - 32;
    glyph = ui_font_8x8[glyph_index];

    for (row = 0; row < 8; row++) {
        int py = cy + row;
        unsigned char bits;
        if (py < 0 || py >= buf_h) { continue; }
        bits = glyph[row];
        for (col = 0; col < 8; col++) {
            int px = cx + col;
            unsigned char* dst;
            if (px < 0 || px >= buf_w) { continue; }
            dst = buf + (py * buf_w + px) * 4;
            if (bits & (0x80 >> col)) {
                dst[0] = 0; dst[1] = fr; dst[2] = fgn; dst[3] = fb;
            } else {
                dst[0] = 0; dst[1] = br; dst[2] = bgn; dst[3] = bb;
            }
        }
    }
}

/* Render a text string into the line buffer */
static inline void ui_linebuf_text(unsigned char* buf, int buf_w, int buf_h,
    int x, int y, const char* text, unsigned int fg, unsigned int bg) {
    int i = 0;
    if (!text) { return; }
    while (text[i] != '\0') {
        ui_linebuf_char(buf, buf_w, buf_h, x + i * 8, y, text[i], fg, bg);
        i++;
    }
}

static inline void ui_draw_listview_line_with_clip(const minidos_app_api_t* api,
    ui_listview_line_buf_t* line_buf,
    int dst_x, int dst_y, int line_w,
    const ui_listview_item_t* item, int is_selected,
    int row_index, const ui_theme_t* theme,
    ui_rect_t clip, int use_clip) {
    unsigned int bg;
    unsigned int fg;
    unsigned int type_fg;
    app_gfx_surface_blit_t blit;
    int clamped_w;
    ui_rect_t dst_rect;
    ui_rect_t clipped;

    if (!api || !line_buf || !item || !theme || line_w <= 0) { return; }

    clamped_w = line_w;
    if (clamped_w > UI_LISTVIEW_LINE_MAX_W) { clamped_w = UI_LISTVIEW_LINE_MAX_W; }
    dst_rect = ui_rect_make(dst_x, dst_y, clamped_w, UI_LISTVIEW_ROW_H);
    clipped = use_clip ? ui_rect_intersect(dst_rect, clip) : dst_rect;
    if (use_clip && ui_rect_is_empty(clipped)) { return; }

    /* Choose colors */
    if (is_selected) {
        bg = theme->selection_bg;
        fg = theme->selection_text;
        type_fg = theme->selection_text;
    } else if ((row_index & 1) == 1) {
        bg = theme->face_alt;
        fg = theme->text;
        type_fg = theme->text_disabled;
    } else {
        bg = theme->field_bg;
        fg = theme->text;
        type_fg = theme->text_disabled;
    }

    /* Fill background */
    ui_linebuf_fill(line_buf->pixels, clamped_w, UI_LISTVIEW_ROW_H,
        0, 0, clamped_w, UI_LISTVIEW_ROW_H, bg);

    /* Selection indicator */
    if (is_selected) {
        ui_linebuf_fill(line_buf->pixels, clamped_w, UI_LISTVIEW_ROW_H,
            0, 0, 3, UI_LISTVIEW_ROW_H, theme->light);
    }

    /* Type tag */
    ui_linebuf_text(line_buf->pixels, clamped_w, UI_LISTVIEW_ROW_H,
        12, 4, item->is_dir ? "[DIR]" : "[FIL]", type_fg, bg);

    /* Item name */
    ui_linebuf_text(line_buf->pixels, clamped_w, UI_LISTVIEW_ROW_H,
        68, 4, item->name, fg, bg);

    /* Blit the rendered line to screen in one syscall */
    blit.buffer = line_buf->pixels;
    blit.width = clamped_w;
    blit.height = UI_LISTVIEW_ROW_H;
    blit.stride = clamped_w * 4;
    blit.format = APP_SURFACE_FORMAT_XRGB8888;
    blit.dest_x = dst_x;
    blit.dest_y = dst_y;
    blit.clip_x = use_clip ? clipped.x : -1;
    blit.clip_y = use_clip ? clipped.y : 0;
    blit.clip_w = use_clip ? clipped.w : 0;
    blit.clip_h = use_clip ? clipped.h : 0;
    blit.dest_w = clamped_w;
    blit.dest_h = UI_LISTVIEW_ROW_H;
    (void)app_gfx_surface_blit(api, &blit);
}

/* Render one listview row into a line buffer and blit it to screen */
static inline void ui_draw_listview_line(const minidos_app_api_t* api,
    ui_listview_line_buf_t* line_buf,
    int dst_x, int dst_y, int line_w,
    const ui_listview_item_t* item, int is_selected,
    int row_index, const ui_theme_t* theme) {
    ui_draw_listview_line_with_clip(api, line_buf, dst_x, dst_y, line_w,
        item, is_selected, row_index, theme, ui_rect_make(0, 0, 0, 0), 0);
}

static inline void ui_draw_listview_line_clipped(const minidos_app_api_t* api,
    ui_listview_line_buf_t* line_buf,
    int dst_x, int dst_y, int line_w,
    const ui_listview_item_t* item, int is_selected,
    int row_index, const ui_theme_t* theme, ui_rect_t clip) {
    ui_draw_listview_line_with_clip(api, line_buf, dst_x, dst_y, line_w,
        item, is_selected, row_index, theme, clip, 1);
}

/* Draw the full listview: all visible rows via batch blit */
static inline void ui_draw_listview(const minidos_app_api_t* api,
    ui_listview_line_buf_t* line_buf,
    ui_rect_t bounds, const ui_listview_t* lv, const ui_theme_t* theme) {
    int i;
    int line_w;
    int visible;

    if (!api || !line_buf || !lv || !theme) { return; }

    line_w = bounds.w;
    if (line_w > UI_LISTVIEW_LINE_MAX_W) { line_w = UI_LISTVIEW_LINE_MAX_W; }
    visible = lv->visible_count;

    for (i = 0; i < visible; i++) {
        int item_idx = lv->scroll_offset + i;
        int dst_y = bounds.y + i * UI_LISTVIEW_ROW_H;
        if (item_idx < lv->item_count) {
            ui_draw_listview_line(api, line_buf,
                bounds.x, dst_y, line_w,
                &lv->items[item_idx],
                item_idx == lv->selected_index,
                i, theme);
        } else {
            /* Empty row below items */
            ui_fill_rect(api, ui_rect_make(bounds.x, dst_y, line_w, UI_LISTVIEW_ROW_H),
                theme->field_bg);
        }
    }
}

static inline void ui_draw_listview_clipped(const minidos_app_api_t* api,
    ui_listview_line_buf_t* line_buf,
    ui_rect_t bounds, const ui_listview_t* lv, const ui_theme_t* theme, ui_rect_t clip) {
    int i;
    int line_w;
    int visible;

    if (!api || !line_buf || !lv || !theme) { return; }

    line_w = bounds.w;
    if (line_w > UI_LISTVIEW_LINE_MAX_W) { line_w = UI_LISTVIEW_LINE_MAX_W; }
    visible = lv->visible_count;

    for (i = 0; i < visible; i++) {
        int item_idx = lv->scroll_offset + i;
        int dst_y = bounds.y + i * UI_LISTVIEW_ROW_H;
        ui_rect_t line_rect = ui_rect_make(bounds.x, dst_y, line_w, UI_LISTVIEW_ROW_H);

        if (ui_rect_is_empty(ui_rect_intersect(line_rect, clip))) {
            continue;
        }

        if (item_idx < lv->item_count) {
            ui_draw_listview_line_clipped(api, line_buf,
                bounds.x, dst_y, line_w,
                &lv->items[item_idx],
                item_idx == lv->selected_index,
                i, theme, clip);
        } else {
            ui_fill_rect_clipped(api, line_rect, theme->field_bg, clip);
        }
    }
}

/*
 * Scroll-aware listview redraw.
 * Re-renders all visible rows via batch surface blit (1 syscall per row).
 * Tracks prev_scroll_offset for future delta optimisation.
 * Currently redraws all visible rows on any scroll change; the batch blit
 * approach already reduces syscalls from N*chars_per_row to N.
 */
static inline void ui_draw_listview_scroll_delta(const minidos_app_api_t* api,
    ui_listview_line_buf_t* line_buf,
    ui_rect_t bounds, ui_listview_t* lv, const ui_theme_t* theme) {
    int delta;
    int line_w;
    int visible;
    int i;

    if (!api || !line_buf || !lv || !theme) { return; }

    delta = lv->scroll_offset - lv->prev_scroll_offset;
    line_w = bounds.w;
    if (line_w > UI_LISTVIEW_LINE_MAX_W) { line_w = UI_LISTVIEW_LINE_MAX_W; }
    visible = lv->visible_count;

    /* For small scroll deltas, batch-redraw all visible rows.
     * Each row is rendered into a line buffer and blitted in one syscall
     * (N visible rows = N syscalls vs N*chars_per_row with per-char drawing).
     * For larger jumps, fall through to full redraw via ui_draw_listview. */
    if (delta == 1 || delta == -1) {
        for (i = 0; i < visible; i++) {
            int item_idx = lv->scroll_offset + i;
            int dst_y = bounds.y + i * UI_LISTVIEW_ROW_H;
            if (item_idx < lv->item_count) {
                ui_draw_listview_line(api, line_buf,
                    bounds.x, dst_y, line_w,
                    &lv->items[item_idx],
                    item_idx == lv->selected_index,
                    i, theme);
            } else {
                ui_fill_rect(api, ui_rect_make(bounds.x, dst_y, line_w, UI_LISTVIEW_ROW_H),
                    theme->field_bg);
            }
        }
    } else {
        ui_draw_listview(api, line_buf, bounds, lv, theme);
    }

    lv->prev_scroll_offset = lv->scroll_offset;
}

/* Helper: initialise a listview struct */
static inline void ui_listview_init(ui_listview_t* lv, int viewport_h) {
    int i;
    if (!lv) { return; }
    lv->item_count = 0;
    lv->scroll_offset = 0;
    lv->selected_index = 0;
    lv->prev_scroll_offset = 0;
    lv->visible_count = viewport_h / UI_LISTVIEW_ROW_H;
    if (lv->visible_count < 1) { lv->visible_count = 1; }
    for (i = 0; i < UI_LISTVIEW_MAX_ITEMS; i++) {
        lv->items[i].name[0] = '\0';
        lv->items[i].is_dir = 0;
        lv->items[i].icon_color = 0;
    }
}

/* Helper: ensure selected item is visible by adjusting scroll_offset */
static inline void ui_listview_ensure_visible(ui_listview_t* lv) {
    if (!lv) { return; }
    if (lv->selected_index < lv->scroll_offset) {
        lv->scroll_offset = lv->selected_index;
    }
    if (lv->selected_index >= lv->scroll_offset + lv->visible_count) {
        lv->scroll_offset = lv->selected_index - lv->visible_count + 1;
    }
    if (lv->scroll_offset < 0) { lv->scroll_offset = 0; }
}

/* Helper: move selection up */
static inline void ui_listview_select_prev(ui_listview_t* lv) {
    if (!lv || lv->item_count <= 0) { return; }
    if (lv->selected_index > 0) {
        lv->selected_index--;
        ui_listview_ensure_visible(lv);
    }
}

/* Helper: move selection down */
static inline void ui_listview_select_next(ui_listview_t* lv) {
    if (!lv || lv->item_count <= 0) { return; }
    if (lv->selected_index < lv->item_count - 1) {
        lv->selected_index++;
        ui_listview_ensure_visible(lv);
    }
}

static inline void ui_draw_text_box(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect, const char* text, int focused) {
    unsigned int fill;
    int max_chars;
    int len = 0;
    int start = 0;
    int visible_len = 0;
    int cursor_x;
    unsigned int ticks = 0;
    if (!theme) {
        return;
    }
    ui_draw_panel(api, theme, rect, 0);
    rect = ui_rect_inset(rect, 2);
    fill = focused ? theme->light : theme->field_bg;
    ui_fill_rect(api, rect, fill);
    ui_frame_rect(api, rect, focused ? theme->selection_bg : theme->shadow);
    if (text) {
        len = ui_strlen(text);
        max_chars = (rect.w - 8) / UI_CHAR_W;
        ui_draw_text_clipped_right(api, rect.x + 4, rect.y + 4, text, theme->field_text, fill, max_chars);
        if (max_chars > 0) {
            start = len > max_chars ? (len - max_chars) : 0;
            visible_len = len - start;
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
        ui_fill_rect(api, ui_rect_make(cursor_x, rect.y + 3, 1, rect.h - 6), theme->field_text);
    }
}

#endif
