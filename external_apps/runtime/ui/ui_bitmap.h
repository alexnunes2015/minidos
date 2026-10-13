#ifndef MINIDOS_UI_BITMAP_H
#define MINIDOS_UI_BITMAP_H

#include "ui_batch.h"

#ifdef MINIDOS_UI_IMPLEMENTATION
ui_bitmap_cache_t g_ui_bitmap_cache;
ui_wallpaper_surface_t g_ui_wallpaper_surface;
#else
extern ui_bitmap_cache_t g_ui_bitmap_cache;
extern ui_wallpaper_surface_t g_ui_wallpaper_surface;
#endif

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
 * Decode the loaded BMP file cache into little-endian packed 0x00RRGGBB.
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
            /* BMP and packed XRGB are both B,G,R in little-endian memory. */
            dst_row[x * 4 + 0] = px[0];   /* B */
            dst_row[x * 4 + 1] = px[1];   /* G */
            dst_row[x * 4 + 2] = px[2];   /* R */
            dst_row[x * 4 + 3] = 0;       /* X */
        }
    }

    s->width  = w;
    s->height = h;
    s->stride = w * 4;
    ui_path_copy(s->path, path);
    s->valid = 1;
    return 1;
}

static inline int ui_wallpaper_surface_matches(const char* path) {
    return g_ui_wallpaper_surface.valid && path && ui_path_equal(g_ui_wallpaper_surface.path, path);
}

static inline int ui_packed_xrgb_blit_scaled_fallback(const minidos_app_api_t* api,
    const unsigned char* pixels, int src_w, int src_h, int stride,
    int dst_x, int dst_y, int dst_w, int dst_h,
    int clip_x, int clip_y, int clip_w, int clip_h) {
    int left;
    int top;
    int right;
    int bottom;
    int x;
    int y;
    unsigned int row_bytes;
    unsigned int remaining_rows;
    int ok = 1;

    if (!api || !pixels
        || src_w <= 0 || src_h <= 0 || src_w > 16384 || src_h > 16384
        || dst_w <= 0 || dst_h <= 0 || dst_w > 16384 || dst_h > 16384
        || stride <= 0
        || dst_x < -32768 || dst_x > 32768
        || dst_y < -32768 || dst_y > 32768
        || dst_x > 0x7FFFFFFF - dst_w
        || dst_y > 0x7FFFFFFF - dst_h) {
        return 0;
    }
    row_bytes = (unsigned int)src_w * 4U;
    remaining_rows = (unsigned int)(src_h - 1);
    if ((unsigned int)stride < row_bytes
        || remaining_rows > (0xFFFFFFFFU - row_bytes) / (unsigned int)stride) {
        return 0;
    }

    left = dst_x;
    top = dst_y;
    right = dst_x + dst_w;
    bottom = dst_y + dst_h;
    if (clip_w > 0 && clip_h > 0) {
        int clip_right;
        int clip_bottom;

        if (clip_w > 16384 || clip_h > 16384
            || clip_x < -32768 || clip_x > 32768
            || clip_y < -32768 || clip_y > 32768
            || clip_x > 0x7FFFFFFF - clip_w
            || clip_y > 0x7FFFFFFF - clip_h) {
            return 0;
        }
        clip_right = clip_x + clip_w;
        clip_bottom = clip_y + clip_h;
        if (left < clip_x) { left = clip_x; }
        if (top < clip_y) { top = clip_y; }
        if (right > clip_right) { right = clip_right; }
        if (bottom > clip_bottom) { bottom = clip_bottom; }
    }
    if (left >= right || top >= bottom) {
        return 1;
    }

    for (y = top; y < bottom; y++) {
        int src_y = ((y - dst_y) * src_h) / dst_h;

        x = left;
        while (x < right) {
            int run_start = x;
            int src_x = ((x - dst_x) * src_w) / dst_w;
            const unsigned char* pixel = pixels
                + (unsigned int)src_y * (unsigned int)stride
                + (unsigned int)src_x * 4U;
            unsigned int color = ((unsigned int)pixel[2] << 16)
                | ((unsigned int)pixel[1] << 8)
                | (unsigned int)pixel[0];
            app_gfx_rect_t rect;

            x++;
            while (x < right) {
                unsigned int next_color;

                src_x = ((x - dst_x) * src_w) / dst_w;
                pixel = pixels
                    + (unsigned int)src_y * (unsigned int)stride
                    + (unsigned int)src_x * 4U;
                next_color = ((unsigned int)pixel[2] << 16)
                    | ((unsigned int)pixel[1] << 8)
                    | (unsigned int)pixel[0];
                if (next_color != color) {
                    break;
                }
                x++;
            }

            rect.x = run_start;
            rect.y = y;
            rect.w = x - run_start;
            rect.h = 1;
            rect.color = color;
            if (ui_gfx_rect_batch_enqueue(api, &rect) <= 0) {
                ok = 0;
            }
        }
    }

    return ok;
}

static inline int ui_wallpaper_surface_blit_scaled(const minidos_app_api_t* api,
    int dst_x, int dst_y, int dst_w, int dst_h,
    int clip_x, int clip_y, int clip_w, int clip_h) {
    ui_wallpaper_surface_t* s = &g_ui_wallpaper_surface;
    app_gfx_surface_blit_t blit;
    int result;

    if (!s->valid) {
        return 0;
    }

    (void)ui_gfx_rect_batch_flush(api);

    blit.buffer = s->pixels;
    blit.width  = s->width;
    blit.height = s->height;
    blit.stride = s->stride;
    blit.format = APP_SURFACE_FORMAT_XRGB8888_LE;
    blit.dest_x = dst_x;
    blit.dest_y = dst_y;
    blit.clip_x = clip_x;
    blit.clip_y = clip_y;
    blit.clip_w = clip_w;
    blit.clip_h = clip_h;
    blit.dest_w = dst_w;
    blit.dest_h = dst_h;
    result = app_gfx_surface_blit(api, &blit);
    if (result > 0) {
        return 1;
    }

    return ui_packed_xrgb_blit_scaled_fallback(api,
        s->pixels, s->width, s->height, s->stride,
        dst_x, dst_y, dst_w, dst_h,
        clip_x, clip_y, clip_w, clip_h);
}

/*
 * Blit the wallpaper surface at (dst_x, dst_y) using the fast surface syscall.
 * clip_x/y/w/h: region to restore (-1 to blit full surface).
 * Returns 1 on success, 0 if surface not loaded.
 */
static inline int ui_wallpaper_surface_blit(const minidos_app_api_t* api, int dst_x, int dst_y, int clip_x, int clip_y, int clip_w, int clip_h) {
    return ui_wallpaper_surface_blit_scaled(api,
        dst_x,
        dst_y,
        g_ui_wallpaper_surface.width,
        g_ui_wallpaper_surface.height,
        clip_x,
        clip_y,
        clip_w,
        clip_h);
}

static inline int ui_wallpaper_surface_ready(void) {
    return g_ui_wallpaper_surface.valid;
}

static inline int ui_wallpaper_surface_width(void) {
    return g_ui_wallpaper_surface.width;
}

static inline int ui_wallpaper_surface_height(void) {
    return g_ui_wallpaper_surface.height;
}

#endif
