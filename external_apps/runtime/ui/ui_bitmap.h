#ifndef MINIDOS_UI_BITMAP_H
#define MINIDOS_UI_BITMAP_H

#include "ui_defs.h"

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

static inline int ui_wallpaper_surface_matches(const char* path) {
    return g_ui_wallpaper_surface.valid && path && ui_path_equal(g_ui_wallpaper_surface.path, path);
}

static inline int ui_wallpaper_surface_blit_scaled(const minidos_app_api_t* api,
    int dst_x, int dst_y, int dst_w, int dst_h,
    int clip_x, int clip_y, int clip_w, int clip_h) {
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
    blit.dest_w = dst_w;
    blit.dest_h = dst_h;
    return app_gfx_surface_blit(api, &blit);
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
