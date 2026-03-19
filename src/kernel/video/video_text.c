#include "video_internal.h"
#include "timer.h"

#define GRAPHICS_CURSOR_BLINK_MS 500

static int graphics_cursor_enabled = 0;
static int graphics_cursor_drawn = 0;
static int graphics_cursor_x = 0;
static int graphics_cursor_y = 0;
static unsigned int graphics_cursor_last_tick = 0;

static unsigned int graphics_cursor_period_ticks(void) {
    unsigned int ticks = timer_ms_to_ticks_ceil(GRAPHICS_CURSOR_BLINK_MS);
    return ticks == 0 ? 1u : ticks;
}

static void graphics_cursor_hide(int present_now) {
    if (!graphics_cursor_drawn) {
        return;
    }
    if (graphics_cursor_x >= 0 && graphics_cursor_x < text_cols
        && graphics_cursor_y >= 0 && graphics_cursor_y < text_rows) {
        render_cell(graphics_cursor_x, graphics_cursor_y, text_buffer[graphics_cursor_y][graphics_cursor_x]);
        video_note_cell(graphics_cursor_x, graphics_cursor_y);
        if (present_now) {
            video_maybe_present_pending();
        }
    }

    graphics_cursor_drawn = 0;
}

static void graphics_cursor_show(int present_now) {
    int x0;
    int y0;

    if (!graphics_mode) {
        return;
    }
    if (cursor_x < 0 || cursor_y < 0 || cursor_x >= text_cols || cursor_y >= text_rows) {
        graphics_cursor_hide(present_now);
        return;
    }
    if (graphics_cursor_drawn && graphics_cursor_x == cursor_x && graphics_cursor_y == cursor_y) {
        return;
    }

    graphics_cursor_hide(present_now);

    x0 = text_origin_x + cursor_x * FONT_W;
    y0 = text_origin_y + cursor_y * FONT_H + (FONT_H - 2);
    fill_rect_rgb(x0, y0, FONT_W, 2, COLOR_FG);
    video_note_dirty(x0, y0, FONT_W, 2);
    if (present_now) {
        video_maybe_present_pending();
    }

    graphics_cursor_drawn = 1;
    graphics_cursor_x = cursor_x;
    graphics_cursor_y = cursor_y;
}

void video_cursor_reset_blink(void) {
    init_video_once();

    if (!graphics_mode) {
        update_cursor();
        return;
    }

    graphics_cursor_enabled = timer_is_ready();
    if (graphics_cursor_enabled) {
        graphics_cursor_last_tick = timer_get_ticks();
    }

    graphics_cursor_show(1);
}

void video_cursor_blink_step(void) {
    unsigned int period;
    unsigned int now;

    init_video_once();
    if (!graphics_mode || !graphics_cursor_enabled) {
        return;
    }
    if (!timer_is_ready()) {
        return;
    }

    period = graphics_cursor_period_ticks();
    if (period == 0) {
        return;
    }

    now = timer_get_ticks();
    if ((unsigned int)(now - graphics_cursor_last_tick) >= period) {
        graphics_cursor_last_tick = now;
        if (graphics_cursor_drawn) {
            graphics_cursor_hide(1);
        } else {
            graphics_cursor_show(1);
        }
    }
}

void update_cursor(void) {
    init_video_once();

    if (graphics_mode) {
        return;
    }

    unsigned short pos = (unsigned short)(cursor_y * TEXT_SCREEN_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

void cls(void) {
    init_video_once();

    for (int y = 0; y < text_rows; y++) {
        for (int x = 0; x < text_cols; x++) {
            text_buffer[y][x] = ' ';
        }
    }

    if (graphics_mode) {
        clear_graphics(COLOR_BG);
        fill_frontbuffer_rect_rgb(0, 0, fb_width, fb_height, COLOR_BG);
        video_clear_dirty();
    } else {
        volatile u8* video = TEXT_VIDEO_MEMORY;
        for (int i = 0; i < TEXT_SCREEN_WIDTH * TEXT_SCREEN_HEIGHT * 2; i += 2) {
            video[i] = ' ';
            video[i + 1] = 0x07;
        }
    }

    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

static void scroll_graphics(void) {
    for (int y = 1; y < text_rows; y++) {
        for (int x = 0; x < text_cols; x++) {
            text_buffer[y - 1][x] = text_buffer[y][x];
        }
    }

    for (int x = 0; x < text_cols; x++) {
        text_buffer[text_rows - 1][x] = ' ';
    }

    redraw_text_buffer();
    cursor_y = text_rows - 1;
    video_note_text_area();
    video_maybe_present_pending();
}

static void scroll_text(void) {
    volatile u8* video = TEXT_VIDEO_MEMORY;

    for (int y = 0; y < TEXT_SCREEN_HEIGHT - 1; y++) {
        for (int x = 0; x < TEXT_SCREEN_WIDTH * 2; x++) {
            video[y * TEXT_SCREEN_WIDTH * 2 + x] = video[(y + 1) * TEXT_SCREEN_WIDTH * 2 + x];
        }
    }

    for (int x = 0; x < TEXT_SCREEN_WIDTH * 2; x += 2) {
        video[(TEXT_SCREEN_HEIGHT - 1) * TEXT_SCREEN_WIDTH * 2 + x] = ' ';
        video[(TEXT_SCREEN_HEIGHT - 1) * TEXT_SCREEN_WIDTH * 2 + x + 1] = 0x07;
    }

    cursor_y = TEXT_SCREEN_HEIGHT - 1;
}

void print_char(char c) {
    init_video_once();

    if (graphics_mode) {
        if (c == '\n') {
            cursor_x = 0;
            cursor_y++;
        } else if (c == '\r') {
            cursor_x = 0;
        } else if (c == '\b') {
            if (cursor_x > 0) {
                cursor_x--;
                text_buffer[cursor_y][cursor_x] = ' ';
                render_cell(cursor_x, cursor_y, ' ');
                video_note_cell(cursor_x, cursor_y);
            }
        } else {
            if (cursor_x >= text_cols) {
                cursor_x = 0;
                cursor_y++;
            }
            if (cursor_y >= text_rows) {
                scroll_graphics();
            }
            if (c < 32 || c > 126) {
                c = '?';
            }
            text_buffer[cursor_y][cursor_x] = c;
            render_cell(cursor_x, cursor_y, c);
            video_note_cell(cursor_x, cursor_y);
            cursor_x++;
        }

        if (cursor_x >= text_cols) {
            cursor_x = 0;
            cursor_y++;
        }

        if (cursor_y >= text_rows) {
            scroll_graphics();
        }

        video_maybe_present_pending();
        return;
    }

    volatile u8* video = TEXT_VIDEO_MEMORY;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            int offset = (cursor_y * TEXT_SCREEN_WIDTH + cursor_x) * 2;
            video[offset] = ' ';
            video[offset + 1] = 0x07;
        }
    } else {
        int offset = (cursor_y * TEXT_SCREEN_WIDTH + cursor_x) * 2;
        video[offset] = (u8)c;
        video[offset + 1] = 0x07;
        cursor_x++;
    }

    if (cursor_x >= TEXT_SCREEN_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= TEXT_SCREEN_HEIGHT) {
        scroll_text();
    }

    update_cursor();
}

void print_string(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        print_char(str[i]);
    }
}

static void draw_char_custom(int col, int row, char c, u32 fg, u32 bg) {
    int x0 = text_origin_x + col * FONT_W;
    int y0 = text_origin_y + row * FONT_H;
    const u8* glyph = glyph_for_char(c);

    if (col < 0 || row < 0 || col >= text_cols || row >= text_rows) {
        return;
    }

    for (int y = 0; y < FONT_H; y++) {
        u8 bits = glyph[y];
        for (int x = 0; x < FONT_W; x++) {
            u32 color = (bits & (1u << (7 - x))) ? fg : bg;
            draw_pixel(x0 + x, y0 + y, color);
        }
    }
}

void draw_string_custom(int col, int row, const char* s, u32 fg, u32 bg) {
    int x = col;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\n') {
            row++;
            x = col;
            continue;
        }
        draw_char_custom(x, row, s[i], fg, bg);
        x++;
        if (x >= text_cols) {
            row++;
            x = col;
        }
        if (row >= text_rows) {
            break;
        }
    }
}

void video_draw_text_at(int x, int y, const char* text, unsigned int fg, unsigned int bg) {
    int px;
    int py;
    int start_x;
    int i;
    int len;

    init_video_once();
    if (!text) {
        return;
    }
    if (!graphics_mode) {
        print_string(text);
        return;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    start_x = x;
    px = x;
    py = y;
    len = 0;
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            py += FONT_H;
            px = start_x;
            continue;
        }
        if (px >= 0 && py >= 0 && px + FONT_W <= fb_width && py + FONT_H <= fb_height) {
            const u8* glyph = glyph_for_char(text[i]);
            int gy;
            int gx;
            for (gy = 0; gy < FONT_H; gy++) {
                u8 bits = glyph[gy];
                for (gx = 0; gx < FONT_W; gx++) {
                    u32 color = (bits & (1u << (7 - gx))) ? (u32)fg : (u32)bg;
                    draw_pixel(px + gx, py + gy, color);
                }
            }
        }
        px += FONT_W;
        len++;
    }
    {
        int w = len * FONT_W;
        int h = FONT_H;
        if (w <= 0) {
            w = FONT_W;
        }
        video_note_dirty(x, y, w, h);
    }
    video_maybe_present_pending();
}
