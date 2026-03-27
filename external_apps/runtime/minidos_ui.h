#ifndef MINIDOS_UI_H
#define MINIDOS_UI_H

#include "minidos_app.h"
#include "minidos_cursor_bitmap.h"

#define UI_CHAR_W 8
#define UI_CHAR_H 8
#define UI_DIRTY_RECTS_MAX 8
#define UI_WM_MAX_WINDOWS 8
#define UI_WM_MAX_CONTROLS 64
#define UI_BITMAP_MAX_FILE_SIZE (256 * 1024)
#define UI_BITMAP_TRANSPARENT_COLOR 0xFF00FFu
#define UI_BITMAP_PATH_MAX 128
/* Max decoded XRGB8888 pixels for wallpaper surface (covers 24bpp source up to 256KB) */
#define UI_WALLPAPER_SURFACE_MAX_BYTES (348160)

typedef struct {
    char path[UI_BITMAP_PATH_MAX];
    int valid;
    int src_w;
    int src_h;
    int abs_src_h;
    int top_down;
    unsigned int bit_count;
    unsigned int row_stride;
    unsigned int data_offset;
    unsigned int bytes_per_pixel;
    unsigned char data[UI_BITMAP_MAX_FILE_SIZE];
} ui_bitmap_cache_t;

/* Decoded XRGB8888 wallpaper surface, populated once from the BMP file cache */
typedef struct {
    char path[UI_BITMAP_PATH_MAX];
    unsigned char pixels[UI_WALLPAPER_SURFACE_MAX_BYTES];
    int width;
    int height;
    int stride;   /* bytes per row = width * 4 */
    int valid;
} ui_wallpaper_surface_t;

static ui_bitmap_cache_t g_ui_bitmap_cache;
static ui_wallpaper_surface_t g_ui_wallpaper_surface;

static inline int ui_path_equal(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    int i = 0;
    while (i < UI_BITMAP_PATH_MAX) {
        char ca = a[i];
        char cb = b[i];
        if (ca == '\0' && cb == '\0') {
            return 1;
        }
        if (ca != cb) {
            return 0;
        }
        if (ca == '\0') {
            break;
        }
        i++;
    }
    return 1;
}

static inline void ui_path_copy(char* dst, const char* src) {
    if (!dst) {
        return;
    }
    int i = 0;
    if (src) {
        while (i < (UI_BITMAP_PATH_MAX - 1) && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static inline unsigned int ui_bitmap_read_u16(const unsigned char* data) {
    return (unsigned int)data[0] | ((unsigned int)data[1] << 8);
}

static inline unsigned int ui_bitmap_read_u32(const unsigned char* data) {
    return (unsigned int)data[0]
        | ((unsigned int)data[1] << 8)
        | ((unsigned int)data[2] << 16)
        | ((unsigned int)data[3] << 24);
}

static int ui_bitmap_cache_load(const minidos_app_api_t* api, const char* path) {
    if (!api || !path) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    if (g_ui_bitmap_cache.valid && ui_path_equal(g_ui_bitmap_cache.path, path)) {
        return 1;
    }

    int file_size = app_file_size(api, path);
    if (file_size <= 0 || file_size > UI_BITMAP_MAX_FILE_SIZE) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    int bytes_read = app_file_read(api, path, g_ui_bitmap_cache.data, file_size);
    if (bytes_read != file_size || file_size < 54) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    if (g_ui_bitmap_cache.data[0] != 'B' || g_ui_bitmap_cache.data[1] != 'M') {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    unsigned int data_offset = ui_bitmap_read_u32(g_ui_bitmap_cache.data + 10);
    if (data_offset >= (unsigned int)file_size || data_offset < 54) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    unsigned int info_size = ui_bitmap_read_u32(g_ui_bitmap_cache.data + 14);
    if (info_size < 40) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    int src_w = (int)ui_bitmap_read_u32(g_ui_bitmap_cache.data + 18);
    int src_h = (int)ui_bitmap_read_u32(g_ui_bitmap_cache.data + 22);
    unsigned int planes = ui_bitmap_read_u16(g_ui_bitmap_cache.data + 26);
    unsigned int bit_count = ui_bitmap_read_u16(g_ui_bitmap_cache.data + 28);
    unsigned int compression = ui_bitmap_read_u32(g_ui_bitmap_cache.data + 30);

    if (planes != 1 || compression != 0 || src_w <= 0 || src_h == 0) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    if (bit_count != 24 && bit_count != 32) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    int abs_src_h = src_h < 0 ? -src_h : src_h;
    if (abs_src_h <= 0) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    unsigned int row_stride = (((unsigned int)src_w * bit_count + 31u) / 32u) * 4u;
    if (row_stride == 0) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    unsigned int pixel_bytes = row_stride * (unsigned int)abs_src_h;
    if (data_offset + pixel_bytes > (unsigned int)file_size) {
        g_ui_bitmap_cache.valid = 0;
        return 0;
    }

    g_ui_bitmap_cache.src_w = src_w;
    g_ui_bitmap_cache.src_h = src_h;
    g_ui_bitmap_cache.abs_src_h = abs_src_h;
    g_ui_bitmap_cache.top_down = (src_h < 0);
    g_ui_bitmap_cache.bit_count = bit_count;
    g_ui_bitmap_cache.row_stride = row_stride;
    g_ui_bitmap_cache.data_offset = data_offset;
    g_ui_bitmap_cache.bytes_per_pixel = bit_count / 8u;
    ui_path_copy(g_ui_bitmap_cache.path, path);
    g_ui_bitmap_cache.valid = 1;
    return 1;
}

/*
 * Decode the loaded BMP file cache into the wallpaper surface as XRGB8888.
 * Returns 1 on success, 0 if the BMP is too large, already cached, or not loaded.
 * Falls back to 0 (invalid surface) silently on any error.
 */
static int ui_wallpaper_surface_load(const minidos_app_api_t* api, const char* path) {
    ui_bitmap_cache_t* c;
    ui_wallpaper_surface_t* s = &g_ui_wallpaper_surface;
    const unsigned char* pixel_base;
    int w, h, y, x;
    unsigned int total_bytes;

    if (!api || !path) {
        s->valid = 0;
        return 0;
    }

    /* Already decoded for this path */
    if (s->valid && ui_path_equal(s->path, path)) {
        return 1;
    }

    s->valid = 0;

    if (!ui_bitmap_cache_load(api, path)) {
        return 0;
    }

    c = &g_ui_bitmap_cache;
    w = c->src_w;
    h = c->abs_src_h;
    total_bytes = (unsigned int)(w * h * 4);

    if (w <= 0 || h <= 0 || total_bytes > UI_WALLPAPER_SURFACE_MAX_BYTES) {
        return 0;
    }

    pixel_base = c->data + c->data_offset;

    for (y = 0; y < h; y++) {
        int row_index = c->top_down ? y : (h - 1 - y);
        const unsigned char* src_row = pixel_base + (unsigned int)row_index * c->row_stride;
        unsigned char* dst_row = s->pixels + (unsigned int)(y * w * 4);

        for (x = 0; x < w; x++) {
            const unsigned char* px = src_row + (unsigned int)x * c->bytes_per_pixel;
            /* BMP is BGR; surface is XRGB8888: byte order X, R, G, B */
            dst_row[x * 4 + 0] = 0;       /* X */
            dst_row[x * 4 + 1] = px[2];   /* R */
            dst_row[x * 4 + 2] = px[1];   /* G */
            dst_row[x * 4 + 3] = px[0];   /* B */
        }
    }

    s->width  = w;
    s->height = h;
    s->stride = w * 4;
    ui_path_copy(s->path, path);
    s->valid = 1;
    return 1;
}

/*
 * Blit the wallpaper surface at (dst_x, dst_y) using the fast surface syscall.
 * clip_x/y/w/h: region to restore (-1 to blit full surface).
 * Returns 1 on success, 0 if surface not loaded.
 */
static inline int ui_wallpaper_surface_blit(const minidos_app_api_t* api, int dst_x, int dst_y, int clip_x, int clip_y, int clip_w, int clip_h) {
    ui_wallpaper_surface_t* s = &g_ui_wallpaper_surface;
    app_gfx_surface_blit_t blit;

    if (!s->valid) {
        return 0;
    }

    blit.buffer = s->pixels;
    blit.width  = s->width;
    blit.height = s->height;
    blit.stride = s->stride;
    blit.format = APP_SURFACE_FORMAT_XRGB8888;
    blit.dest_x = dst_x;
    blit.dest_y = dst_y;
    blit.clip_x = clip_x;
    blit.clip_y = clip_y;
    blit.clip_w = clip_w;
    blit.clip_h = clip_h;
    return app_gfx_surface_blit(api, &blit);
}

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ui_rect_t;

typedef struct {
    ui_rect_t rects[UI_DIRTY_RECTS_MAX];
    int count;
} ui_dirty_list_t;

typedef struct {
    unsigned int desktop_bg;
    unsigned int desktop_accent;
    unsigned int face;
    unsigned int face_alt;
    unsigned int light;
    unsigned int shadow;
    unsigned int dark_shadow;
    unsigned int text;
    unsigned int text_disabled;
    unsigned int title_active_bg;
    unsigned int title_active_text;
    unsigned int title_inactive_bg;
    unsigned int title_inactive_text;
    unsigned int field_bg;
    unsigned int field_text;
    unsigned int selection_bg;
    unsigned int selection_text;
    const char* desktop_bg_bitmap;
} ui_theme_t;

typedef struct {
    ui_rect_t bounds;
    const char* title;
    int active;
    int has_close_button;
} ui_window_t;

typedef struct {
    ui_rect_t bounds;
    const char* label;
    int pressed;
    int focused;
    int enabled;
} ui_button_t;

enum {
    UI_CONTROL_LABEL = 1,
    UI_CONTROL_BUTTON = 2,
    UI_CONTROL_TEXTINPUT = 3,
};

typedef struct {
    int id;
    int type;
    int window_id;
    ui_rect_t bounds;
    const char* text;
    char* text_buffer;
    int text_buffer_len;
    int text_len;
    int visible;
    int enabled;
    int focused;
    int pressed;
} ui_control_t;

typedef struct {
    int id;
    ui_window_t window;
    int visible;
    int z_order;
} ui_wm_window_t;

typedef struct {
    ui_theme_t theme;
    ui_wm_window_t windows[UI_WM_MAX_WINDOWS];
    ui_control_t controls[UI_WM_MAX_CONTROLS];
    int window_count;
    int control_count;
    int next_window_id;
    int next_control_id;
    int active_window_id;
    int focused_control_id;
    int pressed_window_id;
    int pressed_control_id;
    int pressed_hit_close;
} ui_window_manager_t;

static inline int ui_strlen(const char* s) {
    int n = 0;
    while (s && s[n] != '\0') {
        n++;
    }
    return n;
}

static inline ui_rect_t ui_rect_make(int x, int y, int w, int h) {
    ui_rect_t rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    return rect;
}

static inline ui_rect_t ui_rect_inset(ui_rect_t rect, int amount) {
    rect.x += amount;
    rect.y += amount;
    rect.w -= amount * 2;
    rect.h -= amount * 2;
    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static inline int ui_rect_is_empty(ui_rect_t rect) {
    return rect.w <= 0 || rect.h <= 0;
}

static inline int ui_rect_contains(const ui_rect_t* rect, int px, int py) {
    if (!rect) {
        return 0;
    }
    return px >= rect->x && py >= rect->y
        && px < (rect->x + rect->w)
        && py < (rect->y + rect->h);
}

static inline int ui_rect_contains_rect(const ui_rect_t* outer, const ui_rect_t* inner) {
    if (!outer || !inner || ui_rect_is_empty(*inner)) {
        return 0;
    }
    return inner->x >= outer->x
        && inner->y >= outer->y
        && (inner->x + inner->w) <= (outer->x + outer->w)
        && (inner->y + inner->h) <= (outer->y + outer->h);
}

static inline int ui_rects_intersect(ui_rect_t a, ui_rect_t b) {
    if (ui_rect_is_empty(a) || ui_rect_is_empty(b)) {
        return 0;
    }
    return a.x < (b.x + b.w) && (a.x + a.w) > b.x
        && a.y < (b.y + b.h) && (a.y + a.h) > b.y;
}

static inline ui_rect_t ui_rect_intersect(ui_rect_t a, ui_rect_t b) {
    int x = (a.x > b.x) ? a.x : b.x;
    int y = (a.y > b.y) ? a.y : b.y;
    int x2 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    int y2 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
    if (x2 <= x || y2 <= y) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(x, y, x2 - x, y2 - y);
}

static inline ui_rect_t ui_rect_union(ui_rect_t a, ui_rect_t b) {
    int right;
    int bottom;
    ui_rect_t out;

    if (ui_rect_is_empty(a)) {
        return b;
    }
    if (ui_rect_is_empty(b)) {
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

static inline int ui_rect_subtract(ui_rect_t rect, ui_rect_t cutout, ui_rect_t* out_rects, int max_rects) {
    ui_rect_t overlap;
    int count = 0;
    int rect_right;
    int rect_bottom;
    int overlap_right;
    int overlap_bottom;

    if (!out_rects || max_rects <= 0 || ui_rect_is_empty(rect)) {
        return 0;
    }

    overlap = ui_rect_intersect(rect, cutout);
    if (ui_rect_is_empty(overlap)) {
        out_rects[0] = rect;
        return 1;
    }

    rect_right = rect.x + rect.w;
    rect_bottom = rect.y + rect.h;
    overlap_right = overlap.x + overlap.w;
    overlap_bottom = overlap.y + overlap.h;

    if (overlap.y > rect.y && count < max_rects) {
        out_rects[count++] = ui_rect_make(rect.x, rect.y, rect.w, overlap.y - rect.y);
    }
    if (overlap_bottom < rect_bottom && count < max_rects) {
        out_rects[count++] = ui_rect_make(rect.x, overlap_bottom, rect.w, rect_bottom - overlap_bottom);
    }
    if (overlap.x > rect.x && count < max_rects) {
        out_rects[count++] = ui_rect_make(rect.x, overlap.y, overlap.x - rect.x, overlap.h);
    }
    if (overlap_right < rect_right && count < max_rects) {
        out_rects[count++] = ui_rect_make(overlap_right, overlap.y, rect_right - overlap_right, overlap.h);
    }

    return count;
}

static inline void ui_dirty_list_init(ui_dirty_list_t* list) {
    if (!list) {
        return;
    }
    list->count = 0;
}

static inline int ui_dirty_list_add(ui_dirty_list_t* list, ui_rect_t rect) {
    int i;

    if (!list || ui_rect_is_empty(rect)) {
        return 0;
    }

    for (i = 0; i < list->count; i++) {
        if (ui_rect_contains_rect(&list->rects[i], &rect)) {
            return 1;
        }
        if (ui_rect_contains_rect(&rect, &list->rects[i])) {
            list->rects[i] = rect;
            return 1;
        }
    }

    if (list->count < UI_DIRTY_RECTS_MAX) {
        list->rects[list->count++] = rect;
        return 1;
    }

    list->rects[list->count - 1] = ui_rect_union(list->rects[list->count - 1], rect);
    return 1;
}

static inline int ui_dirty_list_add_clipped(ui_dirty_list_t* list, ui_rect_t rect, ui_rect_t clip) {
    return ui_dirty_list_add(list, ui_rect_intersect(rect, clip));
}

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

static inline ui_rect_t ui_window_client_rect(const ui_window_t* window) {
    ui_rect_t rect = ui_rect_make(0, 0, 0, 0);
    if (!window) {
        return rect;
    }
    rect = ui_rect_make(window->bounds.x + 4, window->bounds.y + 22, window->bounds.w - 8, window->bounds.h - 26);
    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static inline ui_rect_t ui_window_title_bar_rect(const ui_window_t* window) {
    if (!window) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(window->bounds.x + 3, window->bounds.y + 3, window->bounds.w - 6, 16);
}

static inline ui_rect_t ui_window_close_button_rect(const ui_window_t* window) {
    ui_rect_t title_rect = ui_window_title_bar_rect(window);
    if (title_rect.w < 20) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(title_rect.x + title_rect.w - 18, title_rect.y + 1, 16, 14);
}

static inline int ui_button_contains(const ui_button_t* button, int x, int y) {
    if (!button) {
        return 0;
    }
    return ui_rect_contains(&button->bounds, x, y);
}

static inline int ui_window_hit_close(const ui_window_t* window, int x, int y) {
    ui_rect_t close_rect = ui_window_close_button_rect(window);
    return window && window->has_close_button && ui_rect_contains(&close_rect, x, y);
}

static inline int ui_window_hit_title(const ui_window_t* window, int x, int y) {
    ui_rect_t title_rect = ui_window_title_bar_rect(window);
    return window && ui_rect_contains(&title_rect, x, y) && !ui_window_hit_close(window, x, y);
}

static inline void ui_draw_button(const minidos_app_api_t* api, const ui_theme_t* theme, const ui_button_t* button);
static inline void ui_draw_window(const minidos_app_api_t* api, const ui_theme_t* theme, const ui_window_t* window);
static inline void ui_draw_desktop(const minidos_app_api_t* api, const ui_theme_t* theme, int width, int height, const char* title);

static inline ui_wm_window_t* ui_wm_find_window(ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == window_id) {
            return &wm->windows[i];
        }
    }
    return 0;
}

static inline const ui_wm_window_t* ui_wm_find_window_const(const ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == window_id) {
            return &wm->windows[i];
        }
    }
    return 0;
}

static inline ui_control_t* ui_wm_find_control(ui_window_manager_t* wm, int control_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].id == control_id) {
            return &wm->controls[i];
        }
    }
    return 0;
}

static inline const ui_control_t* ui_wm_find_control_const(const ui_window_manager_t* wm, int control_id) {
    int i;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].id == control_id) {
            return &wm->controls[i];
        }
    }
    return 0;
}

static inline void ui_wm_init(ui_window_manager_t* wm, ui_theme_t theme) {
    int i;
    if (!wm) {
        return;
    }
    wm->theme = theme;
    wm->window_count = 0;
    wm->control_count = 0;
    wm->next_window_id = 1;
    wm->next_control_id = 1;
    wm->active_window_id = 0;
    wm->focused_control_id = 0;
    wm->pressed_window_id = 0;
    wm->pressed_control_id = 0;
    wm->pressed_hit_close = 0;
    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        wm->windows[i].id = 0;
        wm->windows[i].visible = 0;
        wm->windows[i].z_order = 0;
    }
    for (i = 0; i < UI_WM_MAX_CONTROLS; i++) {
        wm->controls[i].id = 0;
        wm->controls[i].visible = 0;
        wm->controls[i].enabled = 0;
    }
}

static inline void ui_wm_set_active_window(ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return;
    }
    wm->active_window_id = window_id;
    for (i = 0; i < wm->window_count; i++) {
        wm->windows[i].window.active = (wm->windows[i].id == window_id);
    }
}

static inline int ui_wm_create_window(ui_window_manager_t* wm, ui_rect_t bounds, const char* title, int has_close_button) {
    ui_wm_window_t* entry;
    int id;

    if (!wm || wm->window_count >= UI_WM_MAX_WINDOWS) {
        return 0;
    }

    entry = &wm->windows[wm->window_count];
    id = wm->next_window_id++;

    entry->id = id;
    entry->window.bounds = bounds;
    entry->window.title = title;
    entry->window.active = 0;
    entry->window.has_close_button = has_close_button;
    entry->visible = 1;
    entry->z_order = wm->window_count;
    wm->window_count++;

    ui_wm_set_active_window(wm, id);
    return id;
}

static inline void ui_wm_normalize_z_order(ui_window_manager_t* wm) {
    int drawn[UI_WM_MAX_WINDOWS];
    int draw_count;
    int i;

    if (!wm) {
        return;
    }

    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        drawn[i] = 0;
    }

    for (draw_count = 0; draw_count < wm->window_count; draw_count++) {
        int best_index = -1;
        int best_z = 2147483647;

        for (i = 0; i < wm->window_count; i++) {
            if (drawn[i]) {
                continue;
            }
            if (best_index >= 0 && wm->windows[i].z_order >= best_z) {
                continue;
            }

            best_index = i;
            best_z = wm->windows[i].z_order;
        }

        if (best_index < 0) {
            break;
        }

        drawn[best_index] = 1;
        wm->windows[best_index].z_order = draw_count;
    }
}

static inline void ui_wm_bring_to_front(ui_window_manager_t* wm, int window_id) {
    int i;
    int max_z = -1;
    ui_wm_window_t* target = ui_wm_find_window(wm, window_id);
    if (!wm || !target) {
        return;
    }
    ui_wm_normalize_z_order(wm);
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].z_order > max_z) {
            max_z = wm->windows[i].z_order;
        }
    }
    target->z_order = max_z + 1;
    ui_wm_set_active_window(wm, window_id);
}

static inline ui_control_t* ui_wm_alloc_control(ui_window_manager_t* wm, int type, int window_id, ui_rect_t bounds) {
    ui_control_t* control;
    if (!wm || wm->control_count >= UI_WM_MAX_CONTROLS || !ui_wm_find_window(wm, window_id)) {
        return 0;
    }
    control = &wm->controls[wm->control_count++];
    control->id = wm->next_control_id++;
    control->type = type;
    control->window_id = window_id;
    control->bounds = bounds;
    control->text = "";
    control->text_buffer = 0;
    control->text_buffer_len = 0;
    control->text_len = 0;
    control->visible = 1;
    control->enabled = 1;
    control->focused = 0;
    control->pressed = 0;
    return control;
}

static inline int ui_wm_add_label(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, const char* text) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_LABEL, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->text = text ? text : "";
    return control->id;
}

static inline int ui_wm_add_button(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, const char* text) {
    ui_control_t* control = ui_wm_alloc_control(wm, UI_CONTROL_BUTTON, window_id, bounds);
    if (!control) {
        return 0;
    }
    control->text = text ? text : "";
    return control->id;
}

static inline int ui_wm_add_textinput(ui_window_manager_t* wm, int window_id, ui_rect_t bounds, char* text_buffer, int text_buffer_len) {
    ui_control_t* control;
    int len = 0;

    if (!text_buffer || text_buffer_len <= 0) {
        return 0;
    }

    control = ui_wm_alloc_control(wm, UI_CONTROL_TEXTINPUT, window_id, bounds);
    if (!control) {
        return 0;
    }

    while (len < (text_buffer_len - 1) && text_buffer[len] != '\0') {
        len++;
    }
    text_buffer[len] = '\0';

    control->text = text_buffer;
    control->text_buffer = text_buffer;
    control->text_buffer_len = text_buffer_len;
    control->text_len = len;
    return control->id;
}

static inline ui_rect_t ui_wm_control_abs_bounds(const ui_window_manager_t* wm, const ui_control_t* control) {
    const ui_wm_window_t* win;
    ui_rect_t client;

    if (!wm || !control) {
        return ui_rect_make(0, 0, 0, 0);
    }

    win = ui_wm_find_window_const(wm, control->window_id);
    if (!win) {
        return ui_rect_make(0, 0, 0, 0);
    }

    client = ui_window_client_rect(&win->window);
    return ui_rect_make(client.x + control->bounds.x, client.y + control->bounds.y, control->bounds.w, control->bounds.h);
}

static inline void ui_wm_set_focus_control(ui_window_manager_t* wm, int control_id) {
    int i;
    if (!wm) {
        return;
    }
    wm->focused_control_id = control_id;
    for (i = 0; i < wm->control_count; i++) {
        wm->controls[i].focused = (wm->controls[i].id == control_id);
    }
}

static inline int ui_wm_hit_test_control(const ui_window_manager_t* wm, int window_id, int x, int y) {
    int i;
    int found_id = 0;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->control_count; i++) {
        const ui_control_t* control = &wm->controls[i];
        ui_rect_t abs_bounds;
        if (!control->visible || !control->enabled || control->window_id != window_id) {
            continue;
        }
        abs_bounds = ui_wm_control_abs_bounds(wm, control);
        if (ui_rect_contains(&abs_bounds, x, y)) {
            found_id = control->id;
        }
    }
    return found_id;
}

static inline int ui_wm_top_window_at(const ui_window_manager_t* wm, int x, int y) {
    int i;
    int best_id = 0;
    int best_z = -2147483647;
    if (!wm) {
        return 0;
    }
    for (i = 0; i < wm->window_count; i++) {
        const ui_wm_window_t* win = &wm->windows[i];
        if (!win->visible) {
            continue;
        }
        if (ui_rect_contains(&win->window.bounds, x, y) && win->z_order >= best_z) {
            best_z = win->z_order;
            best_id = win->id;
        }
    }
    return best_id;
}

static inline int ui_wm_top_visible_window_id(const ui_window_manager_t* wm) {
    int i;
    int best_id = 0;
    int best_z = -2147483647;

    if (!wm) {
        return 0;
    }

    for (i = 0; i < wm->window_count; i++) {
        const ui_wm_window_t* win = &wm->windows[i];

        if (!win->visible) {
            continue;
        }
        if (win->z_order >= best_z) {
            best_z = win->z_order;
            best_id = win->id;
        }
    }

    return best_id;
}

static inline int ui_wm_dispatch_mouse(ui_window_manager_t* wm, int x, int y, int left_down, int left_pressed,
    int left_released, int* out_window_id, int* out_control_id, int* out_hit_close) {
    int i;
    int top_window_id;
    ui_wm_window_t* window;
    int control_id;
    int hit_close;

    if (!wm) {
        return 0;
    }
    (void)left_down;
    if (out_window_id) {
        *out_window_id = 0;
    }
    if (out_control_id) {
        *out_control_id = 0;
    }
    if (out_hit_close) {
        *out_hit_close = 0;
    }

    top_window_id = ui_wm_top_window_at(wm, x, y);
    if (!top_window_id) {
        if (left_released) {
            for (i = 0; i < wm->control_count; i++) {
                wm->controls[i].pressed = 0;
            }
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
        } else if (left_pressed) {
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
        }
        return 0;
    }

    window = ui_wm_find_window(wm, top_window_id);
    if (!window || !window->visible) {
        return 0;
    }

    if (left_pressed) {
        ui_wm_bring_to_front(wm, top_window_id);
    }
    if (out_window_id) {
        *out_window_id = top_window_id;
    }

    hit_close = ui_window_hit_close(&window->window, x, y);
    if (hit_close) {
        if (out_hit_close) {
            *out_hit_close = 1;
        }
        if (left_pressed) {
            wm->pressed_window_id = top_window_id;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 1;
        }
        if (left_released) {
            int activated = wm->pressed_hit_close && (wm->pressed_window_id == top_window_id);
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            return activated ? 1 : 0;
        }
        return 0;
    }

    control_id = ui_wm_hit_test_control(wm, top_window_id, x, y);
    if (control_id) {
        ui_control_t* control = ui_wm_find_control(wm, control_id);
        if (control && left_pressed) {
            wm->pressed_window_id = top_window_id;
            wm->pressed_control_id = control_id;
            wm->pressed_hit_close = 0;
            ui_wm_set_focus_control(wm, control_id);
            for (i = 0; i < wm->control_count; i++) {
                wm->controls[i].pressed = 0;
            }
            if (control->type == UI_CONTROL_BUTTON) {
                control->pressed = 1;
            }
        }

        if (control && left_released) {
            int activated = (control->type == UI_CONTROL_BUTTON)
                && (wm->pressed_window_id == top_window_id)
                && (wm->pressed_control_id == control_id)
                && control->pressed;
            control->pressed = 0;
            wm->pressed_window_id = 0;
            wm->pressed_control_id = 0;
            wm->pressed_hit_close = 0;
            if (out_control_id) {
                *out_control_id = control_id;
            }
            return activated ? 1 : 0;
        }

        if (out_control_id) {
            *out_control_id = control_id;
        }
    }

    if (left_pressed) {
        wm->pressed_window_id = top_window_id;
        wm->pressed_control_id = 0;
        wm->pressed_hit_close = 0;
        for (i = 0; i < wm->control_count; i++) {
            wm->controls[i].pressed = 0;
        }
    }

    if (left_released) {
        for (i = 0; i < wm->control_count; i++) {
            wm->controls[i].pressed = 0;
        }
        wm->pressed_window_id = 0;
        wm->pressed_control_id = 0;
        wm->pressed_hit_close = 0;
    }

    return 0;
}

static inline int ui_wm_dispatch_key(ui_window_manager_t* wm, char key) {
    ui_control_t* focused;

    if (!wm || wm->focused_control_id == 0) {
        return 0;
    }

    focused = ui_wm_find_control(wm, wm->focused_control_id);
    if (!focused || focused->type != UI_CONTROL_TEXTINPUT || !focused->enabled || !focused->text_buffer || focused->text_buffer_len <= 0) {
        return 0;
    }

    if (key == 8) {
        if (focused->text_len > 0) {
            focused->text_len--;
            focused->text_buffer[focused->text_len] = '\0';
            return 1;
        }
        return 0;
    }

    if (key >= 32 && key <= 126) {
        if (focused->text_len < (focused->text_buffer_len - 1)) {
            focused->text_buffer[focused->text_len++] = key;
            focused->text_buffer[focused->text_len] = '\0';
            return 1;
        }
    }

    return 0;
}

static inline void ui_wm_close_window(ui_window_manager_t* wm, int window_id) {
    int i;
    if (!wm) {
        return;
    }
    for (i = 0; i < wm->window_count; i++) {
        if (wm->windows[i].id == window_id) {
            wm->windows[i].visible = 0;
            break;
        }
    }

    for (i = 0; i < wm->control_count; i++) {
        if (wm->controls[i].window_id != window_id) {
            continue;
        }
        wm->controls[i].pressed = 0;
        wm->controls[i].focused = 0;
        if (wm->focused_control_id == wm->controls[i].id) {
            wm->focused_control_id = 0;
        }
        if (wm->pressed_control_id == wm->controls[i].id) {
            wm->pressed_control_id = 0;
        }
    }

    if (wm->active_window_id == window_id) {
        int next_active = ui_wm_top_visible_window_id(wm);
        ui_wm_set_active_window(wm, next_active);
    }
    if (wm->pressed_window_id == window_id) {
        wm->pressed_window_id = 0;
        wm->pressed_hit_close = 0;
    }
}

static inline void ui_wm_draw(const minidos_app_api_t* api, const ui_window_manager_t* wm, int screen_w, int screen_h, const char* desktop_title) {
    int i;
    int drawn[UI_WM_MAX_WINDOWS];
    int draw_count;
    if (!wm) {
        return;
    }

    ui_draw_desktop(api, &wm->theme, screen_w, screen_h, desktop_title);

    for (i = 0; i < UI_WM_MAX_WINDOWS; i++) {
        drawn[i] = 0;
    }

    for (draw_count = 0; draw_count < wm->window_count; draw_count++) {
        int best_index = -1;
        int best_z = 2147483647;

        for (i = 0; i < wm->window_count; i++) {
            const ui_wm_window_t* win = &wm->windows[i];

            if (!win->visible || drawn[i]) {
                continue;
            }
            if (best_index >= 0 && win->z_order >= best_z) {
                continue;
            }

            best_index = i;
            best_z = win->z_order;
        }

        if (best_index < 0) {
            break;
        }

        drawn[best_index] = 1;

        {
            const ui_wm_window_t* win = &wm->windows[best_index];
            int c;

            ui_draw_window(api, &wm->theme, &win->window);

            for (c = 0; c < wm->control_count; c++) {
                const ui_control_t* control = &wm->controls[c];
                ui_rect_t abs_bounds;
                if (!control->visible || control->window_id != win->id) {
                    continue;
                }
                abs_bounds = ui_wm_control_abs_bounds(wm, control);

                if (control->type == UI_CONTROL_LABEL) {
                    ui_draw_label(api, abs_bounds.x, abs_bounds.y, control->text ? control->text : "", &wm->theme, wm->theme.field_bg);
                } else if (control->type == UI_CONTROL_BUTTON) {
                    ui_button_t button;
                    button.bounds = abs_bounds;
                    button.label = control->text ? control->text : "";
                    button.pressed = control->pressed;
                    button.focused = control->focused;
                    button.enabled = control->enabled;
                    ui_draw_button(api, &wm->theme, &button);
                } else if (control->type == UI_CONTROL_TEXTINPUT) {
                    ui_draw_text_box(api, &wm->theme, abs_bounds, control->text ? control->text : "", control->focused);
                }
            }
        }
    }
}

static inline void ui_draw_button(const minidos_app_api_t* api, const ui_theme_t* theme, const ui_button_t* button) {
    ui_rect_t inner;
    unsigned int fg;
    if (!theme || !button) {
        return;
    }

    ui_fill_rect(api, button->bounds, theme->face);
    if (button->pressed) {
        ui_bevel_rect(api, button->bounds, theme->shadow, theme->light);
        if (button->bounds.w > 2 && button->bounds.h > 2) {
            ui_bevel_rect(api, ui_rect_inset(button->bounds, 1), theme->dark_shadow, theme->face_alt);
        }
    } else {
        ui_bevel_rect(api, button->bounds, theme->light, theme->dark_shadow);
        if (button->bounds.w > 2 && button->bounds.h > 2) {
            ui_bevel_rect(api, ui_rect_inset(button->bounds, 1), theme->face_alt, theme->shadow);
        }
    }

    inner = ui_rect_inset(button->bounds, 3);
    ui_fill_rect(api, inner, theme->face);
    fg = button->enabled ? theme->text : theme->text_disabled;

    if (button->focused && button->bounds.w > 8 && button->bounds.h > 8) {
        ui_frame_rect(api, ui_rect_inset(button->bounds, 4), theme->dark_shadow);
    }

    ui_draw_label_centered(api,
        ui_rect_make(inner.x + (button->pressed ? 1 : 0), inner.y + (button->pressed ? 1 : 0), inner.w, inner.h),
        button->label ? button->label : "",
        fg,
        theme->face);
}

static inline void ui_draw_window(const minidos_app_api_t* api, const ui_theme_t* theme, const ui_window_t* window) {
    ui_rect_t title_rect;
    ui_rect_t client_rect;
    ui_button_t close_button;
    unsigned int title_bg;
    unsigned int title_fg;

    if (!theme || !window) {
        return;
    }

    ui_draw_panel(api, theme, window->bounds, 1);

    title_rect = ui_window_title_bar_rect(window);
    title_bg = window->active ? theme->title_active_bg : theme->title_inactive_bg;
    title_fg = window->active ? theme->title_active_text : theme->title_inactive_text;
    ui_fill_rect(api, title_rect, title_bg);
    ui_draw_text(api, title_rect.x + 6, title_rect.y + 4, window->title ? window->title : "", title_fg, title_bg);

    client_rect = ui_window_client_rect(window);
    ui_fill_rect(api, client_rect, theme->field_bg);

    if (window->has_close_button && title_rect.w >= 20) {
        close_button.bounds = ui_window_close_button_rect(window);
        close_button.label = "X";
        close_button.pressed = 0;
        close_button.focused = 0;
        close_button.enabled = 1;
        ui_draw_button(api, theme, &close_button);
    }
}

static inline void ui_draw_desktop(const minidos_app_api_t* api, const ui_theme_t* theme, int width, int height, const char* title) {
    ui_button_t start_button;
    ui_rect_t taskbar;
    ui_rect_t brand;

    if (!theme || width <= 0 || height <= 0) {
        return;
    }

    int drew_bitmap = 0;
    ui_clear(api, theme->desktop_bg);
    if (theme->desktop_bg_bitmap) {
        drew_bitmap = ui_draw_bitmap(api, theme->desktop_bg_bitmap, 0, 0, width, height);
    }
    (void)drew_bitmap;
    ui_fill_rect(api, ui_rect_make(0, 0, width, 2), theme->desktop_accent);

    taskbar = ui_rect_make(0, height - 28, width, 28);
    ui_draw_panel(api, theme, taskbar, 1);

    start_button.bounds = ui_rect_make(4, height - 24, 58, 20);
    start_button.label = "Start";
    start_button.pressed = 0;
    start_button.focused = 0;
    start_button.enabled = 1;
    ui_draw_button(api, theme, &start_button);

    brand = ui_rect_make(width - 130, height - 24, 122, 18);
    ui_draw_panel(api, theme, brand, 0);
    ui_draw_label_centered(api, brand, title ? title : "MiniDOS", theme->text, theme->face);
}

static inline void ui_draw_cursor(const minidos_app_api_t* api, int x, int y, unsigned int fill, unsigned int outline) {
    int row;
    int draw_x = x - UI_CURSOR_HOTSPOT_X;
    int draw_y = y - UI_CURSOR_HOTSPOT_Y;

    for (row = 0; row < UI_CURSOR_BITMAP_HEIGHT; row++) {
        int col = 0;

        while (col < UI_CURSOR_BITMAP_WIDTH) {
            unsigned char pixel = ui_cursor_bitmap[(row * UI_CURSOR_BITMAP_WIDTH) + col];
            int start;
            unsigned int color;

            if (pixel == UI_CURSOR_PIXEL_TRANSPARENT) {
                col++;
                continue;
            }

            start = col;
            color = (pixel == UI_CURSOR_PIXEL_OUTLINE) ? outline : fill;
            while (col < UI_CURSOR_BITMAP_WIDTH
                && ui_cursor_bitmap[(row * UI_CURSOR_BITMAP_WIDTH) + col] == pixel) {
                col++;
            }
            ui_fill_rect(api, ui_rect_make(draw_x + start, draw_y + row, col - start, 1), color);
        }
    }
}

#endif
