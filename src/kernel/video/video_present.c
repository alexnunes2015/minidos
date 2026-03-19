#include "video_internal.h"

static void video_wait_vretrace(void) {
    int i;
    for (i = 0; i < 8192; i++) {
        if (!(inb(0x3DA) & 0x08)) {
            break;
        }
    }
    for (i = 0; i < 8192; i++) {
        if (inb(0x3DA) & 0x08) {
            break;
        }
    }
}

static void video_copy_rect_to_front(int x, int y, int w, int h) {
    int fb_bytes = video_fb_bytes_per_pixel();
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

    if (fast_present_mode) {
        video_backbuffer_present_src = video_backbuffer
            + (y * backbuffer_pitch)
            + (x * VIDEO_BACKBUFFER_BYTES_PER_PIXEL);
        video_backbuffer_present_dst = (u8*)fb
            + (y * fb_pitch)
            + (x * fb_bytes);
        video_backbuffer_present_src_pitch = backbuffer_pitch;
        video_backbuffer_present_dst_pitch = fb_pitch;
        video_backbuffer_present_w = w;
        video_backbuffer_present_h = h;
        if (fast_present_mode == 32) {
            video_backbuffer_present_rect32();
        } else {
            video_backbuffer_present_rect24();
        }
        return;
    }

    for (py = 0; py < h; py++) {
        volatile u8* dst = fb + ((y + py) * fb_pitch) + (x * fb_bytes);
        const u32* src = (const u32*)(const void*)(video_backbuffer + ((y + py) * backbuffer_pitch)
            + (x * VIDEO_BACKBUFFER_BYTES_PER_PIXEL));
        int px;

        for (px = 0; px < w; px++) {
            write_frontbuffer_pixel(dst + (px * fb_bytes), src[px]);
        }
    }
}

void video_present_pending(void) {
    init_video_once();
    if (!graphics_mode || !backbuffer_ready || !dirty_valid) {
        return;
    }

    if (dirty_w > (fb_width >> 2) || dirty_h > (fb_height >> 2)) {
        video_wait_vretrace();
    }
    video_copy_rect_to_front(dirty_x, dirty_y, dirty_w, dirty_h);
    video_clear_dirty();
}

void video_maybe_present_pending(void) {
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
