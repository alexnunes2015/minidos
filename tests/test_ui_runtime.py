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
