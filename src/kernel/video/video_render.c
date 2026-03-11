#include "video_internal.h"

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
    u32 packed = pack_rgb((u8)((rgb >> 16) & 0xFFu), (u8)((rgb >> 8) & 0xFFu), (u8)(rgb & 0xFFu));

    if (fb_bytes_per_pixel == 4) {
        *(volatile u32*)dst = packed;
        return;
    }

    if (fb_bytes_per_pixel == 3) {
        dst[0] = (u8)(packed & 0xFFu);
        dst[1] = (u8)((packed >> 8) & 0xFFu);
        dst[2] = (u8)((packed >> 16) & 0xFFu);
        return;
    }

    *(volatile u16*)dst = (u16)packed;
}

void fill_rect_rgb(int x, int y, int w, int h, u32 rgb) {
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
            video_disable_backbuffer();
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

    for (py = 0; py < h; py++) {
        volatile u8* row = fb + ((y + py) * fb_pitch) + (x * fb_bytes_per_pixel);
        int px;

        for (px = 0; px < w; px++) {
            write_frontbuffer_pixel(row + (px * fb_bytes_per_pixel), rgb);
        }
    }
}

void draw_pixel(int x, int y, u32 rgb) {
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) {
        return;
    }

    if (backbuffer_ready) {
        if (!video_backbuffer_rect_fits(x, y, 1, 1)) {
            video_disable_backbuffer();
        } else {
            volatile u32* p = (volatile u32*)(void*)(video_backbuffer + (y * backbuffer_pitch)
                + (x * VIDEO_BACKBUFFER_BYTES_PER_PIXEL));
            *p = rgb;
            return;
        }
    }

    write_frontbuffer_pixel(fb + (y * fb_pitch) + (x * fb_bytes_per_pixel), rgb);
}

void clear_graphics(u32 rgb) {
    fill_rect_rgb(0, 0, fb_width, fb_height, rgb);
}

void render_cell(int col, int row, char c) {
    int x0 = text_origin_x + col * FONT_W;
    int y0 = text_origin_y + row * FONT_H;
    const u8* glyph = glyph_for_char(c);

    for (int y = 0; y < FONT_H; y++) {
        u8 bits = glyph[y];
        for (int x = 0; x < FONT_W; x++) {
            u32 color = (bits & (1u << (7 - x))) ? COLOR_FG : COLOR_BG;
            draw_pixel(x0 + x, y0 + y, color);
        }
    }
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
