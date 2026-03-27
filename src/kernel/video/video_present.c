#include "video_internal.h"

static unsigned long long video_dirty_rects_area(void) {
    unsigned long long area = 0ULL;

    for (int i = 0; i < dirty_rect_count; i++) {
        int w = dirty_rects[i].w;
        int h = dirty_rects[i].h;
        if (w <= 0 || h <= 0) {
            continue;
        }
        area += (unsigned long long)w * (unsigned long long)h;
    }

    return area;
}

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
        video_disable_backbuffer_locked();
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
    video_lock();
    if (!graphics_mode || !backbuffer_ready || !dirty_valid) {
        video_unlock();
        return;
    }

    {
        unsigned long long total_pixels = (unsigned long long)fb_width * (unsigned long long)fb_height;
        unsigned long long dirty_area = dirty_overflow
            ? (unsigned long long)dirty_w * (unsigned long long)dirty_h
            : video_dirty_rects_area();
        unsigned long long vsync_threshold = total_pixels >> 2;
        if (vsync_threshold == 0) {
            vsync_threshold = 1;
        }
        if (dirty_area >= vsync_threshold) {
            video_wait_vretrace();
        }
    }
    if (dirty_overflow || dirty_rect_count == 0) {
        video_copy_rect_to_front(dirty_x, dirty_y, dirty_w, dirty_h);
    } else {
        for (int i = 0; i < dirty_rect_count; i++) {
            video_dirty_rect_t* rect = &dirty_rects[i];
            video_copy_rect_to_front(rect->x, rect->y, rect->w, rect->h);
        }
    }
    video_clear_dirty_locked();
    video_unlock();
}

void video_maybe_present_pending(void) {
    video_lock();
    int deferred = present_deferred;
    video_unlock();

    if (!deferred) {
        video_present_pending();
    }
}

void video_set_deferred_present(int enabled) {
    init_video_once();
    if (!graphics_mode || !backbuffer_ready) {
        return;
    }

    video_lock();
    present_deferred = enabled ? 1 : 0;
    video_unlock();

    if (!enabled) {
        video_present_pending();
    }
}
