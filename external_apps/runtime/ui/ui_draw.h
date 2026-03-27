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

static inline void ui_draw_text_box(const minidos_app_api_t* api, const ui_theme_t* theme, ui_rect_t rect, const char* text, int focused) {
    unsigned int fill;
    int max_chars;
    if (!theme) {
        return;
    }
    ui_draw_panel(api, theme, rect, 0);
    rect = ui_rect_inset(rect, 2);
    fill = focused ? theme->light : theme->field_bg;
    ui_fill_rect(api, rect, fill);
    ui_frame_rect(api, rect, focused ? theme->selection_bg : theme->shadow);
    if (text) {
        max_chars = (rect.w - 8) / UI_CHAR_W;
        ui_draw_text_clipped_right(api, rect.x + 4, rect.y + 4, text, theme->field_text, fill, max_chars);
    }
}

#endif
