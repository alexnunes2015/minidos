#include "video_internal.h"
#include "logger.h"
#include "serial.h"
#include "scheduler.h"

#define VIDEO_BOOT_STATE_MAGIC 0x56494430u
#define VIDEO_LOCK_OWNER_NONE (-2)

typedef struct {
    u32 magic;
    u32 fb_addr;
    int fb_width;
    int fb_height;
    int fb_pitch;
    int fb_bpp;
    int fb_bytes_per_pixel;
    int text_cols;
    int text_rows;
    int text_origin_x;
    int text_origin_y;
    u8 red_size;
    u8 red_pos;
    u8 green_size;
    u8 green_pos;
    u8 blue_size;
    u8 blue_pos;
} video_boot_state_t;

static video_boot_state_t g_video_boot_state = {
    VIDEO_BOOT_STATE_MAGIC,
    0,
    0,
    0,
    0,
    0,
    0,
    TEXT_SCREEN_WIDTH,
    TEXT_SCREEN_HEIGHT,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0
};

int cursor_x = 0;
int cursor_y = 0;

int video_ready = 0;
int graphics_mode = 0;

volatile u8* fb = 0;
int fb_width = 0;
int fb_height = 0;
int fb_pitch = 0;
int fb_bpp = 0;
int fb_bytes_per_pixel = 0;
u8 red_size = 0;
u8 red_pos = 0;
u8 green_size = 0;
u8 green_pos = 0;
u8 blue_size = 0;
u8 blue_pos = 0;
u8* const video_backbuffer = VIDEO_BACKBUFFER_BASE;
int backbuffer_ready = 0;
int backbuffer_pitch = 0;
int present_deferred = 0;
int fast_present_mode = 0;  /* 0=slow, 24=fast24, 32=fast32 */
int dirty_valid = 0;
int dirty_x = 0;
int dirty_y = 0;
int dirty_w = 0;
int dirty_h = 0;
int dirty_rect_count = 0;
int dirty_overflow = 0;
video_dirty_rect_t dirty_rects[VIDEO_DIRTY_RECT_CAPACITY] = {0};

int text_cols = TEXT_SCREEN_WIDTH;
int text_rows = TEXT_SCREEN_HEIGHT;
int text_origin_x = 0;
int text_origin_y = 0;

u8* video_backbuffer_fill_base = 0;
int video_backbuffer_fill_pitch = 0;
int video_backbuffer_fill_h = 0;
int video_backbuffer_fill_w = 0;
u32 video_backbuffer_fill_rgb = 0;

u8* video_backbuffer_present_src = 0;
u8* video_backbuffer_present_dst = 0;
int video_backbuffer_present_src_pitch = 0;
int video_backbuffer_present_dst_pitch = 0;
int video_backbuffer_present_w = 0;
int video_backbuffer_present_h = 0;

/*
 * Keep the text cell backing store out of the flat kernel image.
 * The kernel now outgrows the 0xA0000 VGA aperture if this 40 KiB buffer
 * lives in .bss, which leaks runtime globals directly into the legacy
 * banked framebuffer window and shows up as colored artifacts on screen.
 */
char (*const text_buffer)[MAX_TEXT_COLS] = VIDEO_TEXT_BUFFER_BASE;

static volatile int g_video_lock_flag = 0;
static volatile int g_video_lock_owner = VIDEO_LOCK_OWNER_NONE;
static volatile int g_video_lock_depth = 0;

static int video_lock_owner_id(void) {
    int pid = scheduler_get_current_pid();
    return (pid < 0) ? -1 : pid;
}

void video_lock(void) {
    int owner = video_lock_owner_id();
    if (g_video_lock_owner == owner) {
        g_video_lock_depth++;
        return;
    }
    while (__sync_lock_test_and_set(&g_video_lock_flag, 1)) {
        __asm__ volatile ("pause");
    }
    g_video_lock_owner = owner;
    g_video_lock_depth = 1;
}

void video_unlock(void) {
    int owner = video_lock_owner_id();
    if (g_video_lock_owner != owner) {
        return;
    }
    g_video_lock_depth--;
    if (g_video_lock_depth == 0) {
        g_video_lock_owner = VIDEO_LOCK_OWNER_NONE;
        __sync_lock_release(&g_video_lock_flag);
    }
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

static inline u32 read_eflags(void) {
    u32 flags;
    __asm__ volatile ("pushf\npop %0" : "=r"(flags));
    return flags;
}

static inline void restore_interrupts(u32 flags) {
    if ((flags & 0x00000200U) != 0U) {
        __asm__ volatile ("sti");
    }
}

static int video_detect_fast_present_mode(void);

static int video_boot_state_valid(void) {
    return g_video_boot_state.magic == VIDEO_BOOT_STATE_MAGIC
        && g_video_boot_state.fb_addr != 0
        && g_video_boot_state.fb_width > 0
        && g_video_boot_state.fb_height > 0
        && g_video_boot_state.fb_pitch > 0
        && g_video_boot_state.fb_bpp >= 15;
}

static void video_restore_boot_state(void) {
    graphics_mode = 1;
    fb = (volatile u8*)(u32)g_video_boot_state.fb_addr;
    fb_width = g_video_boot_state.fb_width;
    fb_height = g_video_boot_state.fb_height;
    fb_pitch = g_video_boot_state.fb_pitch;
    fb_bpp = g_video_boot_state.fb_bpp;
    fb_bytes_per_pixel = g_video_boot_state.fb_bytes_per_pixel;
    text_cols = g_video_boot_state.text_cols;
    text_rows = g_video_boot_state.text_rows;
    text_origin_x = g_video_boot_state.text_origin_x;
    text_origin_y = g_video_boot_state.text_origin_y;
    red_size = g_video_boot_state.red_size;
    red_pos = g_video_boot_state.red_pos;
    green_size = g_video_boot_state.green_size;
    green_pos = g_video_boot_state.green_pos;
    blue_size = g_video_boot_state.blue_size;
    blue_pos = g_video_boot_state.blue_pos;
    backbuffer_ready = 0;
    backbuffer_pitch = 0;
    if (fb_width <= VIDEO_BACKBUFFER_MAX_WIDTH
        && fb_height <= VIDEO_BACKBUFFER_MAX_HEIGHT) {
        unsigned int required_pitch = (unsigned int)fb_width * (unsigned int)VIDEO_BACKBUFFER_BYTES_PER_PIXEL;
        unsigned int required_bytes = required_pitch * (unsigned int)fb_height;

        if (required_pitch > 0 && required_bytes <= VIDEO_BACKBUFFER_MAX_BYTES) {
            backbuffer_ready = 1;
            backbuffer_pitch = (int)required_pitch;
        }
    }
    fast_present_mode = video_detect_fast_present_mode();
    video_ready = 1;
}

static void video_save_boot_state(void) {
    g_video_boot_state.magic = VIDEO_BOOT_STATE_MAGIC;
    g_video_boot_state.fb_addr = (u32)fb;
    g_video_boot_state.fb_width = fb_width;
    g_video_boot_state.fb_height = fb_height;
    g_video_boot_state.fb_pitch = fb_pitch;
    g_video_boot_state.fb_bpp = fb_bpp;
    g_video_boot_state.fb_bytes_per_pixel = fb_bytes_per_pixel;
    g_video_boot_state.text_cols = text_cols;
    g_video_boot_state.text_rows = text_rows;
    g_video_boot_state.text_origin_x = text_origin_x;
    g_video_boot_state.text_origin_y = text_origin_y;
    g_video_boot_state.red_size = red_size;
    g_video_boot_state.red_pos = red_pos;
    g_video_boot_state.green_size = green_size;
    g_video_boot_state.green_pos = green_pos;
    g_video_boot_state.blue_size = blue_size;
    g_video_boot_state.blue_pos = blue_pos;
}

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static void video_update_dirty_union(int x, int y, int w, int h) {
    if (!dirty_valid) {
        dirty_valid = 1;
        dirty_x = x;
        dirty_y = y;
        dirty_w = w;
        dirty_h = h;
        return;
    }

    int right = x + w;
    int bottom = y + h;
    int current_right = dirty_x + dirty_w;
    int current_bottom = dirty_y + dirty_h;

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

static void video_record_dirty_rect(int x, int y, int w, int h) {
    if (dirty_overflow) {
        return;
    }

    if (dirty_rect_count < VIDEO_DIRTY_RECT_CAPACITY) {
        dirty_rects[dirty_rect_count].x = x;
        dirty_rects[dirty_rect_count].y = y;
        dirty_rects[dirty_rect_count].w = w;
        dirty_rects[dirty_rect_count].h = h;
        dirty_rect_count++;
        return;
    }

    dirty_overflow = 1;
    dirty_rect_count = 0;
}
static int video_detect_fast_present_mode(void) {
    if (!graphics_mode || !backbuffer_ready || fb == 0 || fb_pitch <= 0) {
        return 0;
    }
    if (red_size != 8 || red_pos != 16 || green_size != 8 || green_pos != 8 || blue_size != 8 || blue_pos != 0) {
        return 0;
    }
    if (fb_bytes_per_pixel == 4) {
        return 32;
    }
    if (fb_bytes_per_pixel == 3) {
        return 24;
    }
    return 0;
}

static u8 expand_6bit_to_8bit(u8 c) {
    return (u8)((c << 2) | (c >> 4));
}

void video_clear_dirty_locked(void) {
    dirty_valid = 0;
    dirty_x = 0;
    dirty_y = 0;
    dirty_w = 0;
    dirty_h = 0;
    dirty_rect_count = 0;
    dirty_overflow = 0;
}

void video_clear_dirty(void) {
    video_lock();
    video_clear_dirty_locked();
    video_unlock();
}

void video_disable_backbuffer_locked(void) {
    backbuffer_ready = 0;
    backbuffer_pitch = 0;
    fast_present_mode = 0;
    video_clear_dirty_locked();
}

void video_disable_backbuffer(void) {
    video_lock();
    video_disable_backbuffer_locked();
    video_unlock();
}

__attribute__((noinline, regparm(0)))
int video_backbuffer_rect_fits(int x, int y, int w, int h) {
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

void video_note_dirty_locked(int x, int y, int w, int h) {
    int x0;
    int y0;
    int x1;
    int y1;

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
    video_update_dirty_union(x, y, w, h);
    video_record_dirty_rect(x, y, w, h);
}

void video_note_dirty(int x, int y, int w, int h) {
    video_lock();
    video_note_dirty_locked(x, y, w, h);
    video_unlock();
}

void init_video_once(void) {
    u32 flags = read_eflags();

    if (video_ready || (graphics_mode && fb != 0 && fb_width > 0 && fb_height > 0 && fb_pitch > 0 && fb_bpp >= 15)) {
        video_ready = 1;
        fb_bytes_per_pixel = (fb_bpp <= 16) ? 2 : ((fb_bpp <= 24) ? 3 : 4);
        return;
    }
    if (video_boot_state_valid()) {
        video_restore_boot_state();
        return;
    }

    __asm__ volatile ("cli");
    if (video_ready) {
        restore_interrupts(flags);
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

            backbuffer_ready = 0;
            backbuffer_pitch = 0;
            if (fb_width <= VIDEO_BACKBUFFER_MAX_WIDTH
                && fb_height <= VIDEO_BACKBUFFER_MAX_HEIGHT) {
                unsigned int required_pitch = (unsigned int)fb_width * (unsigned int)VIDEO_BACKBUFFER_BYTES_PER_PIXEL;
                unsigned int required_bytes = required_pitch * (unsigned int)fb_height;

                if (required_pitch > 0 && required_bytes <= VIDEO_BACKBUFFER_MAX_BYTES) {
                    backbuffer_ready = 1;
                    backbuffer_pitch = (int)required_pitch;
                }
            }
            fast_present_mode = video_detect_fast_present_mode();
            video_save_boot_state();

            log_serial_raw("[vid] fb=");
            serial_print_hex((u32)fb);
            log_serial_raw("w=");
            serial_print_hex((u32)fb_width);
            log_serial_raw("h=");
            serial_print_hex((u32)fb_height);
            log_serial_raw("p=");
            serial_print_hex((u32)fb_pitch);
            log_serial_raw("b=");
            serial_print_hex((u32)fb_bpp);
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

    restore_interrupts(flags);
}

int video_is_graphics(void) {
    init_video_once();
    return graphics_mode;
}

int video_get_width(void) {
    init_video_once();
    if (graphics_mode) {
        return fb_width;
    }
    return TEXT_SCREEN_WIDTH * FONT_W;
}

int video_get_height(void) {
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
    if (backbuffer_ready) {
        /*
         * When a backbuffer is present, avoid touching the front buffer during
         * clears. Writing the front buffer immediately makes every frame start
         * with a visible flash (the screen briefly shows only the clear color)
         * before the rest of the UI is redrawn and presented. Keeping the
         * clear on the backbuffer and marking it dirty lets the caller control
         * exactly when the new frame becomes visible via video_present_pending.
         */
        clear_graphics(rgb);
        video_note_dirty(0, 0, fb_width, fb_height);
        video_maybe_present_pending();
    } else {
        /* Fallback for text mode or no backbuffer: update the front buffer
         * directly to keep the display in sync.
         */
        clear_graphics(rgb);
        fill_frontbuffer_rect_rgb(0, 0, fb_width, fb_height, rgb);
        video_clear_dirty();
    }
}

void video_fill_rect(int x, int y, int w, int h, unsigned int rgb) {
    init_video_once();
    if (!graphics_mode || w <= 0 || h <= 0) {
        return;
    }

    video_lock();
    fill_rect_rgb_locked(x, y, w, h, rgb);
    video_note_dirty_locked(x, y, w, h);
    video_unlock();
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

            int level = (grad_pos * 64) / fb_width;
            int threshold = bayer8x8[((y & 7) * 8) + (x & 7)];

            u32 c;
            if (threshold < level) {
                c = 0xF2F6FFu;
            } else {
                c = 0x1A5FD2u;
            }

            draw_pixel(x, bar_y + y, c);
        }
    }

    video_note_dirty(0, bar_y, fb_width, bar_h);
    video_maybe_present_pending();
}

#define VIDEO_STRESS_MAX_WORKERS 6

typedef struct {
    int id;
    int iterations;
    int screen_w;
    int screen_h;
    unsigned int color_base;
} video_stress_job_t;

static video_stress_job_t g_video_stress_jobs[VIDEO_STRESS_MAX_WORKERS];

static void video_stress_worker(void* raw_job) {
    video_stress_job_t* job = (video_stress_job_t*)raw_job;
    if (!job) {
        scheduler_exit_current_task();
    }

    int width = job->screen_w;
    int height = job->screen_h;
    if (width <= 0 || height <= 0 || job->iterations <= 0) {
        scheduler_exit_current_task();
    }

    for (int i = 0; i < job->iterations; i++) {
        int size = 16 + (i % 96);
        int max_x = width - size;
        if (max_x <= 0) {
            max_x = 1;
        }
        int max_y = height - size;
        if (max_y <= 0) {
            max_y = 1;
        }
        int x = (i * 31 + job->id * 17) % max_x;
        int y = (i * 29 + job->id * 13) % max_y;
        u32 color = (job->color_base + (i * 0x00030301u)) & 0x00FFFFFFu;

        video_fill_rect(x, y, size, size, color);
        scheduler_yield();
    }

    log_serial_raw("PTVIDEO200\n");
    scheduler_exit_current_task();
}

int video_start_stress_workers(int worker_count, int iterations) {
    init_video_once();
    if (!graphics_mode || worker_count <= 0 || iterations <= 0) {
        return 0;
    }

    if (worker_count > VIDEO_STRESS_MAX_WORKERS) {
        worker_count = VIDEO_STRESS_MAX_WORKERS;
    }

    int started = 0;
    for (int i = 0; i < worker_count; i++) {
        video_stress_job_t* job = &g_video_stress_jobs[i];
        job->id = i;
        job->iterations = iterations;
        job->screen_w = fb_width;
        job->screen_h = fb_height;
        job->color_base = 0x00102030u + ((unsigned int)i * 0x00010101u);

        int pid = scheduler_spawn_kernel_task("videostress", "videostress", 0, video_stress_worker, job, 0);
        if (pid < 0) {
            break;
        }
        started++;
    }

    return started;
}
