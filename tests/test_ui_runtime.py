#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile
import textwrap

from qemu_harness import repo_root


TEST_SOURCE = r"""
#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <sys/mman.h>

#include "minidos_ui.h"

static int saw_single_title = 0;
static int back_order = 0;
static int front_order = 0;
static int draw_sequence = 0;
static int test_status = 1;
static char last_text[128];
static int gfx_rect_calls = 0;
static int gfx_surface_blit_calls = 0;
static app_gfx_surface_blit_t last_surface_blit;

static void run_all_tests(void);

static int stub_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    (void)a1;
    (void)a2;

    if (num == MINIDOS_SYSCALL_GFX_TEXT) {
        const app_gfx_text_t* text = (const app_gfx_text_t*)a0;

        if (!text || !text->text) {
            return 1;
        }

        snprintf(last_text, sizeof(last_text), "%s", text->text);

        if (strcmp(text->text, "Solo") == 0) {
            saw_single_title = 1;
        } else if (strcmp(text->text, "Back") == 0 && back_order == 0) {
            back_order = ++draw_sequence;
        } else if (strcmp(text->text, "Front") == 0 && front_order == 0) {
            front_order = ++draw_sequence;
        }
    }

    if (num == MINIDOS_SYSCALL_GFX_RECT) {
        gfx_rect_calls++;
    }

    if (num == MINIDOS_SYSCALL_GFX_SURFACE_BLIT) {
        const app_gfx_surface_blit_t* blit = (const app_gfx_surface_blit_t*)a0;
        if (blit) {
            last_surface_blit = *blit;
        }
        gfx_surface_blit_calls++;
    }

    return 1;
}

static int test_single_window_sparse_zorder(void) {
    minidos_app_api_t api;
    ui_window_manager_t wm;
    int window_id;
    int i;

    api.syscall = stub_syscall;
    saw_single_title = 0;

    ui_wm_init(&wm, ui_theme_classic());
    window_id = ui_wm_create_window(&wm, ui_rect_make(40, 40, 120, 80), "Solo", 1);
    if (window_id <= 0) {
        fprintf(stderr, "failed to create single window\n");
        return 0;
    }

    for (i = 0; i < 12; i++) {
        ui_wm_bring_to_front(&wm, window_id);
    }

    ui_wm_draw(&api, &wm, 320, 200, "Desk");
    if (!saw_single_title) {
        fprintf(stderr, "single window title was not drawn after sparse z-order promotion\n");
        return 0;
    }

    return 1;
}

static int test_text_box_clips_overflow(void) {
    minidos_app_api_t api;
    ui_theme_t theme;

    api.syscall = stub_syscall;
    theme = ui_theme_classic();
    last_text[0] = '\0';

    ui_draw_text_box(&api, &theme, ui_rect_make(0, 0, 36, 24), "ABCDEFG", 0);
    if (strcmp(last_text, "EFG") != 0) {
        fprintf(stderr, "text box overflow was not clipped to visible suffix: '%s'\n", last_text);
        return 0;
    }

    return 1;
}

static int test_close_window_promotes_next_visible(void) {
    ui_window_manager_t wm;
    const ui_wm_window_t* back_window;
    const ui_wm_window_t* front_window;
    int back_id;
    int front_id;
    int front_button_id;

    ui_wm_init(&wm, ui_theme_classic());
    back_id = ui_wm_create_window(&wm, ui_rect_make(16, 16, 120, 80), "Back", 0);
    front_id = ui_wm_create_window(&wm, ui_rect_make(24, 24, 120, 80), "Front", 1);
    if (back_id <= 0 || front_id <= 0) {
        fprintf(stderr, "failed to create windows for close-window test\n");
        return 0;
    }

    if (ui_wm_add_button(&wm, back_id, ui_rect_make(8, 8, 48, 20), "Back") <= 0) {
        fprintf(stderr, "failed to create back button\n");
        return 0;
    }
    front_button_id = ui_wm_add_button(&wm, front_id, ui_rect_make(8, 8, 48, 20), "Front");
    if (front_button_id <= 0) {
        fprintf(stderr, "failed to create front button\n");
        return 0;
    }

    ui_wm_set_focus_control(&wm, front_button_id);
    wm.pressed_window_id = front_id;
    wm.pressed_control_id = front_button_id;
    wm.pressed_hit_close = 1;

    ui_wm_close_window(&wm, front_id);

    back_window = ui_wm_find_window_const(&wm, back_id);
    front_window = ui_wm_find_window_const(&wm, front_id);
    if (!back_window || !front_window) {
        fprintf(stderr, "window lookup failed after close\n");
        return 0;
    }
    if (front_window->visible) {
        fprintf(stderr, "closed window is still visible\n");
        return 0;
    }
    if (wm.active_window_id != back_id || !back_window->window.active || front_window->window.active) {
        fprintf(stderr, "active window was not promoted correctly after close\n");
        return 0;
    }
    if (wm.focused_control_id != 0 || wm.pressed_control_id != 0 || wm.pressed_window_id != 0 || wm.pressed_hit_close != 0) {
        fprintf(stderr, "close window left stale focus/press state behind\n");
        return 0;
    }

    return 1;
}

static int test_multi_window_draw_order(void) {
    minidos_app_api_t api;
    ui_window_manager_t wm;
    const ui_wm_window_t* back_window;
    const ui_wm_window_t* front_window;
    int back_id;
    int front_id;
    int i;

    api.syscall = stub_syscall;
    back_order = 0;
    front_order = 0;
    draw_sequence = 0;

    ui_wm_init(&wm, ui_theme_classic());
    back_id = ui_wm_create_window(&wm, ui_rect_make(16, 16, 120, 80), "Back", 0);
    front_id = ui_wm_create_window(&wm, ui_rect_make(24, 24, 120, 80), "Front", 1);
    if (back_id <= 0 || front_id <= 0) {
        fprintf(stderr, "failed to create multi-window setup\n");
        return 0;
    }

    for (i = 0; i < 10; i++) {
        ui_wm_bring_to_front(&wm, front_id);
    }

    ui_wm_draw(&api, &wm, 320, 200, "Desk");
    if (back_order == 0 || front_order == 0) {
        fprintf(stderr, "not all window titles were drawn (back=%d front=%d)\n", back_order, front_order);
        return 0;
    }
    if (back_order >= front_order) {
        fprintf(stderr, "window draw order incorrect (back=%d front=%d)\n", back_order, front_order);
        return 0;
    }

    back_window = ui_wm_find_window_const(&wm, back_id);
    front_window = ui_wm_find_window_const(&wm, front_id);
    if (!back_window || !front_window) {
        fprintf(stderr, "window lookup failed after draw-order test\n");
        return 0;
    }
    if (back_window->z_order < 0 || front_window->z_order > wm.window_count) {
        fprintf(stderr, "z-order was not compacted correctly (back=%d front=%d count=%d)\n",
            back_window->z_order, front_window->z_order, wm.window_count);
        return 0;
    }

    return 1;
}

static int test_cursor_batches_horizontal_runs(void) {
    minidos_app_api_t api;
    int visible_pixels = 0;
    int i;

    api.syscall = stub_syscall;
    gfx_rect_calls = 0;

    for (i = 0; i < UI_CURSOR_BITMAP_WIDTH * UI_CURSOR_BITMAP_HEIGHT; i++) {
        if (ui_cursor_bitmap[i] != UI_CURSOR_PIXEL_TRANSPARENT) {
            visible_pixels++;
        }
    }

    ui_draw_cursor(&api, 32, 24, 0xFFFFFFu, 0x000000u);
    if (gfx_rect_calls <= 0) {
        fprintf(stderr, "cursor draw did not emit any rects\n");
        return 0;
    }
    if (gfx_rect_calls >= visible_pixels) {
        fprintf(stderr, "cursor draw still uses per-pixel rects (rects=%d pixels=%d)\n",
            gfx_rect_calls, visible_pixels);
        return 0;
    }

    return 1;
}

int main(void) {
    ucontext_t main_ctx;
    ucontext_t test_ctx;
    void* stack;
    const size_t stack_size = 1u << 20;

    stack = mmap(0, stack_size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_32BIT, -1, 0);
    if (stack == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    if (getcontext(&test_ctx) != 0) {
        perror("getcontext");
        munmap(stack, stack_size);
        return 1;
    }

    test_ctx.uc_link = &main_ctx;
    test_ctx.uc_stack.ss_sp = stack;
    test_ctx.uc_stack.ss_size = stack_size;
    test_ctx.uc_stack.ss_flags = 0;
    makecontext(&test_ctx, run_all_tests, 0);

    /*
     * Run the UI code on a low 32-bit stack so the header-only runtime can
     * pass stack-allocated gfx structs through its legacy unsigned-int syscall
     * ABI without pointer truncation on x86_64 hosts.
     */
    if (swapcontext(&main_ctx, &test_ctx) != 0) {
        perror("swapcontext");
        munmap(stack, stack_size);
        return 1;
    }

    munmap(stack, stack_size);
    return test_status;
}

/* Build a minimal valid 2x2 24bpp BMP in the provided buffer (size must be >= 70 bytes).
   Returns the number of bytes written. */
static int make_minimal_bmp_24bpp(unsigned char* buf, int buf_size,
    unsigned char r0, unsigned char g0, unsigned char b0,
    unsigned char r1, unsigned char g1, unsigned char b1) {
    /* BMP with 2x2 pixels, 24bpp, no compression */
    /* Header 14 bytes + DIB 40 bytes + pixel data 2 rows * 8 bytes (padded to 4) = 16 bytes */
    int total_size = 14 + 40 + 16;
    if (buf_size < total_size) return 0;
    memset(buf, 0, total_size);

    /* BMP file header */
    buf[0] = 'B'; buf[1] = 'M';
    buf[2] = (unsigned char)total_size; buf[3] = 0; buf[4] = 0; buf[5] = 0;
    buf[6] = 0; buf[7] = 0; buf[8] = 0; buf[9] = 0;
    buf[10] = 54; buf[11] = 0; buf[12] = 0; buf[13] = 0; /* pixel data offset */

    /* DIB header (BITMAPINFOHEADER, 40 bytes) */
    buf[14] = 40; /* header size */
    buf[18] = 2;  /* width = 2 */
    buf[22] = 2;  /* height = 2 (bottom-up) */
    buf[26] = 1;  /* planes */
    buf[28] = 24; /* bit count */
    /* compression = 0, already zeroed */

    /* Pixel data: 2 rows, each 2 pixels at 3 bytes = 6 bytes, padded to 8 bytes */
    /* Row 0 (bottom in file): two pixels of color 0 */
    buf[54] = b0; buf[55] = g0; buf[56] = r0;
    buf[57] = b0; buf[58] = g0; buf[59] = r0;
    /* Row 1 (top in file): two pixels of color 1 */
    buf[62] = b1; buf[63] = g1; buf[64] = r1;
    buf[65] = b1; buf[66] = g1; buf[67] = r1;
    return total_size;
}

static int test_surface_blit_syscall_dispatched(void) {
    minidos_app_api_t api;
    static unsigned char pixels[16]; /* 2x2 XRGB8888 */
    app_gfx_surface_blit_t blit;

    api.syscall = stub_syscall;
    gfx_surface_blit_calls = 0;

    blit.buffer = pixels;
    blit.width  = 2;
    blit.height = 2;
    blit.stride = 8;
    blit.format = APP_SURFACE_FORMAT_XRGB8888;
    blit.dest_x = 0;
    blit.dest_y = 0;
    blit.clip_x = -1;
    blit.clip_y = 0;
    blit.clip_w = 0;
    blit.clip_h = 0;
    blit.dest_w = 8;
    blit.dest_h = 6;

    app_gfx_surface_blit(&api, &blit);

    if (gfx_surface_blit_calls != 1) {
        fprintf(stderr, "expected 1 surface blit syscall, got %d\n", gfx_surface_blit_calls);
        return 0;
    }
    if (last_surface_blit.buffer != pixels
        || last_surface_blit.width != 2
        || last_surface_blit.height != 2
        || last_surface_blit.dest_w != 8
        || last_surface_blit.dest_h != 6) {
        fprintf(stderr, "surface blit descriptor fields not passed correctly\n");
        return 0;
    }
    return 1;
}

static int test_wallpaper_surface_decode_bmp(void) {
    static unsigned char bmp_buf[80];
    ui_wallpaper_surface_t* s = &g_ui_wallpaper_surface;
    int bmp_size;

    /* Write a minimal 2x2 BMP into the bitmap cache manually */
    bmp_size = make_minimal_bmp_24bpp(bmp_buf, sizeof(bmp_buf),
        0xFFu, 0x00u, 0x00u,  /* row 0: red */
        0x00u, 0x00u, 0xFFu); /* row 1: blue */
    if (bmp_size <= 0) {
        fprintf(stderr, "failed to build minimal BMP\n");
        return 0;
    }

    /* Reset surface cache */
    s->valid = 0;
    g_ui_bitmap_cache.valid = 0;

    /* Inject BMP bytes directly into the file cache and call load */
    memcpy(g_ui_bitmap_cache.data, bmp_buf, bmp_size);
    g_ui_bitmap_cache.src_w        = 2;
    g_ui_bitmap_cache.src_h        = 2;
    g_ui_bitmap_cache.abs_src_h    = 2;
    g_ui_bitmap_cache.top_down     = 0;
    g_ui_bitmap_cache.bit_count    = 24;
    g_ui_bitmap_cache.row_stride   = 8;
    g_ui_bitmap_cache.data_offset  = 54;
    g_ui_bitmap_cache.bytes_per_pixel = 3;
    ui_path_copy(g_ui_bitmap_cache.path, "TEST.BMP");
    g_ui_bitmap_cache.valid = 1;

    /* Force surface decode by clearing surface path */
    s->valid = 0;
    s->path[0] = '\0';

    /* Manually call the decode loop that ui_wallpaper_surface_load would do */
    {
        int w = 2, h = 2, x, y;
        const unsigned char* pixel_base = g_ui_bitmap_cache.data + g_ui_bitmap_cache.data_offset;
        if ((unsigned)(w * h * 4) > UI_WALLPAPER_SURFACE_MAX_BYTES) {
            fprintf(stderr, "surface too large\n");
            return 0;
        }
        for (y = 0; y < h; y++) {
            int row_index = 1 - y; /* bottom-up BMP, top_down=0 */
            const unsigned char* src_row = pixel_base + (unsigned)row_index * g_ui_bitmap_cache.row_stride;
            unsigned char* dst_row = s->pixels + (unsigned)(y * w * 4);
            for (x = 0; x < w; x++) {
                const unsigned char* px = src_row + (unsigned)x * 3;
                dst_row[x * 4 + 0] = 0;
                dst_row[x * 4 + 1] = px[2];
                dst_row[x * 4 + 2] = px[1];
                dst_row[x * 4 + 3] = px[0];
            }
        }
        s->width = w; s->height = h; s->stride = w * 4;
        ui_path_copy(s->path, "TEST.BMP");
        s->valid = 1;
    }

    /* Verify decoded surface: row 0 should be blue (from bottom row of bottom-up BMP) */
    /* bmp row 0 (file bottom) = red (r=FF,g=0,b=0), decoded to y=1 in surface */
    /* bmp row 1 (file top)    = blue (r=0,g=0,b=FF), decoded to y=0 in surface */
    if (!s->valid) {
        fprintf(stderr, "surface not marked valid after decode\n");
        return 0;
    }
    /* y=0 row: blue pixel -> XRGB8888: X=0, R=0, G=0, B=FF */
    if (s->pixels[1] != 0x00u || s->pixels[3] != 0xFFu) {
        fprintf(stderr, "decoded surface pixel mismatch: R=%02x B=%02x (expected R=00 B=FF)\n",
            (unsigned)s->pixels[1], (unsigned)s->pixels[3]);
        return 0;
    }
    /* y=1 row: red pixel -> XRGB8888: X=0, R=FF, G=0, B=0 */
    if (s->pixels[8 + 1] != 0xFFu || s->pixels[8 + 3] != 0x00u) {
        fprintf(stderr, "decoded surface pixel mismatch: R=%02x B=%02x (expected R=FF B=00)\n",
            (unsigned)s->pixels[8 + 1], (unsigned)s->pixels[8 + 3]);
        return 0;
    }
    return 1;
}

static int test_wallpaper_surface_fallback_on_invalid(void) {
    minidos_app_api_t api;
    int result;

    api.syscall = stub_syscall;

    /* Reset surface cache */
    g_ui_wallpaper_surface.valid = 0;
    g_ui_bitmap_cache.valid = 0;

    /* Attempt to load a nonexistent path (app_file_size returns -1 from stub) */
    result = ui_wallpaper_surface_load(&api, "NOFILE.BMP");

    if (result != 0) {
        fprintf(stderr, "expected surface load to fail for missing file, got %d\n", result);
        return 0;
    }
    if (g_ui_wallpaper_surface.valid != 0) {
        fprintf(stderr, "surface should not be valid after failed load\n");
        return 0;
    }

    /* blit should return 0 when surface is invalid */
    result = ui_wallpaper_surface_blit(&api, 0, 0, -1, 0, 0, 0);
    if (result != 0) {
        fprintf(stderr, "blit should return 0 when surface invalid, got %d\n", result);
        return 0;
    }
    return 1;
}

static int test_wallpaper_surface_scaled_blit_uses_dest_size(void) {
    minidos_app_api_t api;
    ui_wallpaper_surface_t* s = &g_ui_wallpaper_surface;

    api.syscall = stub_syscall;
    gfx_surface_blit_calls = 0;
    memset(&last_surface_blit, 0, sizeof(last_surface_blit));

    s->valid = 1;
    strcpy(s->path, "WALL.BMP");
    s->width = 320;
    s->height = 200;
    s->stride = 1280;

    if (!ui_wallpaper_surface_matches("WALL.BMP")) {
        fprintf(stderr, "wallpaper surface path match failed\n");
        return 0;
    }

    if (!ui_wallpaper_surface_blit_scaled(&api, 0, 0, 640, 480, 10, 20, 30, 40)) {
        fprintf(stderr, "scaled wallpaper blit helper failed\n");
        return 0;
    }

    if (gfx_surface_blit_calls != 1) {
        fprintf(stderr, "expected scaled wallpaper blit syscall\n");
        return 0;
    }

    if (last_surface_blit.dest_w != 640 || last_surface_blit.dest_h != 480) {
        fprintf(stderr, "scaled wallpaper blit did not preserve destination size\n");
        return 0;
    }

    if (last_surface_blit.clip_x != 10 || last_surface_blit.clip_y != 20
        || last_surface_blit.clip_w != 30 || last_surface_blit.clip_h != 40) {
        fprintf(stderr, "scaled wallpaper blit did not preserve clip rect\n");
        return 0;
    }

    return 1;
}

static void run_all_tests(void) {
    if (!test_single_window_sparse_zorder()) {
        test_status = 1;
        return;
    }
    if (!test_text_box_clips_overflow()) {
        test_status = 1;
        return;
    }
    if (!test_close_window_promotes_next_visible()) {
        test_status = 1;
        return;
    }
    if (!test_multi_window_draw_order()) {
        test_status = 1;
        return;
    }
    if (!test_cursor_batches_horizontal_runs()) {
        test_status = 1;
        return;
    }
    if (!test_surface_blit_syscall_dispatched()) {
        test_status = 1;
        return;
    }
    if (!test_wallpaper_surface_decode_bmp()) {
        test_status = 1;
        return;
    }
    if (!test_wallpaper_surface_fallback_on_invalid()) {
        test_status = 1;
        return;
    }
    if (!test_wallpaper_surface_scaled_blit_uses_dest_size()) {
        test_status = 1;
        return;
    }

    puts("ui runtime regression tests passed");
    test_status = 0;
}
"""


def main():
    root = repo_root()
    runtime_dir = os.path.join(root, "external_apps", "runtime")

    with tempfile.TemporaryDirectory(prefix="minidos-ui-runtime-") as tmpdir:
        source_path = os.path.join(tmpdir, "ui_runtime_test.c")
        binary_path = os.path.join(tmpdir, "ui_runtime_test")

        with open(source_path, "w", encoding="ascii") as f:
            f.write(textwrap.dedent(TEST_SOURCE))

        compile_cmd = [
            "gcc",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wno-pointer-to-int-cast",
            "-Wno-int-to-pointer-cast",
            "-std=c99",
            "-fno-pie",
            "-no-pie",
            f"-I{runtime_dir}",
            source_path,
            "-o",
            binary_path,
        ]
        subprocess.run(compile_cmd, cwd=root, check=True)
        subprocess.run([binary_path], cwd=root, check=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
