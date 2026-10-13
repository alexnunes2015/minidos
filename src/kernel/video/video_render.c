#include "video_internal.h"

static inline void write_frontbuffer_pixel_packed(volatile u8* dst, int fb_bytes, u32 packed) {
    if (fb_bytes == 4) {
        *(volatile u32*)dst = packed;
        return;
    }

    if (fb_bytes == 3) {
        dst[0] = (u8)(packed & 0xFFu);
        dst[1] = (u8)((packed >> 8) & 0xFFu);
        dst[2] = (u8)((packed >> 16) & 0xFFu);
        return;
    }

    *(volatile u16*)dst = (u16)packed;
}

static u32 scale_component(u8 c, u8 bits, u8 pos) {
    if (bits == 0) {
        return 0;
    }

    u32 max = (1u << bits) - 1u;
    u32 value = ((u32)c * max + 127u) / 255u;
    return value << pos;
}

static u32 pack_rgb(u8 r, u8 g, u8 b) {
    return scale_component(r, red_size, red_pos)
        | scale_component(g, green_size, green_pos)
        | scale_component(b, blue_size, blue_pos);
}

void write_frontbuffer_pixel(volatile u8* dst, u32 rgb) {
    int fb_bytes = video_fb_bytes_per_pixel();
    u32 packed = pack_rgb((u8)((rgb >> 16) & 0xFFu), (u8)((rgb >> 8) & 0xFFu), (u8)(rgb & 0xFFu));
    write_frontbuffer_pixel_packed(dst, fb_bytes, packed);
}

void fill_frontbuffer_rect_rgb(int x, int y, int w, int h, u32 rgb) {
    if (!graphics_mode || w <= 0 || h <= 0) {
        return;
    }

    video_lock();
    fill_frontbuffer_rect_rgb_locked(x, y, w, h, rgb);
    video_unlock();
}

void fill_frontbuffer_rect_rgb_locked(int x, int y, int w, int h, u32 rgb) {
    int x0;
    int y0;
    int x1;
    int y1;
    int py;
    int fb_bytes;

    if (!graphics_mode || w <= 0 || h <= 0) {
        return;
    }

    x0 = x;
    y0 = y;
    x1 = x + w;
    y1 = y + h;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > fb_width) {
        x1 = fb_width;
    }
    if (y1 > fb_height) {
        y1 = fb_height;
    }

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    x = x0;
    y = y0;
    w = x1 - x0;
    h = y1 - y0;
    fb_bytes = video_fb_bytes_per_pixel();

    u32 packed = pack_rgb((u8)((rgb >> 16) & 0xFFu), (u8)((rgb >> 8) & 0xFFu), (u8)(rgb & 0xFFu));

    if (fb_bytes == 4) {
        /* Fast path: 32-bit word writes to VESA LFB */
        for (py = 0; py < h; py++) {
            volatile u32* row = (volatile u32*)(void*)(fb + ((y + py) * fb_pitch) + (x * 4));
            int px;
            for (px = 0; px < w; px++) {
                row[px] = packed;
            }
        }
    } else {
        /* Fallback: per-pixel write for 16/24-bit modes */
        for (py = 0; py < h; py++) {
            volatile u8* row = fb + ((y + py) * fb_pitch) + (x * fb_bytes);
            int px;
            for (px = 0; px < w; px++) {
                write_frontbuffer_pixel_packed(row + (px * fb_bytes), fb_bytes, packed);
            }
        }
    }
}

void fill_rect_rgb(int x, int y, int w, int h, u32 rgb) {
    if (!graphics_mode || w <= 0 || h <= 0) {
        return;
    }

    video_lock();
    fill_rect_rgb_locked(x, y, w, h, rgb);
    video_unlock();
}

void fill_rect_rgb_locked(int x, int y, int w, int h, u32 rgb) {
    int x0;
    int y0;
    int x1;
    int y1;
    int py;

    if (!graphics_mode || w <= 0 || h <= 0) {
        return;
    }

    x0 = x;
    y0 = y;
    x1 = x + w;
    y1 = y + h;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > fb_width) {
        x1 = fb_width;
    }
    if (y1 > fb_height) {
        y1 = fb_height;
    }

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    x = x0;
    y = y0;
    w = x1 - x0;
    h = y1 - y0;

    if (backbuffer_ready) {
        if (!video_backbuffer_rect_fits(x, y, w, h)) {
            video_disable_backbuffer_locked();
        } else {
            video_backbuffer_fill_base = video_backbuffer + (y * backbuffer_pitch) + (x * VIDEO_BACKBUFFER_BYTES_PER_PIXEL);
            video_backbuffer_fill_pitch = backbuffer_pitch;
            video_backbuffer_fill_h = h;
            video_backbuffer_fill_w = w;
            video_backbuffer_fill_rgb = rgb;
            video_backbuffer_fill_rect32();
            return;
        }
    }

    u32 packed = pack_rgb((u8)((rgb >> 16) & 0xFFu), (u8)((rgb >> 8) & 0xFFu), (u8)(rgb & 0xFFu));
    int fb_bytes = video_fb_bytes_per_pixel();

    if (fb_bytes == 4) {
        /* Fast path for 32-bit VESA LFB: word-aligned writes */
        for (py = 0; py < h; py++) {
            volatile u32* row = (volatile u32*)(void*)(fb + ((y + py) * fb_pitch) + (x * 4));
            int px;
            for (px = 0; px < w; px++) {
                row[px] = packed;
            }
        }
    } else {
        for (py = 0; py < h; py++) {
            volatile u8* row = fb + ((y + py) * fb_pitch) + (x * fb_bytes);
            int px;
            for (px = 0; px < w; px++) {
                write_frontbuffer_pixel_packed(row + (px * fb_bytes), fb_bytes, packed);
            }
        }
    }
}

void draw_pixel(int x, int y, u32 rgb) {
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) {
        return;
    }

    video_lock();
    draw_pixel_locked(x, y, rgb);
    video_unlock();
}

void draw_pixel_locked(int x, int y, u32 rgb) {
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) {
        return;
    }

    if (backbuffer_ready) {
        if (!video_backbuffer_rect_fits(x, y, 1, 1)) {
            video_disable_backbuffer_locked();
        } else {
            volatile u32* p = (volatile u32*)(void*)(video_backbuffer + (y * backbuffer_pitch)
                + (x * VIDEO_BACKBUFFER_BYTES_PER_PIXEL));
            *p = rgb;
            return;
        }
    }

    write_frontbuffer_pixel(fb + (y * fb_pitch) + (x * video_fb_bytes_per_pixel()), rgb);
}

void clear_graphics(u32 rgb) {
    fill_rect_rgb(0, 0, fb_width, fb_height, rgb);
}

void render_cell(int col, int row, char c) {
    int x0 = text_origin_x + col * FONT_W;
    int y0 = text_origin_y + row * FONT_H;
    const u8* glyph = glyph_for_char(c);
    int y;

    video_lock();
    for (y = 0; y < FONT_H; y++) {
        u8 bits = glyph[y];
        int x;
        for (x = 0; x < FONT_W; x++) {
            u32 color = (bits & (1u << (7 - x))) ? COLOR_FG : COLOR_BG;
            draw_pixel_locked(x0 + x, y0 + y, color);
        }
    }
    video_unlock();
}

void redraw_text_buffer(void) {
    for (int y = 0; y < text_rows; y++) {
        for (int x = 0; x < text_cols; x++) {
            render_cell(x, y, text_buffer[y][x]);
        }
    }
}

void video_note_cell(int col, int row) {
    video_note_dirty(text_origin_x + (col * FONT_W), text_origin_y + (row * FONT_H), FONT_W, FONT_H);
}

void video_note_text_area(void) {
    video_note_dirty(text_origin_x, text_origin_y, text_cols * FONT_W, text_rows * FONT_H);
}

#define VIDEO_SURFACE_MAX_DIMENSION 16384
#define VIDEO_SURFACE_COORD_LIMIT 32768

static u32 read_u32_le(const unsigned char* p) {
    return (u32)p[0]
        | ((u32)p[1] << 8)
        | ((u32)p[2] << 16)
        | ((u32)p[3] << 24);
}

static u8 unpack_component(u32 packed, u8 size, u8 position) {
    u32 max_value;
    u32 value;

    if (size == 0 || size > 8) {
        return 0;
    }
    max_value = (1u << size) - 1u;
    value = (packed >> position) & max_value;
    return (u8)((value * 255u + (max_value >> 1)) / max_value);
}

static u32 read_draw_target_rgb_locked(int x, int y) {
    if (backbuffer_ready) {
        const volatile u32* pixel = (const volatile u32*)(const void*)(video_backbuffer
            + (y * backbuffer_pitch) + (x * VIDEO_BACKBUFFER_BYTES_PER_PIXEL));
        return *pixel & 0x00FFFFFFu;
    }

    {
        int bytes = video_fb_bytes_per_pixel();
        const volatile u8* pixel = fb + (y * fb_pitch) + (x * bytes);
        u32 packed = (u32)pixel[0] | ((u32)pixel[1] << 8);
        u8 r;
        u8 g;
        u8 b;

        if (bytes >= 3) {
            packed |= (u32)pixel[2] << 16;
        }
        if (bytes == 4) {
            packed |= (u32)pixel[3] << 24;
        }
        r = unpack_component(packed, red_size, red_pos);
        g = unpack_component(packed, green_size, green_pos);
        b = unpack_component(packed, blue_size, blue_pos);
        return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
}

static u32 blend_rgb_source_over(u32 src, u32 dst, u32 alpha) {
    u32 inverse = 255u - alpha;
    u32 r = ((((src >> 16) & 0xFFu) * alpha)
        + (((dst >> 16) & 0xFFu) * inverse) + 127u) / 255u;
    u32 g = ((((src >> 8) & 0xFFu) * alpha)
        + (((dst >> 8) & 0xFFu) * inverse) + 127u) / 255u;
    u32 b = (((src & 0xFFu) * alpha)
        + ((dst & 0xFFu) * inverse) + 127u) / 255u;
    return (r << 16) | (g << 8) | b;
}

int video_blit_surface(const video_surface_blit_t* b) {
    int x0, y0, x1, y1;
    int dest_w;
    int dest_h;
    int py;
    unsigned int row_bytes;
    unsigned int remaining_rows;

    init_video_once();
    if (!b || !graphics_mode) {
        return 0;
    }
    if (!b->buffer
        || b->width <= 0 || b->height <= 0
        || b->width > VIDEO_SURFACE_MAX_DIMENSION
        || b->height > VIDEO_SURFACE_MAX_DIMENSION
        || b->stride <= 0
        || (b->format != VIDEO_SURFACE_FORMAT_LEGACY_XRGB8888
            && b->format != VIDEO_SURFACE_FORMAT_XRGB8888_LE
            && b->format != VIDEO_SURFACE_FORMAT_ARGB8888_LE)) {
        return 0;
    }
    row_bytes = (unsigned int)b->width * 4U;
    remaining_rows = (unsigned int)(b->height - 1);
    if ((unsigned int)b->stride < row_bytes
        || remaining_rows > (0xFFFFFFFFU - row_bytes) / (unsigned int)b->stride) {
        return 0;
    }

    dest_w = (b->dest_w > 0) ? b->dest_w : b->width;
    dest_h = (b->dest_h > 0) ? b->dest_h : b->height;
    if (dest_w <= 0 || dest_h <= 0
        || dest_w > VIDEO_SURFACE_MAX_DIMENSION
        || dest_h > VIDEO_SURFACE_MAX_DIMENSION
        || b->dest_x < -VIDEO_SURFACE_COORD_LIMIT || b->dest_x > VIDEO_SURFACE_COORD_LIMIT
        || b->dest_y < -VIDEO_SURFACE_COORD_LIMIT || b->dest_y > VIDEO_SURFACE_COORD_LIMIT
        || b->dest_x > 0x7FFFFFFF - dest_w
        || b->dest_y > 0x7FFFFFFF - dest_h) {
        return 0;
    }

    /* Full destination rect */
    x0 = b->dest_x;
    y0 = b->dest_y;
    x1 = b->dest_x + dest_w;
    y1 = b->dest_y + dest_h;

    /* Apply clip rect when valid */
    if (b->clip_w > 0 && b->clip_h > 0) {
        if (b->clip_w > VIDEO_SURFACE_MAX_DIMENSION
            || b->clip_h > VIDEO_SURFACE_MAX_DIMENSION
            || b->clip_x < -VIDEO_SURFACE_COORD_LIMIT || b->clip_x > VIDEO_SURFACE_COORD_LIMIT
            || b->clip_y < -VIDEO_SURFACE_COORD_LIMIT || b->clip_y > VIDEO_SURFACE_COORD_LIMIT
            || b->clip_x > 0x7FFFFFFF - b->clip_w
            || b->clip_y > 0x7FFFFFFF - b->clip_h) {
            return 0;
        }
        if (b->clip_x             > x0) x0 = b->clip_x;
        if (b->clip_y             > y0) y0 = b->clip_y;
        if (b->clip_x + b->clip_w < x1) x1 = b->clip_x + b->clip_w;
        if (b->clip_y + b->clip_h < y1) y1 = b->clip_y + b->clip_h;
    }

    /* Clamp to framebuffer */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb_width)  x1 = fb_width;
    if (y1 > fb_height) y1 = fb_height;

    if (x0 >= x1 || y0 >= y1) {
        return 1;
    }

    video_lock();

    if (b->format == VIDEO_SURFACE_FORMAT_XRGB8888_LE
        && dest_w == b->width && dest_h == b->height
        && backbuffer_ready
        && video_backbuffer_rect_fits(x0, y0, x1 - x0, y1 - y0)) {
        int source_x = x0 - b->dest_x;
        int source_y = y0 - b->dest_y;
        int copy_w = x1 - x0;

        for (py = 0; py < y1 - y0; py++) {
            const unsigned char* src_bytes = b->buffer
                + ((unsigned int)(source_y + py) * (unsigned int)b->stride)
                + (unsigned int)(source_x * 4);
            u32* dst = (u32*)(void*)(video_backbuffer
                + ((y0 + py) * backbuffer_pitch) + (x0 * VIDEO_BACKBUFFER_BYTES_PER_PIXEL));
            int px;

            if ((((unsigned int)src_bytes | (unsigned int)b->stride) & 3U) == 0U) {
                const u32* src = (const u32*)(const void*)src_bytes;
                for (px = 0; px < copy_w; px++) {
                    dst[px] = src[px] & 0x00FFFFFFu;
                }
            } else {
                for (px = 0; px < copy_w; px++) {
                    dst[px] = read_u32_le(src_bytes + (px * 4)) & 0x00FFFFFFu;
                }
            }
        }

        video_note_dirty_locked(x0, y0, x1 - x0, y1 - y0);
        video_unlock();
        video_maybe_present_pending();
        return 1;
    }

    {
        int y_product = (y0 - b->dest_y) * b->height;
        int src_y = y_product / dest_h;
        int y_fraction = y_product % dest_h;
        int y_whole_step = b->height / dest_h;
        int y_fraction_step = b->height % dest_h;
        int x_product = (x0 - b->dest_x) * b->width;
        int src_x_start = x_product / dest_w;
        int x_fraction_start = x_product % dest_w;
        int x_whole_step = b->width / dest_w;
        int x_fraction_step = b->width % dest_w;

        for (py = y0; py < y1; py++) {
            int px;
            int src_x = src_x_start;
            int x_fraction = x_fraction_start;
            const unsigned char* src_row;

            if (src_y >= b->height) {
                src_y = b->height - 1;
            }
            src_row = b->buffer + ((unsigned int)src_y * (unsigned int)b->stride);

            for (px = x0; px < x1; px++) {
                const unsigned char* p;
                u32 rgb;
                u32 alpha = 255u;

                if (src_x >= b->width) {
                    src_x = b->width - 1;
                }
                p = src_row + (unsigned int)(src_x * 4);
                if (b->format == VIDEO_SURFACE_FORMAT_LEGACY_XRGB8888) {
                    rgb = ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
                } else {
                    u32 packed = read_u32_le(p);
                    rgb = packed & 0x00FFFFFFu;
                    if (b->format == VIDEO_SURFACE_FORMAT_ARGB8888_LE) {
                        alpha = packed >> 24;
                    }
                }

                if (alpha == 255u) {
                    draw_pixel_locked(px, py, rgb);
                } else if (alpha != 0u) {
                    draw_pixel_locked(px, py,
                        blend_rgb_source_over(rgb, read_draw_target_rgb_locked(px, py), alpha));
                }

                src_x += x_whole_step;
                x_fraction += x_fraction_step;
                if (x_fraction >= dest_w) {
                    src_x++;
                    x_fraction -= dest_w;
                }
            }

            src_y += y_whole_step;
            y_fraction += y_fraction_step;
            if (y_fraction >= dest_h) {
                src_y++;
                y_fraction -= dest_h;
            }
        }
    }

    video_note_dirty_locked(x0, y0, x1 - x0, y1 - y0);
    video_unlock();
    video_maybe_present_pending();
    return 1;
}

int video_blit_surface_desc(const void* blit_desc) {
    return video_blit_surface((const video_surface_blit_t*)blit_desc);
}
