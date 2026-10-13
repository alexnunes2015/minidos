#ifndef VIDEO_H
#define VIDEO_H

enum {
    VIDEO_SURFACE_FORMAT_LEGACY_XRGB8888 = 0,
    VIDEO_SURFACE_FORMAT_XRGB8888_LE = 1,
    VIDEO_SURFACE_FORMAT_ARGB8888_LE = 2,
};

enum {
    VIDEO_TEXT_TRANSPARENT_BG = 1,
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
    unsigned int rgb;
} video_rect_t;

typedef struct {
    const unsigned char* buffer;
    int width;
    int height;
    int stride;
    int format;
    int dest_x;
    int dest_y;
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
    int dest_w;
    int dest_h;
} video_surface_blit_t;

void cls();
void print_string(const char* str);
void print_char(char c);
void update_cursor();
void video_cursor_reset_blink(void);
void video_cursor_blink_step(void);
void video_cursor_suspend_graphics(void);
int video_is_graphics();
int video_get_width();
int video_get_height();
void video_clear_color(unsigned int rgb);
void video_fill_rect(int x, int y, int w, int h, unsigned int rgb);
int video_fill_rect_batch(const video_rect_t* rects, int count);
void video_draw_text_at(int x, int y, const char* text, unsigned int fg, unsigned int bg);
int video_draw_text_clipped(int x, int y, const char* text, unsigned int fg,
    unsigned int bg, int clip_x, int clip_y, int clip_w, int clip_h,
    unsigned int flags);
void video_present_pending(void);
void video_set_deferred_present(int enabled);
int video_start_stress_workers(int worker_count, int iterations);
int video_draw_indexed_image_centered(const unsigned char* pixels, int width, int height, const unsigned char* palette);
void video_draw_boot_gradient(unsigned int frame);
void video_show_bsod(const char* stop_code, const char* detail);
int video_blit_surface(const video_surface_blit_t* blit);
int video_blit_surface_desc(const void* blit_desc);

#endif
