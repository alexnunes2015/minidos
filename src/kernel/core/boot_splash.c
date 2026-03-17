#include "boot_splash.h"

#include "drive.h"
#include "fat16.h"
#include "logger.h"
#include "serial.h"
#include "timer.h"
#include "video.h"

#define BOOT_SPLASH_BG_RGB       0x000000u
#define BOOT_SPLASH_CURSOR_RGB   0xF2F6FFu
#define BOOT_SPLASH_TICK_STEP    3u
#define BOOT_SPLASH_CYCLE_MS     5000u
#define BOOT_SPLASH_CURSOR_MS    500u
#define BOOT_SPLASH_CURSOR_X     24
#define BOOT_SPLASH_CURSOR_Y     24
#define BOOT_SPLASH_CURSOR_W     12
#define BOOT_SPLASH_CURSOR_H     18

typedef struct {
    int active;
    int graphics;
    int logo_loaded;
    int cursor_visible;
    unsigned int start_tick;
    unsigned int logo_tick;
    unsigned int last_tick;
    unsigned int last_cursor_tick;
    unsigned int frame;
} boot_splash_state_t;

static boot_splash_state_t g_boot_splash;
static unsigned char g_logo_pixels[320 * 200];
static unsigned char g_logo_palette[256 * 3];

static unsigned int boot_splash_current_frame(unsigned int ticks) {
    unsigned int hz;
    unsigned int width;
    unsigned int ticks_per_cycle;
    unsigned int elapsed_ticks;

    hz = timer_get_hz();
    width = (unsigned int)video_get_width();
    if (hz == 0 || width == 0) {
        return 0;
    }

    ticks_per_cycle = (hz * BOOT_SPLASH_CYCLE_MS + 999u) / 1000u;
    if (ticks_per_cycle == 0) {
        ticks_per_cycle = 1;
    }

    elapsed_ticks = ticks - g_boot_splash.logo_tick;
    elapsed_ticks %= ticks_per_cycle;
    return (elapsed_ticks * width) / ticks_per_cycle;
}

static unsigned int boot_splash_cycle_ticks(void) {
    unsigned int ticks = timer_ms_to_ticks_ceil(BOOT_SPLASH_CYCLE_MS);
    return ticks == 0 ? 1 : ticks;
}

static unsigned int boot_splash_cursor_ticks(void) {
    unsigned int ticks = timer_ms_to_ticks_ceil(BOOT_SPLASH_CURSOR_MS);
    return ticks == 0 ? 1 : ticks;
}

static void boot_splash_draw_cursor(int visible) {
    if (!g_boot_splash.graphics || g_boot_splash.logo_loaded) {
        return;
    }

    video_fill_rect(
        BOOT_SPLASH_CURSOR_X,
        BOOT_SPLASH_CURSOR_Y,
        BOOT_SPLASH_CURSOR_W,
        BOOT_SPLASH_CURSOR_H,
        visible ? BOOT_SPLASH_CURSOR_RGB : BOOT_SPLASH_BG_RGB
    );
    g_boot_splash.cursor_visible = visible;
}

static void boot_splash_draw_base(void) {
    if (!g_boot_splash.graphics) {
        return;
    }

    video_clear_color(BOOT_SPLASH_BG_RGB);
    boot_splash_draw_cursor(1);
}

void boot_splash_begin(void) {
    unsigned int tick = 0;

    if (timer_is_ready()) {
        tick = timer_get_ticks();
    }

    g_boot_splash.active = 1;
    g_boot_splash.graphics = video_is_graphics();
    g_boot_splash.logo_loaded = 0;
    g_boot_splash.cursor_visible = 0;
    g_boot_splash.start_tick = tick;
    g_boot_splash.logo_tick = tick;
    g_boot_splash.last_tick = tick;
    g_boot_splash.last_cursor_tick = tick;
    g_boot_splash.frame = 0;

    log_serial_raw("BOOT100\n");

    if (!g_boot_splash.graphics) {
        return;
    }

    boot_splash_draw_base();
}

void boot_splash_try_load_logo(void) {
    int logo_bytes;
    int pal_bytes;

    if (!g_boot_splash.active || !g_boot_splash.graphics || g_boot_splash.logo_loaded) {
        return;
    }
    if (drive_get_count() <= 0) {
        return;
    }

    fat16_set_drive(drive_get_current());
    if (!fat16_init()) {
        return;
    }

    logo_bytes = fat16_read_file("BOOTLOGO.DAT", g_logo_pixels, sizeof(g_logo_pixels));
    pal_bytes = fat16_read_file("BOOTLOGO.PAL", g_logo_palette, sizeof(g_logo_palette));
    if (logo_bytes != (int)sizeof(g_logo_pixels) || pal_bytes != (int)sizeof(g_logo_palette)) {
        return;
    }

    video_draw_indexed_image_centered(g_logo_pixels, 320, 200, g_logo_palette);
    g_boot_splash.logo_loaded = 1;
    g_boot_splash.logo_tick = timer_is_ready() ? timer_get_ticks() : g_boot_splash.start_tick;
    if (timer_is_ready()) {
        g_boot_splash.frame = boot_splash_current_frame(g_boot_splash.logo_tick);
        video_draw_boot_gradient(g_boot_splash.frame);
    } else {
        g_boot_splash.frame = 0;
    }
    log_serial_raw("BOOT110\n");
}

void boot_splash_pump(void) {
    unsigned int ticks;
    unsigned int delta;
    unsigned int cursor_delta;

    if (!g_boot_splash.active || !g_boot_splash.graphics || !timer_is_ready()) {
        return;
    }

    ticks = timer_get_ticks();
    if (!g_boot_splash.logo_loaded) {
        cursor_delta = ticks - g_boot_splash.last_cursor_tick;
        if (cursor_delta >= boot_splash_cursor_ticks()) {
            g_boot_splash.last_cursor_tick = ticks;
            boot_splash_draw_cursor(!g_boot_splash.cursor_visible);
        }
        return;
    }

    delta = ticks - g_boot_splash.last_tick;
    if (delta < BOOT_SPLASH_TICK_STEP) {
        return;
    }

    g_boot_splash.last_tick = ticks;
    g_boot_splash.frame = boot_splash_current_frame(ticks);
    video_draw_boot_gradient(g_boot_splash.frame);
}

void boot_splash_finish(void) {
    if (!g_boot_splash.active) {
        return;
    }

    if (g_boot_splash.graphics && g_boot_splash.logo_loaded && timer_is_ready()) {
        unsigned int min_ticks = boot_splash_cycle_ticks();
        while ((unsigned int)(timer_get_ticks() - g_boot_splash.logo_tick) < min_ticks) {
            boot_splash_pump();
            timer_wait_for_interrupt();
        }
        boot_splash_pump();
    }

    log_serial_raw("BOOT190\n");
    g_boot_splash.active = 0;

    if (g_boot_splash.graphics) {
        cls();
    }
}

int boot_splash_is_active(void) {
    return g_boot_splash.active;
}
