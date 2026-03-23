#ifndef VIDEO_INTERNAL_H
#define VIDEO_INTERNAL_H

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
#define VIDEO_TEXT_BUFFER_BASE ((char (*)[MAX_TEXT_COLS])0x00380000u)
#define VIDEO_BACKBUFFER_MAX_WIDTH 1280
#define VIDEO_BACKBUFFER_MAX_HEIGHT 1024
#define VIDEO_BACKBUFFER_BYTES_PER_PIXEL 4
#define VIDEO_BACKBUFFER_MAX_BYTES (VIDEO_BACKBUFFER_MAX_WIDTH * VIDEO_BACKBUFFER_MAX_HEIGHT * 4)
#define VIDEO_BACKBUFFER_BASE ((u8*)0x00B00000u)

#define COLOR_BG 0x000000u
#define COLOR_FG 0xD8DEE9u

extern int cursor_x;
extern int cursor_y;

extern int video_ready;
extern int graphics_mode;

extern volatile u8* fb;
extern int fb_width;
extern int fb_height;
extern int fb_pitch;
extern int fb_bpp;
extern int fb_bytes_per_pixel;
extern u8 red_size;
extern u8 red_pos;
extern u8 green_size;
extern u8 green_pos;
extern u8 blue_size;
extern u8 blue_pos;
extern u8* const video_backbuffer;
extern int backbuffer_ready;
extern int backbuffer_pitch;
extern int present_deferred;
extern int fast_present_mode;
extern int dirty_valid;
extern int dirty_x;
extern int dirty_y;
extern int dirty_w;
extern int dirty_h;

#define VIDEO_DIRTY_RECT_CAPACITY 512
typedef struct {
    int x;
    int y;
    int w;
    int h;
} video_dirty_rect_t;

extern int dirty_rect_count;
extern int dirty_overflow;
extern video_dirty_rect_t dirty_rects[VIDEO_DIRTY_RECT_CAPACITY];

extern int text_cols;
extern int text_rows;
extern int text_origin_x;
extern int text_origin_y;

extern char (*const text_buffer)[MAX_TEXT_COLS];

extern u8* video_backbuffer_fill_base;
extern int video_backbuffer_fill_pitch;
extern int video_backbuffer_fill_h;
extern int video_backbuffer_fill_w;
extern u32 video_backbuffer_fill_rgb;
extern void __attribute__((regparm(0))) video_backbuffer_fill_rect32(void);

extern u8* video_backbuffer_present_src;
extern u8* video_backbuffer_present_dst;
extern int video_backbuffer_present_src_pitch;
extern int video_backbuffer_present_dst_pitch;
extern int video_backbuffer_present_w;
extern int video_backbuffer_present_h;
extern void __attribute__((regparm(0))) video_backbuffer_present_rect32(void);
extern void __attribute__((regparm(0))) video_backbuffer_present_rect24(void);

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline int video_fb_bytes_per_pixel(void) {
    if (fb_bpp <= 16) {
        return 2;
    }
    if (fb_bpp <= 24) {
        return 3;
    }
    return 4;
}

void init_video_once(void);

void video_clear_dirty(void);
void video_disable_backbuffer(void);
int __attribute__((noinline, regparm(0))) video_backbuffer_rect_fits(int x, int y, int w, int h);
void video_note_dirty(int x, int y, int w, int h);

void write_frontbuffer_pixel(volatile u8* dst, u32 rgb);
void fill_frontbuffer_rect_rgb(int x, int y, int w, int h, u32 rgb);
void fill_rect_rgb(int x, int y, int w, int h, u32 rgb);
void draw_pixel(int x, int y, u32 rgb);
void clear_graphics(u32 rgb);

const u8* glyph_for_char(char c);

void render_cell(int col, int row, char c);
void redraw_text_buffer(void);
void video_note_cell(int col, int row);
void video_note_text_area(void);

void video_maybe_present_pending(void);

void draw_string_custom(int col, int row, const char* s, u32 fg, u32 bg);

#endif
