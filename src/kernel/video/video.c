#include "video.h"
#include "logger.h"
#include "serial.h"

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
#define VIDEO_BACKBUFFER_MAX_WIDTH 1024
#define VIDEO_BACKBUFFER_MAX_HEIGHT 768
#define VIDEO_BACKBUFFER_BYTES_PER_PIXEL 4
#define VIDEO_BACKBUFFER_MAX_BYTES (VIDEO_BACKBUFFER_MAX_WIDTH * VIDEO_BACKBUFFER_MAX_HEIGHT * 4)
#define VIDEO_BACKBUFFER_BASE ((u8*)0x00400000u)

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
static int fb_bytes_per_pixel = 0;
static u8 red_size = 0;
static u8 red_pos = 0;
static u8 green_size = 0;
static u8 green_pos = 0;
static u8 blue_size = 0;
static u8 blue_pos = 0;
static u8* const video_backbuffer = VIDEO_BACKBUFFER_BASE;
static int backbuffer_ready = 0;
static int backbuffer_pitch = 0;
static int present_deferred = 0;
static int dirty_valid = 0;
static int dirty_x = 0;
static int dirty_y = 0;
static int dirty_w = 0;
static int dirty_h = 0;

static int text_cols = TEXT_SCREEN_WIDTH;
static int text_rows = TEXT_SCREEN_HEIGHT;
static int text_origin_x = 0;
static int text_origin_y = 0;

static char text_buffer[MAX_TEXT_ROWS][MAX_TEXT_COLS];
static void init_video_once(void);
u8* video_backbuffer_fill_base = 0;
int video_backbuffer_fill_pitch = 0;
int video_backbuffer_fill_h = 0;
int video_backbuffer_fill_w = 0;
u32 video_backbuffer_fill_rgb = 0;
extern void __attribute__((regparm(0))) video_backbuffer_fill_rect32(void);

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

static void video_clear_dirty(void) {
    dirty_valid = 0;
    dirty_x = 0;
    dirty_y = 0;
    dirty_w = 0;
    dirty_h = 0;
}

static void video_disable_backbuffer(void) {
    backbuffer_ready = 0;
    backbuffer_pitch = 0;
    video_clear_dirty();
}

__attribute__((noinline, regparm(0)))
static int video_backbuffer_rect_fits(int x, int y, int w, int h) {
    unsigned int row_bytes;
    unsigned int row_offset;

    if (!backbuffer_ready || backbuffer_pitch <= 0) {
        return 0;
    }
    if (x < 0 || y < 0 || w <= 0 || h <= 0) {
        return 0;
    }

    row_bytes = (unsigned int)w * (unsigned int)VIDEO_BACKBUFFER_BYTES_PER_PIXEL;
    row_offset = ((unsigned int)(y + h - 1) * (unsigned int)backbuffer_pitch)
        + ((unsigned int)x * (unsigned int)VIDEO_BACKBUFFER_BYTES_PER_PIXEL);

    if (row_bytes == 0 || row_bytes > VIDEO_BACKBUFFER_MAX_BYTES) {
        return 0;
    }
    if (row_offset > VIDEO_BACKBUFFER_MAX_BYTES) {
        return 0;
    }
    return row_bytes <= (VIDEO_BACKBUFFER_MAX_BYTES - row_offset);
}

static void video_note_dirty(int x, int y, int w, int h) {
    int x0;
    int y0;
    int x1;
    int y1;
    int right;
    int bottom;
    int current_right;
    int current_bottom;

    if (!graphics_mode || !backbuffer_ready || w <= 0 || h <= 0) {
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

    if (!dirty_valid) {
        dirty_valid = 1;
        dirty_x = x;
        dirty_y = y;
        dirty_w = w;
        dirty_h = h;
        return;
    }

    right = x + w;
    bottom = y + h;
    current_right = dirty_x + dirty_w;
    current_bottom = dirty_y + dirty_h;

    if (x < dirty_x) {
        dirty_x = x;
    }
    if (y < dirty_y) {
        dirty_y = y;
    }
    if (right > current_right) {
        current_right = right;
    }
    if (bottom > current_bottom) {
        current_bottom = bottom;
    }

    dirty_w = current_right - dirty_x;
    dirty_h = current_bottom - dirty_y;
}

static void write_frontbuffer_pixel(volatile u8* dst, u32 rgb) {
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

static void video_copy_rect_to_front(int x, int y, int w, int h) {
    int x0;
    int y0;
    int x1;
    int y1;
    int py;

    if (!graphics_mode || !backbuffer_ready || w <= 0 || h <= 0) {
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
    if (!video_backbuffer_rect_fits(x, y, w, h)) {
        video_disable_backbuffer();
        return;
    }

    for (py = 0; py < h; py++) {
        volatile u8* dst = fb + ((y + py) * fb_pitch) + (x * fb_bytes_per_pixel);
        const u32* src = (const u32*)(const void*)(video_backbuffer + ((y + py) * backbuffer_pitch)
            + (x * VIDEO_BACKBUFFER_BYTES_PER_PIXEL));
        int px;

        for (px = 0; px < w; px++) {
            write_frontbuffer_pixel(dst + (px * fb_bytes_per_pixel), src[px]);
        }
    }
}

void video_present_pending(void) {
    init_video_once();
    if (!graphics_mode || !backbuffer_ready || !dirty_valid) {
        return;
    }

    video_copy_rect_to_front(dirty_x, dirty_y, dirty_w, dirty_h);
    video_clear_dirty();
}

static void video_maybe_present_pending(void) {
    if (!present_deferred) {
        video_present_pending();
    }
}

void video_set_deferred_present(int enabled) {
    init_video_once();
    if (!graphics_mode || !backbuffer_ready) {
        return;
    }

    if (!enabled) {
        video_present_pending();
    }
    present_deferred = enabled ? 1 : 0;
}

__attribute__((noinline, regparm(0)))
static void fill_rect_rgb(int x, int y, int w, int h, u32 rgb) {
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

static void draw_pixel(int x, int y, u32 rgb) {
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

static void clear_graphics(u32 rgb) {
    fill_rect_rgb(0, 0, fb_width, fb_height, rgb);
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
        case 'a': { static const u8 g[8] = {0x00,0x00,0x3C,0x02,0x3E,0x42,0x3E,0x00}; return g; }
        case 'b': { static const u8 g[8] = {0x40,0x40,0x5C,0x62,0x42,0x62,0x5C,0x00}; return g; }
        case 'c': { static const u8 g[8] = {0x00,0x00,0x3C,0x42,0x40,0x42,0x3C,0x00}; return g; }
        case 'd': { static const u8 g[8] = {0x02,0x02,0x3A,0x46,0x42,0x46,0x3A,0x00}; return g; }
        case 'e': { static const u8 g[8] = {0x00,0x00,0x3C,0x42,0x7E,0x40,0x3C,0x00}; return g; }
        case 'f': { static const u8 g[8] = {0x0C,0x10,0x10,0x3E,0x10,0x10,0x10,0x00}; return g; }
        case 'g': { static const u8 g[8] = {0x00,0x00,0x3A,0x46,0x46,0x3A,0x02,0x3C}; return g; }
        case 'h': { static const u8 g[8] = {0x40,0x40,0x5C,0x62,0x42,0x42,0x42,0x00}; return g; }
        case 'i': { static const u8 g[8] = {0x08,0x00,0x18,0x08,0x08,0x08,0x1C,0x00}; return g; }
        case 'j': { static const u8 g[8] = {0x04,0x00,0x0C,0x04,0x04,0x44,0x44,0x38}; return g; }
        case 'k': { static const u8 g[8] = {0x40,0x40,0x44,0x48,0x70,0x48,0x44,0x00}; return g; }
        case 'l': { static const u8 g[8] = {0x18,0x08,0x08,0x08,0x08,0x08,0x1C,0x00}; return g; }
        case 'm': { static const u8 g[8] = {0x00,0x00,0x6C,0x52,0x52,0x52,0x52,0x00}; return g; }
        case 'n': { static const u8 g[8] = {0x00,0x00,0x5C,0x62,0x42,0x42,0x42,0x00}; return g; }
        case 'o': { static const u8 g[8] = {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x00}; return g; }
        case 'p': { static const u8 g[8] = {0x00,0x00,0x5C,0x62,0x62,0x5C,0x40,0x40}; return g; }
        case 'q': { static const u8 g[8] = {0x00,0x00,0x3A,0x46,0x46,0x3A,0x02,0x02}; return g; }
        case 'r': { static const u8 g[8] = {0x00,0x00,0x5C,0x62,0x40,0x40,0x40,0x00}; return g; }
        case 's': { static const u8 g[8] = {0x00,0x00,0x3E,0x40,0x3C,0x02,0x7C,0x00}; return g; }
        case 't': { static const u8 g[8] = {0x10,0x10,0x3E,0x10,0x10,0x10,0x0E,0x00}; return g; }
        case 'u': { static const u8 g[8] = {0x00,0x00,0x42,0x42,0x42,0x46,0x3A,0x00}; return g; }
        case 'v': { static const u8 g[8] = {0x00,0x00,0x42,0x42,0x42,0x24,0x18,0x00}; return g; }
        case 'w': { static const u8 g[8] = {0x00,0x00,0x42,0x42,0x5A,0x5A,0x24,0x00}; return g; }
        case 'x': { static const u8 g[8] = {0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00}; return g; }
        case 'y': { static const u8 g[8] = {0x00,0x00,0x42,0x46,0x3A,0x02,0x04,0x38}; return g; }
        case 'z': { static const u8 g[8] = {0x00,0x00,0x7E,0x04,0x18,0x20,0x7E,0x00}; return g; }
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

static void video_note_cell(int col, int row) {
    video_note_dirty(text_origin_x + (col * FONT_W), text_origin_y + (row * FONT_H), FONT_W, FONT_H);
}

static void video_note_text_area(void) {
    video_note_dirty(text_origin_x, text_origin_y, text_cols * FONT_W, text_rows * FONT_H);
}

static void init_video_once(void) {
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
            fb_bytes_per_pixel = (fb_bpp <= 16) ? 2 : ((fb_bpp <= 24) ? 3 : 4);
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

            if (fb_width <= VIDEO_BACKBUFFER_MAX_WIDTH
                && fb_height <= VIDEO_BACKBUFFER_MAX_HEIGHT) {
                unsigned int required_pitch = (unsigned int)fb_width * (unsigned int)VIDEO_BACKBUFFER_BYTES_PER_PIXEL;
                unsigned int required_bytes = required_pitch * (unsigned int)fb_height;

                if (required_pitch > 0 && required_bytes <= VIDEO_BACKBUFFER_MAX_BYTES) {
                    backbuffer_ready = 1;
                    backbuffer_pitch = (int)required_pitch;
                }
            }

            log_serial_raw("[video] init fb=");
            serial_print_hex((u32)fb);
            log_serial_raw(" w=");
            serial_print_hex((u32)fb_width);
            log_serial_raw(" h=");
            serial_print_hex((u32)fb_height);
            log_serial_raw(" pitch=");
            serial_print_hex((u32)fb_pitch);
            log_serial_raw(" bpp=");
            serial_print_hex((u32)fb_bpp);
            log_serial_raw(" bb=");
            serial_print_hex((u32)backbuffer_ready);
            log_serial_raw(" bp=");
            serial_print_hex((u32)backbuffer_pitch);
            log_serial_raw("\n");
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
        video_note_dirty(0, 0, fb_width, fb_height);
        video_present_pending();
    }
}

int video_is_graphics() {
    init_video_once();
    return graphics_mode;
}

int video_get_width() {
    init_video_once();
    if (graphics_mode) {
        return fb_width;
    }
    return TEXT_SCREEN_WIDTH * FONT_W;
}

int video_get_height() {
    init_video_once();
    if (graphics_mode) {
        return fb_height;
    }
    return TEXT_SCREEN_HEIGHT * FONT_H;
}

void video_clear_color(unsigned int rgb) {
    init_video_once();
    if (!graphics_mode) {
        cls();
        return;
    }
    clear_graphics(rgb);
    video_note_dirty(0, 0, fb_width, fb_height);
    video_maybe_present_pending();
}

void video_fill_rect(int x, int y, int w, int h, unsigned int rgb) {
    init_video_once();
    if (!graphics_mode || w <= 0 || h <= 0) {
        return;
    }

    fill_rect_rgb(x, y, w, h, rgb);
    video_note_dirty(x, y, w, h);
    video_maybe_present_pending();
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

    video_note_dirty(0, 0, fb_width, fb_height);
    video_maybe_present_pending();
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

    video_note_dirty(0, bar_y, fb_width, bar_h);
    video_maybe_present_pending();
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
        video_note_dirty(0, 0, fb_width, fb_height);
        video_maybe_present_pending();
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
    video_note_text_area();
    video_maybe_present_pending();
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

static void measure_string_custom(int col, int row, const char* s, int* out_w, int* out_h) {
    int x = col;
    int start_col = col;
    int start_row = row;
    int max_x = col;
    int max_y = row;

    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }
    if (!s) {
        return;
    }

    for (int i = 0; s[i] != '\0' && row < text_rows; i++) {
        if (s[i] == '\n') {
            row++;
            x = start_col;
            continue;
        }
        if (x < text_cols) {
            if ((x + 1) > max_x) {
                max_x = x + 1;
            }
            if ((row + 1) > max_y) {
                max_y = row + 1;
            }
        }
        x++;
        if (x >= text_cols) {
            row++;
            x = start_col;
        }
    }

    if (out_w) {
        *out_w = (max_x > col) ? ((max_x - col) * FONT_W) : 0;
    }
    if (out_h) {
        *out_h = (max_y > start_row) ? ((max_y - start_row) * FONT_H) : FONT_H;
    }
}

void video_draw_text_at(int x, int y, const char* text, unsigned int fg, unsigned int bg) {
    init_video_once();
    if (!text) {
        return;
    }
    if (!graphics_mode) {
        print_string(text);
        return;
    }
    int col = x / FONT_W;
    int row = y / FONT_H;
    if (col < 0) {
        col = 0;
    }
    if (row < 0) {
        row = 0;
    }
    draw_string_custom(col, row, text, fg, bg);
    {
        int w = 0;
        int h = 0;
        measure_string_custom(col, row, text, &w, &h);
        if (w <= 0) {
            w = FONT_W;
        }
        if (h <= 0) {
            h = FONT_H;
        }
        video_note_dirty(text_origin_x + (col * FONT_W), text_origin_y + (row * FONT_H), w, h);
    }
    video_maybe_present_pending();
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
        video_note_dirty(0, 0, fb_width, fb_height);
        video_maybe_present_pending();
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
