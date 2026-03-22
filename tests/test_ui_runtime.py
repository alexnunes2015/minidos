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

static void run_all_tests(void);

static int stub_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    (void)a1;
    (void)a2;

    if (num == MINIDOS_SYSCALL_GFX_TEXT) {
        const app_gfx_text_t* text = (const app_gfx_text_t*)a0;

        if (!text || !text->text) {
            return 1;
        }

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

static int test_multi_window_draw_order(void) {
    minidos_app_api_t api;
    ui_window_manager_t wm;
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
