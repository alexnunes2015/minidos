#include "video.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define TEXT_VIDEO_MEMORY ((volatile u8*)0xB8000)
#define TEXT_SCREEN_WIDTH 80
#define TEXT_SCREEN_HEIGHT 25

#define BOOT_VIDEO_FLAG_ADDR       0x0510
#define BOOT_VIDEO_WIDTH_ADDR      0x0512
#define BOOT_VIDEO_HEIGHT_ADDR     0x0514
#define BOOT_VIDEO_PITCH_ADDR      0x0516
#define BOOT_VIDEO_BPP_ADDR        0x0518
#define BOOT_VIDEO_RED_SIZE_ADDR   0x0519
#define BOOT_VIDEO_RED_POS_ADDR    0x051A
#define BOOT_VIDEO_GREEN_SIZE_ADDR 0x051B
#define BOOT_VIDEO_GREEN_POS_ADDR  0x051C
#define BOOT_VIDEO_BLUE_SIZE_ADDR  0x051D
#define BOOT_VIDEO_BLUE_POS_ADDR   0x051E
#define BOOT_VIDEO_FB_ADDR         0x0520

#define FONT_W 8
#define FONT_H 8
#define MAX_TEXT_COLS 256
#define MAX_TEXT_ROWS 160

#define COLOR_BG 0x000000u
#define COLOR_FG 0xD8DEE9u

static int cursor_x = 0;
static int cursor_y = 0;

static int video_ready = 0;
static int graphics_mode = 0;

static volatile u8* fb = 0;
static int fb_width = 0;
static int fb_height = 0;
static int fb_pitch = 0;
static int fb_bpp = 0;
static u8 red_size = 0;
static u8 red_pos = 0;
static u8 green_size = 0;
static u8 green_pos = 0;
static u8 blue_size = 0;
static u8 blue_pos = 0;

static int text_cols = TEXT_SCREEN_WIDTH;
static int text_rows = TEXT_SCREEN_HEIGHT;
static int text_origin_x = 0;
static int text_origin_y = 0;

static char text_buffer[MAX_TEXT_ROWS][MAX_TEXT_COLS];

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 mem8(u32 addr) {
    u8 val;
    __asm__ volatile ("movb (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}

static inline u16 mem16(u32 addr) {
    u16 val;
    __asm__ volatile ("movw (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}

static inline u32 mem32(u32 addr) {
    u32 val;
    __asm__ volatile ("movl (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static u8 expand_6bit_to_8bit(u8 c) {
    return (u8)((c << 2) | (c >> 4));
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

static void draw_pixel(int x, int y, u32 rgb) {
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) {
        return;
    }

    u8* p = (u8*)fb + (y * fb_pitch);
    u32 packed = pack_rgb((u8)((rgb >> 16) & 0xFF), (u8)((rgb >> 8) & 0xFF), (u8)(rgb & 0xFF));

    if (fb_bpp == 32) {
        *(volatile u32*)(p + x * 4) = packed;
    } else if (fb_bpp == 24) {
        u8* px = p + x * 3;
        px[0] = (u8)(packed & 0xFF);
        px[1] = (u8)((packed >> 8) & 0xFF);
        px[2] = (u8)((packed >> 16) & 0xFF);
    } else {
        *(volatile u16*)(p + x * 2) = (u16)packed;
    }
}

static void clear_graphics(u32 rgb) {
    for (int y = 0; y < fb_height; y++) {
        for (int x = 0; x < fb_width; x++) {
            draw_pixel(x, y, rgb);
        }
    }
}

static const u8 GLYPH_SPACE[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const u8 GLYPH_QMARK[8] = { 0x3C, 0x42, 0x04, 0x18, 0x10, 0x00, 0x10, 0x00 };

static const u8* glyph_for_char(char c) {
    switch (c) {
        case ' ': return GLYPH_SPACE;
        case '!': { static const u8 g[8] = {0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00}; return g; }
        case '"': { static const u8 g[8] = {0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00}; return g; }
        case '#': { static const u8 g[8] = {0x24,0x24,0x7E,0x24,0x7E,0x24,0x24,0x00}; return g; }
        case '$': { static const u8 g[8] = {0x10,0x3C,0x50,0x3C,0x12,0x7C,0x10,0x00}; return g; }
        case '%': { static const u8 g[8] = {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00}; return g; }
        case '&': { static const u8 g[8] = {0x30,0x48,0x50,0x20,0x55,0x48,0x35,0x00}; return g; }
        case '\'': { static const u8 g[8] = {0x18,0x18,0x10,0x20,0x00,0x00,0x00,0x00}; return g; }
        case '(': { static const u8 g[8] = {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0x00}; return g; }
        case ')': { static const u8 g[8] = {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0x00}; return g; }
        case '*': { static const u8 g[8] = {0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}; return g; }
        case '+': { static const u8 g[8] = {0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00}; return g; }
        case ',': { static const u8 g[8] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10}; return g; }
        case '-': { static const u8 g[8] = {0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00}; return g; }
        case '.': { static const u8 g[8] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}; return g; }
        case '/': { static const u8 g[8] = {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00}; return g; }
        case '0': { static const u8 g[8] = {0x3C,0x42,0x46,0x4A,0x52,0x62,0x3C,0x00}; return g; }
        case '1': { static const u8 g[8] = {0x18,0x28,0x08,0x08,0x08,0x08,0x3E,0x00}; return g; }
        case '2': { static const u8 g[8] = {0x3C,0x42,0x02,0x0C,0x10,0x20,0x7E,0x00}; return g; }
        case '3': { static const u8 g[8] = {0x3C,0x42,0x02,0x1C,0x02,0x42,0x3C,0x00}; return g; }
        case '4': { static const u8 g[8] = {0x04,0x0C,0x14,0x24,0x44,0x7E,0x04,0x00}; return g; }
        case '5': { static const u8 g[8] = {0x7E,0x40,0x7C,0x02,0x02,0x42,0x3C,0x00}; return g; }
        case '6': { static const u8 g[8] = {0x1C,0x20,0x40,0x7C,0x42,0x42,0x3C,0x00}; return g; }
        case '7': { static const u8 g[8] = {0x7E,0x42,0x04,0x08,0x10,0x10,0x10,0x00}; return g; }
        case '8': { static const u8 g[8] = {0x3C,0x42,0x42,0x3C,0x42,0x42,0x3C,0x00}; return g; }
        case '9': { static const u8 g[8] = {0x3C,0x42,0x42,0x3E,0x02,0x04,0x38,0x00}; return g; }
        case ':': { static const u8 g[8] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}; return g; }
        case ';': { static const u8 g[8] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x10}; return g; }
        case '<': { static const u8 g[8] = {0x06,0x18,0x60,0x60,0x18,0x06,0x00,0x00}; return g; }
        case '=': { static const u8 g[8] = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}; return g; }
        case '>': { static const u8 g[8] = {0x60,0x18,0x06,0x06,0x18,0x60,0x00,0x00}; return g; }
        case '?': return GLYPH_QMARK;
        case '@': { static const u8 g[8] = {0x3C,0x42,0x5A,0x5A,0x5C,0x40,0x3C,0x00}; return g; }
        case 'A': { static const u8 g[8] = {0x18,0x24,0x42,0x7E,0x42,0x42,0x42,0x00}; return g; }
        case 'B': { static const u8 g[8] = {0x7C,0x42,0x42,0x7C,0x42,0x42,0x7C,0x00}; return g; }
        case 'C': { static const u8 g[8] = {0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00}; return g; }
        case 'D': { static const u8 g[8] = {0x78,0x44,0x42,0x42,0x42,0x44,0x78,0x00}; return g; }
        case 'E': { static const u8 g[8] = {0x7E,0x40,0x40,0x78,0x40,0x40,0x7E,0x00}; return g; }
        case 'F': { static const u8 g[8] = {0x7E,0x40,0x40,0x78,0x40,0x40,0x40,0x00}; return g; }
        case 'G': { static const u8 g[8] = {0x3C,0x42,0x40,0x4E,0x42,0x42,0x3C,0x00}; return g; }
        case 'H': { static const u8 g[8] = {0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x00}; return g; }
        case 'I': { static const u8 g[8] = {0x3E,0x08,0x08,0x08,0x08,0x08,0x3E,0x00}; return g; }
        case 'J': { static const u8 g[8] = {0x1E,0x04,0x04,0x04,0x44,0x44,0x38,0x00}; return g; }
        case 'K': { static const u8 g[8] = {0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00}; return g; }
        case 'L': { static const u8 g[8] = {0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00}; return g; }
        case 'M': { static const u8 g[8] = {0x42,0x66,0x5A,0x5A,0x42,0x42,0x42,0x00}; return g; }
        case 'N': { static const u8 g[8] = {0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x00}; return g; }
        case 'O': { static const u8 g[8] = {0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00}; return g; }
        case 'P': { static const u8 g[8] = {0x7C,0x42,0x42,0x7C,0x40,0x40,0x40,0x00}; return g; }
        case 'Q': { static const u8 g[8] = {0x3C,0x42,0x42,0x42,0x4A,0x44,0x3A,0x00}; return g; }
        case 'R': { static const u8 g[8] = {0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x00}; return g; }
        case 'S': { static const u8 g[8] = {0x3C,0x42,0x40,0x3C,0x02,0x42,0x3C,0x00}; return g; }
        case 'T': { static const u8 g[8] = {0x7F,0x08,0x08,0x08,0x08,0x08,0x08,0x00}; return g; }
        case 'U': { static const u8 g[8] = {0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00}; return g; }
        case 'V': { static const u8 g[8] = {0x42,0x42,0x42,0x42,0x24,0x24,0x18,0x00}; return g; }
        case 'W': { static const u8 g[8] = {0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00}; return g; }
        case 'X': { static const u8 g[8] = {0x42,0x24,0x18,0x18,0x18,0x24,0x42,0x00}; return g; }
        case 'Y': { static const u8 g[8] = {0x42,0x24,0x18,0x10,0x10,0x10,0x10,0x00}; return g; }
        case 'Z': { static const u8 g[8] = {0x7E,0x04,0x08,0x10,0x20,0x40,0x7E,0x00}; return g; }
        case '[': { static const u8 g[8] = {0x3C,0x20,0x20,0x20,0x20,0x20,0x3C,0x00}; return g; }
        case '\\': { static const u8 g[8] = {0x40,0x20,0x10,0x08,0x04,0x02,0x00,0x00}; return g; }
        case ']': { static const u8 g[8] = {0x3C,0x04,0x04,0x04,0x04,0x04,0x3C,0x00}; return g; }
        case '^': { static const u8 g[8] = {0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00}; return g; }
        case '_': { static const u8 g[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00}; return g; }
        case '`': { static const u8 g[8] = {0x10,0x08,0x04,0x00,0x00,0x00,0x00,0x00}; return g; }
        case '|': { static const u8 g[8] = {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00}; return g; }
        case '~': { static const u8 g[8] = {0x00,0x00,0x32,0x4C,0x00,0x00,0x00,0x00}; return g; }
        default: return GLYPH_QMARK;
    }
}

static void render_cell(int col, int row, char c) {
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

static void redraw_text_buffer() {
    for (int y = 0; y < text_rows; y++) {
        for (int x = 0; x < text_cols; x++) {
            render_cell(x, y, text_buffer[y][x]);
        }
    }
}

static void init_video_once() {
    if (video_ready) {
        return;
    }

    u8 mode_flag = mem8(BOOT_VIDEO_FLAG_ADDR);
    if (mode_flag == 1) {
        u32 fb_addr = mem32(BOOT_VIDEO_FB_ADDR);
        u16 width = mem16(BOOT_VIDEO_WIDTH_ADDR);
        u16 height = mem16(BOOT_VIDEO_HEIGHT_ADDR);
        u16 pitch = mem16(BOOT_VIDEO_PITCH_ADDR);
        u8 bpp = mem8(BOOT_VIDEO_BPP_ADDR);

        if (fb_addr != 0 && width >= 320 && height >= 200 && pitch >= width && bpp >= 15) {
            graphics_mode = 1;
            fb = (volatile u8*)fb_addr;
            fb_width = width;
            fb_height = height;
            fb_pitch = pitch;
            fb_bpp = bpp;
            red_size = mem8(BOOT_VIDEO_RED_SIZE_ADDR);
            red_pos = mem8(BOOT_VIDEO_RED_POS_ADDR);
            green_size = mem8(BOOT_VIDEO_GREEN_SIZE_ADDR);
            green_pos = mem8(BOOT_VIDEO_GREEN_POS_ADDR);
            blue_size = mem8(BOOT_VIDEO_BLUE_SIZE_ADDR);
            blue_pos = mem8(BOOT_VIDEO_BLUE_POS_ADDR);

            if (red_size == 0 || green_size == 0 || blue_size == 0) {
                if (fb_bpp == 16) {
                    red_size = 5; red_pos = 11;
                    green_size = 6; green_pos = 5;
                    blue_size = 5; blue_pos = 0;
                } else {
                    red_size = 8; red_pos = 16;
                    green_size = 8; green_pos = 8;
                    blue_size = 8; blue_pos = 0;
                }
            }

            text_cols = min_int(MAX_TEXT_COLS, fb_width / FONT_W);
            text_rows = min_int(MAX_TEXT_ROWS, fb_height / FONT_H);
            if (text_cols < 1) text_cols = 1;
            if (text_rows < 1) text_rows = 1;

            text_origin_x = 0;
            text_origin_y = 0;
        }
    }

    for (int y = 0; y < MAX_TEXT_ROWS; y++) {
        for (int x = 0; x < MAX_TEXT_COLS; x++) {
            text_buffer[y][x] = ' ';
        }
    }

    cursor_x = 0;
    cursor_y = 0;

    video_ready = 1;

    if (graphics_mode) {
        clear_graphics(COLOR_BG);
        redraw_text_buffer();
    }
}

int video_is_graphics() {
    init_video_once();
    return graphics_mode;
}

int video_draw_indexed_image_centered(const unsigned char* pixels, int width, int height, const unsigned char* palette) {
    init_video_once();

    if (!graphics_mode || !pixels || !palette || width <= 0 || height <= 0) {
        return 0;
    }

    for (int y = 0; y < fb_height; y++) {
        int src_y = (y * height) / fb_height;
        int src_row = src_y * width;
        for (int x = 0; x < fb_width; x++) {
            int src_x = (x * width) / fb_width;
            u8 idx = pixels[src_row + src_x];
            int p = idx * 3;
            u8 r = expand_6bit_to_8bit(palette[p + 0]);
            u8 g = expand_6bit_to_8bit(palette[p + 1]);
            u8 b = expand_6bit_to_8bit(palette[p + 2]);
            u32 rgb = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
            draw_pixel(x, y, rgb);
        }
    }

    return 1;
}

void video_draw_boot_gradient(unsigned int frame) {
    init_video_once();

    if (!graphics_mode) {
        return;
    }

    static const u8 bayer8x8[64] = {
        0, 48, 12, 60, 3, 51, 15, 63,
        32, 16, 44, 28, 35, 19, 47, 31,
        8, 56, 4, 52, 11, 59, 7, 55,
        40, 24, 36, 20, 43, 27, 39, 23,
        2, 50, 14, 62, 1, 49, 13, 61,
        34, 18, 46, 30, 33, 17, 45, 29,
        10, 58, 6, 54, 9, 57, 5, 53,
        42, 26, 38, 22, 41, 25, 37, 21
    };

    int bar_h = fb_height / 40;
    if (bar_h < 5) {
        bar_h = 5;
    }
    int bar_y = fb_height - bar_h;
    if (bar_y < 0) {
        bar_y = 0;
    }

    for (int y = 0; y < bar_h; y++) {
        for (int x = 0; x < fb_width; x++) {
            int shift = (int)(frame % (unsigned int)fb_width);
            int grad_pos = x + shift;
            if (grad_pos >= fb_width) {
                grad_pos -= fb_width;
            }

            int level = (grad_pos * 64) / fb_width; // 0..63
            int threshold = bayer8x8[((y & 7) * 8) + (x & 7)];

            u32 c;
            if (threshold < level) {
                c = 0xF2F6FFu; // white
            } else {
                c = 0x1A5FD2u; // blue
            }

            draw_pixel(x, bar_y + y, c);
        }
    }
}

void update_cursor() {
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

void cls() {
    init_video_once();

    for (int y = 0; y < text_rows; y++) {
        for (int x = 0; x < text_cols; x++) {
            text_buffer[y][x] = ' ';
        }
    }

    if (graphics_mode) {
        clear_graphics(COLOR_BG);
        redraw_text_buffer();
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

static void scroll_graphics() {
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
}

static void scroll_text() {
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
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }

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
            }
        } else {
            if (c < 32 || c > 126) {
                c = '?';
            }
            text_buffer[cursor_y][cursor_x] = c;
            render_cell(cursor_x, cursor_y, c);
            cursor_x++;
        }

        if (cursor_x >= text_cols) {
            cursor_x = 0;
            cursor_y++;
        }

        if (cursor_y >= text_rows) {
            scroll_graphics();
        }

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

static void draw_string_custom(int col, int row, const char* s, u32 fg, u32 bg) {
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

static int str_len_local(const char* s) {
    int n = 0;
    while (s && s[n] != '\0') {
        n++;
    }
    return n;
}

static void draw_string_centered_custom(int row, const char* s, u32 fg, u32 bg) {
    int len = str_len_local(s);
    int col = (text_cols - len) / 2;
    if (col < 0) {
        col = 0;
    }
    draw_string_custom(col, row, s, fg, bg);
}

void video_show_bsod(const char* stop_code, const char* detail) {
    const char* stop = stop_code ? stop_code : "0E : 016F : BFF93BD4";
    const char* extra = detail ? detail : "A TEST FATAL EXCEPTION HAS OCCURRED.";

    init_video_once();

    if (graphics_mode) {
        const u32 bg = 0x0000AAu;
        const u32 fg = 0xFFFFFFu;

        clear_graphics(bg);
        draw_string_centered_custom(3, " MINI DOS ", bg, 0xB8B8B8u);
        draw_string_custom(4, 6, "AN ERROR HAS OCCURRED. TO CONTINUE:", fg, bg);
        draw_string_custom(4, 8, "PRESS ENTER TO RETURN TO SHELL, OR", fg, bg);
        draw_string_custom(4, 10, "PRESS CTRL+ALT+DEL TO RESTART YOUR COMPUTER. IF YOU DO THIS,", fg, bg);
        draw_string_custom(4, 11, "YOU WILL LOSE ANY UNSAVED INFORMATION IN ALL OPEN APPLICATIONS.", fg, bg);
        draw_string_custom(4, 13, "ERROR: ", fg, bg);
        draw_string_custom(11, 13, stop, fg, bg);
        draw_string_custom(4, 15, extra, fg, bg);
        draw_string_centered_custom(18, "PRESS ANY KEY TO CONTINUE _", fg, bg);
        return;
    }

    {
        volatile u8* video = TEXT_VIDEO_MEMORY;
        const u8 attr = 0x1F;
        int pos = 0;

        for (int y = 0; y < TEXT_SCREEN_HEIGHT; y++) {
            for (int x = 0; x < TEXT_SCREEN_WIDTH; x++) {
                video[pos++] = ' ';
                video[pos++] = attr;
            }
        }

        const char* lines[] = {
            "                                  MINI DOS                                   ",
            "",
            " AN ERROR HAS OCCURRED. TO CONTINUE:",
            "",
            " PRESS ENTER TO RETURN TO SHELL, OR",
            "",
            " PRESS CTRL+ALT+DEL TO RESTART YOUR COMPUTER. IF YOU DO THIS,",
            " YOU WILL LOSE ANY UNSAVED INFORMATION IN ALL OPEN APPLICATIONS.",
            "",
            " ERROR: ",
            "",
            "                          PRESS ANY KEY TO CONTINUE _",
            0
        };

        int row = 1;
        for (int li = 0; lines[li] != 0 && row < TEXT_SCREEN_HEIGHT; li++, row++) {
            const char* s = lines[li];
            for (int x = 0; s[x] != '\0' && x < TEXT_SCREEN_WIDTH; x++) {
                int off = (row * TEXT_SCREEN_WIDTH + x) * 2;
                video[off] = (u8)s[x];
                video[off + 1] = attr;
            }
        }

        row = 9;
        for (int x = 0; stop[x] != '\0' && (8 + x) < TEXT_SCREEN_WIDTH; x++) {
            int off = (row * TEXT_SCREEN_WIDTH + 8 + x) * 2;
            video[off] = (u8)stop[x];
            video[off + 1] = attr;
        }

        row = 11;
        for (int x = 0; extra[x] != '\0' && (1 + x) < TEXT_SCREEN_WIDTH; x++) {
            int off = (row * TEXT_SCREEN_WIDTH + 1 + x) * 2;
            video[off] = (u8)extra[x];
            video[off + 1] = attr;
        }
    }
}
