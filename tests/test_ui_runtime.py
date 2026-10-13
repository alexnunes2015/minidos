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

#define MINIDOS_UI_IMPLEMENTATION
#include "minidos_ui.h"

static int saw_single_title = 0;
static int back_order = 0;
static int front_order = 0;
static int draw_sequence = 0;
static int test_status = 1;
static char last_text[128];
static int gfx_text_calls = 0;
static int gfx_text_clipped_calls = 0;
static int gfx_rect_calls = 0;
static int gfx_rect_legacy_calls = 0;
static int gfx_rect_batch_calls = 0;
static int gfx_surface_blit_calls = 0;
static app_gfx_surface_blit_t last_surface_blit;
static app_gfx_text_clipped_t last_text_clipped;
static char last_text_clipped_value[128];
static unsigned int stub_ticks = 0;
static int clip_check_enabled = 0;
static int clip_check_failed = 0;
static ui_rect_t clip_check_rect;

#define STUB_RECT_LOG_MAX 512
#define STUB_BATCH_LOG_MAX 64
#define STUB_EVENT_LOG_MAX 1024

enum {
    STUB_EVENT_RECT_BATCH = 1,
    STUB_EVENT_RECT = 2,
    STUB_EVENT_TEXT = 3,
    STUB_EVENT_TEXT_CLIPPED = 4,
    STUB_EVENT_SURFACE = 5,
    STUB_EVENT_PRESENT = 6,
};

static app_gfx_rect_t stub_rect_log[STUB_RECT_LOG_MAX];
static int stub_rect_log_count = 0;
static unsigned int stub_batch_size_log[STUB_BATCH_LOG_MAX];
static int stub_batch_size_log_count = 0;
static int stub_event_log[STUB_EVENT_LOG_MAX];
static int stub_event_log_count = 0;
static int stub_rect_batch_result = 1;
static int stub_text_clipped_result = 1;
static int stub_surface_blit_result = 1;

static void run_all_tests(void);

static void stub_record_event(int event) {
    if (stub_event_log_count < STUB_EVENT_LOG_MAX) {
        stub_event_log[stub_event_log_count++] = event;
    }
}

static void stub_check_rect(const app_gfx_rect_t* rect) {
    if (!rect) {
        return;
    }
    if (stub_rect_log_count < STUB_RECT_LOG_MAX) {
        stub_rect_log[stub_rect_log_count++] = *rect;
    }
    if (clip_check_enabled) {
        if (rect->x < clip_check_rect.x
            || rect->y < clip_check_rect.y
            || rect->x + rect->w > clip_check_rect.x + clip_check_rect.w
            || rect->y + rect->h > clip_check_rect.y + clip_check_rect.h) {
            clip_check_failed = 1;
        }
    }
}

static void stub_record_text(const char* text) {
    if (!text) {
        return;
    }

    snprintf(last_text, sizeof(last_text), "%s", text);
    if (strcmp(text, "Solo") == 0) {
        saw_single_title = 1;
    } else if (strcmp(text, "Back") == 0 && back_order == 0) {
        back_order = ++draw_sequence;
    } else if (strcmp(text, "Front") == 0 && front_order == 0) {
        front_order = ++draw_sequence;
    }
}

static void stub_graphics_reset(void) {
    ui_gfx_rect_batch_reset();
    last_text[0] = '\0';
    last_text_clipped_value[0] = '\0';
    memset(&last_text_clipped, 0, sizeof(last_text_clipped));
    memset(&last_surface_blit, 0, sizeof(last_surface_blit));
    gfx_text_calls = 0;
    gfx_text_clipped_calls = 0;
    gfx_rect_calls = 0;
    gfx_rect_legacy_calls = 0;
    gfx_rect_batch_calls = 0;
    gfx_surface_blit_calls = 0;
    stub_rect_log_count = 0;
    stub_batch_size_log_count = 0;
    stub_event_log_count = 0;
    stub_rect_batch_result = 1;
    stub_text_clipped_result = 1;
    stub_surface_blit_result = 1;
    clip_check_enabled = 0;
    clip_check_failed = 0;
}

static int stub_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    (void)a1;
    (void)a2;

    if (num == MINIDOS_SYSCALL_GFX_TEXT) {
        const app_gfx_text_t* text = (const app_gfx_text_t*)a0;

        gfx_text_calls++;
        stub_record_event(STUB_EVENT_TEXT);
        if (!text || !text->text) {
            return 1;
        }
        stub_record_text(text->text);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_TEXT_CLIPPED) {
        const app_gfx_text_clipped_t* text = (const app_gfx_text_clipped_t*)a0;

        gfx_text_clipped_calls++;
        stub_record_event(STUB_EVENT_TEXT_CLIPPED);
        if (text) {
            last_text_clipped = *text;
            if (text->text) {
                snprintf(last_text_clipped_value, sizeof(last_text_clipped_value), "%s", text->text);
                stub_record_text(text->text);
            }
            if (clip_check_enabled
                && (text->clip_x != clip_check_rect.x
                    || text->clip_y != clip_check_rect.y
                    || text->clip_w != clip_check_rect.w
                    || text->clip_h != clip_check_rect.h)) {
                clip_check_failed = 1;
            }
        }
        return stub_text_clipped_result;
    }

    if (num == MINIDOS_SYSCALL_GFX_RECT) {
        const app_gfx_rect_t* rect = (const app_gfx_rect_t*)a0;

        gfx_rect_calls++;
        gfx_rect_legacy_calls++;
        stub_record_event(STUB_EVENT_RECT);
        stub_check_rect(rect);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_RECT_BATCH) {
        const app_gfx_rect_batch_t* batch = (const app_gfx_rect_batch_t*)a0;
        unsigned int i;

        gfx_rect_batch_calls++;
        stub_record_event(STUB_EVENT_RECT_BATCH);
        if (batch && stub_batch_size_log_count < STUB_BATCH_LOG_MAX) {
            stub_batch_size_log[stub_batch_size_log_count++] = batch->count;
        }
        if (stub_rect_batch_result > 0 && batch && batch->rects) {
            for (i = 0; i < batch->count; i++) {
                gfx_rect_calls++;
                stub_check_rect(&batch->rects[i]);
            }
        }
        return stub_rect_batch_result;
    }

    if (num == MINIDOS_SYSCALL_GFX_SURFACE_BLIT) {
        const app_gfx_surface_blit_t* blit = (const app_gfx_surface_blit_t*)a0;
        if (blit) {
            last_surface_blit = *blit;
        }
        gfx_surface_blit_calls++;
        stub_record_event(STUB_EVENT_SURFACE);
        return stub_surface_blit_result;
    }

    if (num == MINIDOS_SYSCALL_GFX_PRESENT) {
        stub_record_event(STUB_EVENT_PRESENT);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GET_TICKS) {
        return (int)stub_ticks;
    }

    return 1;
}

static int test_single_window_sparse_zorder(void) {
    minidos_app_api_t api;
    ui_window_manager_t wm;
    int window_id;
    int i;

    api.syscall = stub_syscall;
    stub_graphics_reset();
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
    stub_graphics_reset();

    ui_draw_text_box(&api, &theme, ui_rect_make(0, 0, 36, 24), "ABCDEFG", 7, 0);
    if (strcmp(last_text, "EFG") != 0) {
        fprintf(stderr, "text box overflow was not clipped to visible suffix: '%s'\n", last_text);
        return 0;
    }

    return 1;
}

static int test_text_box_draws_focus_cursor(void) {
    minidos_app_api_t api;
    ui_theme_t theme;
    int unfocused_rects;
    int focused_rects;

    api.syscall = stub_syscall;
    theme = ui_theme_classic();

    stub_graphics_reset();
    stub_ticks = 0;
    ui_draw_text_box(&api, &theme, ui_rect_make(0, 0, 84, 24), "ABC", 3, 0);
    (void)ui_gfx_rect_batch_flush(&api);
    unfocused_rects = gfx_rect_calls;

    stub_graphics_reset();
    stub_ticks = 0;
    ui_draw_text_box(&api, &theme, ui_rect_make(0, 0, 84, 24), "ABC", 3, 1);
    (void)ui_gfx_rect_batch_flush(&api);
    focused_rects = gfx_rect_calls;

    if (focused_rects <= unfocused_rects) {
        fprintf(stderr, "focused text box did not draw an insertion cursor\n");
        return 0;
    }

    stub_graphics_reset();
    stub_ticks = 20;
    ui_draw_text_box(&api, &theme, ui_rect_make(0, 0, 84, 24), "ABC", 3, 1);
    (void)ui_gfx_rect_batch_flush(&api);
    if (gfx_rect_calls >= focused_rects) {
        fprintf(stderr, "focused text box cursor did not blink off\n");
        return 0;
    }

    return 1;
}

static int test_text_box_tracks_caret_position(void) {
    minidos_app_api_t api;
    ui_theme_t theme;

    api.syscall = stub_syscall;
    theme = ui_theme_classic();

    stub_graphics_reset();
    ui_draw_text_box(&api, &theme, ui_rect_make(0, 0, 36, 24), "ABCDEFG", 2, 0);
    if (strcmp(last_text, "ABC") != 0) {
        fprintf(stderr, "text box did not show text around an interior caret: '%s'\n", last_text);
        return 0;
    }

    stub_graphics_reset();
    ui_draw_text_box(&api, &theme, ui_rect_make(0, 0, 36, 24), "ABCDEFG", 4, 0);
    if (strcmp(last_text, "BCD") != 0) {
        fprintf(stderr, "text box did not scroll with the interior caret: '%s'\n", last_text);
        return 0;
    }

    return 1;
}

static int test_text_clipped_partial_cell_does_not_overdraw(void) {
    minidos_app_api_t api;

    api.syscall = stub_syscall;
    stub_graphics_reset();
    stub_text_clipped_result = -1;
    clip_check_enabled = 1;
    clip_check_failed = 0;
    clip_check_rect = ui_rect_make(13, 22, 2, 3);

    ui_draw_text_clipped(&api, 10, 20, "A", 0xFFFFFFu, 0x000000u, clip_check_rect);
    (void)ui_gfx_rect_batch_flush(&api);

    clip_check_enabled = 0;
    if (gfx_text_calls != 0) {
        fprintf(stderr, "partial clipped text used whole-cell text syscall\n");
        return 0;
    }
    if (gfx_text_clipped_calls != 1) {
        fprintf(stderr, "partial clipped text fallback did not try the clipped-text syscall\n");
        return 0;
    }
    if (gfx_rect_calls <= 0) {
        fprintf(stderr, "partial clipped text did not render clipped rects\n");
        return 0;
    }
    if (clip_check_failed) {
        fprintf(stderr, "partial clipped text drew outside the clip rect\n");
        return 0;
    }

    return 1;
}

static int test_listview_clipped_uses_surface_clip(void) {
    minidos_app_api_t api;
    ui_theme_t theme;
    ui_listview_t listview;

    api.syscall = stub_syscall;
    theme = ui_theme_classic();
    stub_graphics_reset();
    ui_listview_init(&listview, 40);
    listview.item_count = 1;
    listview.selected_index = 0;
    listview.scroll_offset = 0;
    snprintf(listview.items[0].name, UI_LISTVIEW_ITEM_NAME_MAX, "%s", "ITEM.TXT");
    listview.items[0].is_dir = 0;
    listview.items[0].icon_color = 0;

    ui_draw_listview_clipped(&api, &g_ui_listview_line_buf,
        ui_rect_make(10, 20, 120, 40), &listview, &theme,
        ui_rect_make(15, 25, 7, 6));

    if (gfx_surface_blit_calls != 1) {
        fprintf(stderr, "clipped listview should blit only intersecting rows, got %d\n",
            gfx_surface_blit_calls);
        return 0;
    }
    if (last_surface_blit.clip_x != 15 || last_surface_blit.clip_y != 25
        || last_surface_blit.clip_w != 7 || last_surface_blit.clip_h != 6) {
        fprintf(stderr, "clipped listview blit used wrong clip: %d,%d %dx%d\n",
            last_surface_blit.clip_x, last_surface_blit.clip_y,
            last_surface_blit.clip_w, last_surface_blit.clip_h);
        return 0;
    }

    return 1;
}

static int test_listview_surface_rejection_falls_back_to_clipped_runs(void) {
    minidos_app_api_t api;
    ui_theme_t theme;
    ui_listview_item_t item;
    ui_rect_t clip = ui_rect_make(10, 20, 8, 3);
    int row;

    api.syscall = stub_syscall;
    theme = ui_theme_classic();
    memset(&item, 0, sizeof(item));
    snprintf(item.name, sizeof(item.name), "%s", "ITEM.TXT");
    stub_graphics_reset();
    stub_surface_blit_result = -1;
    clip_check_enabled = 1;
    clip_check_rect = clip;

    ui_draw_listview_line_clipped(&api, &g_ui_listview_line_buf,
        10, 20, 120, &item, 1, 0, &theme, clip);
    (void)ui_gfx_rect_batch_flush(&api);

    clip_check_enabled = 0;
    if (gfx_surface_blit_calls != 1
        || last_surface_blit.format != APP_SURFACE_FORMAT_XRGB8888_LE
        || last_surface_blit.clip_x != clip.x || last_surface_blit.clip_y != clip.y
        || last_surface_blit.clip_w != clip.w || last_surface_blit.clip_h != clip.h) {
        fprintf(stderr, "listview fallback did not first attempt the packed clipped blit\n");
        return 0;
    }
    if (clip_check_failed || gfx_rect_batch_calls != 1
        || gfx_rect_legacy_calls != 0 || gfx_rect_calls != 6
        || stub_rect_log_count != 6) {
        fprintf(stderr, "listview surface rejection did not produce six clipped color runs\n");
        return 0;
    }
    for (row = 0; row < 3; row++) {
        const app_gfx_rect_t* indicator = &stub_rect_log[row * 2];
        const app_gfx_rect_t* selection = &stub_rect_log[row * 2 + 1];

        if (indicator->x != 10 || indicator->y != 20 + row
            || indicator->w != 3 || indicator->h != 1
            || indicator->color != theme.light
            || selection->x != 13 || selection->y != 20 + row
            || selection->w != 5 || selection->h != 1
            || selection->color != theme.selection_bg) {
            fprintf(stderr, "listview fallback changed run order or packed colors on row %d\n", row);
            return 0;
        }
    }
    if (stub_event_log_count != 2
        || stub_event_log[0] != STUB_EVENT_SURFACE
        || stub_event_log[1] != STUB_EVENT_RECT_BATCH) {
        fprintf(stderr, "listview fallback emitted an unexpected command sequence\n");
        return 0;
    }

    return 1;
}

static int test_checkbox_and_radio_keyboard_dispatch(void) {
    ui_window_manager_t wm;
    int window_id;
    int checkbox_id;
    int radio_a_id;
    int radio_b_id;
    const ui_control_t* checkbox;
    const ui_control_t* radio_a;
    const ui_control_t* radio_b;

    ui_wm_init(&wm, ui_theme_classic());
    window_id = ui_wm_create_window(&wm, ui_rect_make(10, 10, 220, 120), "Controls", 1);
    checkbox_id = ui_wm_add_checkbox(&wm, window_id, ui_rect_make(8, 8, 120, 20), "Check", 0);
    radio_a_id = ui_wm_add_radio(&wm, window_id, ui_rect_make(8, 32, 120, 20), "A", 7, 1);
    radio_b_id = ui_wm_add_radio(&wm, window_id, ui_rect_make(8, 56, 120, 20), "B", 7, 0);
    if (checkbox_id <= 0 || radio_a_id <= 0 || radio_b_id <= 0) {
        fprintf(stderr, "failed to create checkbox/radio controls\n");
        return 0;
    }

    ui_wm_set_focus_control(&wm, checkbox_id);
    if (!ui_wm_dispatch_key(&wm, 32)) {
        fprintf(stderr, "checkbox did not react to keyboard activation\n");
        return 0;
    }

    ui_wm_set_focus_control(&wm, radio_b_id);
    if (!ui_wm_dispatch_key(&wm, 32)) {
        fprintf(stderr, "radio did not react to keyboard activation\n");
        return 0;
    }

    checkbox = ui_wm_find_control_const(&wm, checkbox_id);
    radio_a = ui_wm_find_control_const(&wm, radio_a_id);
    radio_b = ui_wm_find_control_const(&wm, radio_b_id);
    if (!checkbox || !radio_a || !radio_b || !checkbox->checked || radio_a->checked || !radio_b->checked) {
        fprintf(stderr, "checkbox/radio keyboard state incorrect\n");
        return 0;
    }

    return 1;
}

static int test_textinput_caret_keyboard_navigation(void) {
    ui_window_manager_t wm;
    int window_id;
    int input_id;
    char input[16] = "ABCD";
    const ui_control_t* control;

    ui_wm_init(&wm, ui_theme_classic());
    window_id = ui_wm_create_window(&wm, ui_rect_make(10, 10, 220, 120), "Input", 1);
    input_id = ui_wm_add_textinput(&wm, window_id, ui_rect_make(8, 8, 120, 20), input, (int)sizeof(input));
    if (input_id <= 0) {
        fprintf(stderr, "failed to create text input\n");
        return 0;
    }

    ui_wm_set_focus_control(&wm, input_id);
    if (!ui_wm_dispatch_key(&wm, 0x15) || !ui_wm_dispatch_key(&wm, 0x15)
        || !ui_wm_dispatch_key(&wm, 'X')) {
        fprintf(stderr, "text input did not accept left-arrow insertion\n");
        return 0;
    }
    control = ui_wm_find_control_const(&wm, input_id);
    if (!control || strcmp(input, "ABXCD") != 0 || control->caret_pos != 3 || control->text_len != 5) {
        fprintf(stderr, "left-arrow insertion did not preserve text around the caret\n");
        return 0;
    }

    if (!ui_wm_dispatch_key(&wm, 8) || !ui_wm_dispatch_key(&wm, 0x16)
        || !ui_wm_dispatch_key(&wm, 'Y')) {
        fprintf(stderr, "text input did not accept right-arrow insertion\n");
        return 0;
    }
    control = ui_wm_find_control_const(&wm, input_id);
    if (!control || strcmp(input, "ABCYD") != 0 || control->caret_pos != 4 || control->text_len != 5) {
        fprintf(stderr, "right-arrow insertion did not preserve text around the caret\n");
        return 0;
    }

    return 1;
}

static int test_dropdown_and_scrollbar_mouse_dispatch(void) {
    ui_window_manager_t wm;
    int window_id;
    int dropdown_id;
    int scrollbar_id;
    const char* const items[] = {"One", "Two", "Three"};
    ui_control_t* dropdown;
    ui_control_t* scrollbar;
    int out_window_id = 0;
    int out_control_id = 0;
    int out_hit_close = 0;

    ui_wm_init(&wm, ui_theme_classic());
    window_id = ui_wm_create_window(&wm, ui_rect_make(20, 20, 260, 180), "Widget", 1);
    dropdown_id = ui_wm_add_dropdown(&wm, window_id, ui_rect_make(12, 12, 120, 24), items, 3, 0);
    scrollbar_id = ui_wm_add_scrollbar(&wm, window_id, ui_rect_make(180, 12, 18, 120), 0, 20, 4, 5);
    if (dropdown_id <= 0 || scrollbar_id <= 0) {
        fprintf(stderr, "failed to create dropdown/scrollbar controls\n");
        return 0;
    }

    if (ui_wm_dispatch_mouse(&wm, 50, 66, 1, 1, 0, &out_window_id, &out_control_id, &out_hit_close) != 0) {
        fprintf(stderr, "dropdown header press unexpectedly activated\n");
        return 0;
    }
    if (ui_wm_dispatch_mouse(&wm, 50, 66, 0, 0, 1, &out_window_id, &out_control_id, &out_hit_close) != 1) {
        fprintf(stderr, "dropdown header click did not open popup\n");
        return 0;
    }
    dropdown = ui_wm_find_control(&wm, dropdown_id);
    if (!dropdown || !dropdown->open) {
        fprintf(stderr, "dropdown popup was not opened\n");
        return 0;
    }

    if (ui_wm_dispatch_mouse(&wm, 50, 102, 1, 1, 0, &out_window_id, &out_control_id, &out_hit_close) != 0) {
        fprintf(stderr, "dropdown item press unexpectedly activated\n");
        return 0;
    }
    if (ui_wm_dispatch_mouse(&wm, 50, 102, 0, 0, 1, &out_window_id, &out_control_id, &out_hit_close) != 1) {
        fprintf(stderr, "dropdown item click did not select item\n");
        return 0;
    }

    dropdown = ui_wm_find_control(&wm, dropdown_id);
    if (!dropdown || dropdown->selected_index != 1 || dropdown->open) {
        fprintf(stderr, "dropdown selection was not updated by mouse click\n");
        return 0;
    }

    if (ui_wm_dispatch_mouse(&wm, 210, 130, 1, 1, 0, &out_window_id, &out_control_id, &out_hit_close) == 0) {
        scrollbar = ui_wm_find_control(&wm, scrollbar_id);
        if (!scrollbar || scrollbar->value <= 5) {
            fprintf(stderr, "scrollbar page click did not advance value\n");
            return 0;
        }
    } else {
        fprintf(stderr, "scrollbar press should report state change via control update\n");
        return 0;
    }

    return 1;
}

static int test_control_visible_bounds_clip_to_client(void) {
    ui_window_manager_t wm;
    int window_id;
    int button_id;
    const ui_control_t* button;
    ui_rect_t visible;

    ui_wm_init(&wm, ui_theme_classic());
    window_id = ui_wm_create_window(&wm, ui_rect_make(20, 20, 120, 80), "Clip", 1);
    button_id = ui_wm_add_button(&wm, window_id, ui_rect_make(70, 40, 60, 24), "Wide");
    if (window_id <= 0 || button_id <= 0) {
        fprintf(stderr, "failed to create clipping test controls\n");
        return 0;
    }

    button = ui_wm_find_control_const(&wm, button_id);
    visible = ui_wm_control_visible_bounds(&wm, button);
    if (visible.w <= 0 || visible.h <= 0) {
        fprintf(stderr, "visible bounds unexpectedly empty\n");
        return 0;
    }
    if (visible.w >= 60) {
        fprintf(stderr, "control bounds were not clipped to the window client area\n");
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
    if (!back_window) {
        fprintf(stderr, "window lookup failed after close\n");
        return 0;
    }
    if (front_window) {
        fprintf(stderr, "closed window id was still discoverable\n");
        return 0;
    }
    if (wm.active_window_id != back_id || !back_window->window.active) {
        fprintf(stderr, "active window was not promoted correctly after close\n");
        return 0;
    }
    if (wm.focused_control_id != 0 || wm.pressed_control_id != 0 || wm.pressed_window_id != 0 || wm.pressed_hit_close != 0) {
        fprintf(stderr, "close window left stale focus/press state behind\n");
        return 0;
    }

    return 1;
}

static int test_minimize_restore_and_maximize_window(void) {
    ui_window_manager_t wm;
    const ui_wm_window_t* back_window;
    const ui_wm_window_t* front_window;
    ui_rect_t restore_bounds;
    ui_rect_t max_bounds;
    int back_id;
    int front_id;

    ui_wm_init(&wm, ui_theme_classic());
    back_id = ui_wm_create_window(&wm, ui_rect_make(16, 16, 120, 80), "Back", 0);
    front_id = ui_wm_create_window_ex(&wm, ui_rect_make(24, 24, 140, 90), "Front", 1, 1, 1);
    if (back_id <= 0 || front_id <= 0) {
        fprintf(stderr, "failed to create windows for minimize/maximize test\n");
        return 0;
    }

    ui_wm_minimize_window(&wm, front_id);
    back_window = ui_wm_find_window_const(&wm, back_id);
    front_window = ui_wm_find_window_const(&wm, front_id);
    if (!back_window || !front_window) {
        fprintf(stderr, "window lookup failed after minimize\n");
        return 0;
    }
    if (!front_window->visible || !front_window->window.minimized) {
        fprintf(stderr, "minimized window was not retained for taskbar restore\n");
        return 0;
    }
    if (ui_wm_top_window_at(&wm, 30, 30) != back_id || wm.active_window_id != back_id) {
        fprintf(stderr, "minimized front window still participates in hit-test/activation\n");
        return 0;
    }

    ui_wm_restore_window(&wm, front_id);
    front_window = ui_wm_find_window_const(&wm, front_id);
    if (!front_window || front_window->window.minimized || wm.active_window_id != front_id) {
        fprintf(stderr, "restore did not reactivate minimized window\n");
        return 0;
    }

    restore_bounds = front_window->window.bounds;
    max_bounds = ui_rect_make(0, 0, 320, 172);
    ui_wm_toggle_maximize_window(&wm, front_id, max_bounds);
    front_window = ui_wm_find_window_const(&wm, front_id);
    if (!front_window || !front_window->window.maximized
        || front_window->window.bounds.w != max_bounds.w
        || front_window->window.bounds.h != max_bounds.h) {
        fprintf(stderr, "maximize did not apply desktop bounds\n");
        return 0;
    }

    ui_wm_toggle_maximize_window(&wm, front_id, max_bounds);
    front_window = ui_wm_find_window_const(&wm, front_id);
    if (!front_window || front_window->window.maximized
        || front_window->window.bounds.x != restore_bounds.x
        || front_window->window.bounds.y != restore_bounds.y
        || front_window->window.bounds.w != restore_bounds.w
        || front_window->window.bounds.h != restore_bounds.h) {
        fprintf(stderr, "restore from maximized did not recover original bounds\n");
        return 0;
    }

    return 1;
}

static int test_static_window_ignores_minimize_maximize(void) {
    ui_window_manager_t wm;
    const ui_wm_window_t* window;
    ui_rect_t original_bounds;
    int window_id;

    ui_wm_init(&wm, ui_theme_classic());
    window_id = ui_wm_create_window(&wm, ui_rect_make(24, 24, 140, 90), "Static", 1);
    if (window_id <= 0) {
        fprintf(stderr, "failed to create static window\n");
        return 0;
    }

    window = ui_wm_find_window_const(&wm, window_id);
    if (!window) {
        fprintf(stderr, "static window lookup failed\n");
        return 0;
    }
    original_bounds = window->window.bounds;

    ui_wm_minimize_window(&wm, window_id);
    ui_wm_toggle_maximize_window(&wm, window_id, ui_rect_make(0, 0, 320, 172));

    window = ui_wm_find_window_const(&wm, window_id);
    if (!window || window->window.minimized || window->window.maximized
        || window->window.bounds.x != original_bounds.x
        || window->window.bounds.y != original_bounds.y
        || window->window.bounds.w != original_bounds.w
        || window->window.bounds.h != original_bounds.h) {
        fprintf(stderr, "static window accepted minimize/maximize operation\n");
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
    stub_graphics_reset();
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
    stub_graphics_reset();

    for (i = 0; i < UI_CURSOR_BITMAP_WIDTH * UI_CURSOR_BITMAP_HEIGHT; i++) {
        if (ui_cursor_bitmap[i] != UI_CURSOR_PIXEL_TRANSPARENT) {
            visible_pixels++;
        }
    }

    ui_draw_cursor(&api, 32, 24, 0xFFFFFFu, 0x000000u);
    (void)ui_gfx_rect_batch_flush(&api);
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

static int test_rect_batch_flushes_at_capacity_in_order(void) {
    minidos_app_api_t api;
    unsigned int i;

    api.syscall = stub_syscall;
    stub_graphics_reset();

    for (i = 0; i < APP_GFX_RECT_BATCH_MAX; i++) {
        ui_fill_rect(&api, ui_rect_make((int)i, (int)i + 10, 1, 1), 0x100000u + i);
    }

    if (gfx_rect_batch_calls != 1
        || stub_batch_size_log_count != 1
        || stub_batch_size_log[0] != APP_GFX_RECT_BATCH_MAX) {
        fprintf(stderr, "full rectangle queue did not auto-flush one %u-entry batch\n",
            (unsigned int)APP_GFX_RECT_BATCH_MAX);
        return 0;
    }
    if (g_ui_gfx_rect_batch.count != 0U) {
        fprintf(stderr, "rectangle queue remained full after its capacity flush\n");
        return 0;
    }

    ui_fill_rect(&api, ui_rect_make(100, 110, 2, 3), 0xABCDEFu);
    (void)ui_gfx_rect_batch_flush(&api);

    if (gfx_rect_batch_calls != 2
        || stub_batch_size_log_count != 2
        || stub_batch_size_log[1] != 1U
        || gfx_rect_calls != (int)APP_GFX_RECT_BATCH_MAX + 1
        || gfx_rect_legacy_calls != 0) {
        fprintf(stderr, "rectangle batch boundary counts were incorrect\n");
        return 0;
    }
    for (i = 0; i < APP_GFX_RECT_BATCH_MAX; i++) {
        if (stub_rect_log[i].x != (int)i
            || stub_rect_log[i].y != (int)i + 10
            || stub_rect_log[i].color != 0x100000u + i) {
            fprintf(stderr, "rectangle batch changed submission order at index %u\n", i);
            return 0;
        }
    }
    if (stub_rect_log[APP_GFX_RECT_BATCH_MAX].x != 100
        || stub_rect_log[APP_GFX_RECT_BATCH_MAX].color != 0xABCDEFu) {
        fprintf(stderr, "rectangle after capacity boundary was not preserved\n");
        return 0;
    }

    return 1;
}

static int test_rect_batch_falls_back_to_legacy_in_order(void) {
    minidos_app_api_t api;

    api.syscall = stub_syscall;
    stub_graphics_reset();
    stub_rect_batch_result = -1;

    ui_fill_rect(&api, ui_rect_make(4, 5, 6, 7), 0x112233u);
    ui_fill_rect(&api, ui_rect_make(8, 9, 10, 11), 0x445566u);
    if (!ui_gfx_rect_batch_flush(&api)) {
        fprintf(stderr, "legacy rectangle fallback reported failure\n");
        return 0;
    }

    if (gfx_rect_batch_calls != 1 || gfx_rect_legacy_calls != 2 || gfx_rect_calls != 2) {
        fprintf(stderr, "rejected rectangle batch did not fall back exactly once per rectangle\n");
        return 0;
    }
    if (stub_rect_log_count != 2
        || stub_rect_log[0].x != 4 || stub_rect_log[0].color != 0x112233u
        || stub_rect_log[1].x != 8 || stub_rect_log[1].color != 0x445566u) {
        fprintf(stderr, "legacy rectangle fallback changed submission order\n");
        return 0;
    }
    if (stub_event_log_count != 3
        || stub_event_log[0] != STUB_EVENT_RECT_BATCH
        || stub_event_log[1] != STUB_EVENT_RECT
        || stub_event_log[2] != STUB_EVENT_RECT) {
        fprintf(stderr, "legacy rectangle fallback emitted an unexpected command sequence\n");
        return 0;
    }

    return 1;
}

static int test_rect_batch_flushes_before_ordered_operations(void) {
    minidos_app_api_t api;
    ui_wallpaper_surface_t* surface = &g_ui_wallpaper_surface;

    api.syscall = stub_syscall;

    stub_graphics_reset();
    ui_fill_rect(&api, ui_rect_make(1, 2, 3, 4), 0x010203u);
    ui_draw_text(&api, 5, 6, "Text", 0xFFFFFFu, 0x000000u);
    if (stub_event_log_count != 2
        || stub_event_log[0] != STUB_EVENT_RECT_BATCH
        || stub_event_log[1] != STUB_EVENT_TEXT) {
        fprintf(stderr, "rectangle batch was not flushed before text\n");
        return 0;
    }

    stub_graphics_reset();
    surface->valid = 1;
    surface->width = 1;
    surface->height = 1;
    surface->stride = 4;
    ui_fill_rect(&api, ui_rect_make(7, 8, 9, 10), 0x040506u);
    if (!ui_wallpaper_surface_blit_scaled(&api, 0, 0, 1, 1, -1, 0, 0, 0)) {
        fprintf(stderr, "surface helper unexpectedly rejected a valid cached surface\n");
        return 0;
    }
    if (stub_event_log_count != 2
        || stub_event_log[0] != STUB_EVENT_RECT_BATCH
        || stub_event_log[1] != STUB_EVENT_SURFACE) {
        fprintf(stderr, "rectangle batch was not flushed before surface blit\n");
        return 0;
    }

    stub_graphics_reset();
    ui_fill_rect(&api, ui_rect_make(11, 12, 13, 14), 0x070809u);
    ui_present(&api);
    if (stub_event_log_count != 2
        || stub_event_log[0] != STUB_EVENT_RECT_BATCH
        || stub_event_log[1] != STUB_EVENT_PRESENT) {
        fprintf(stderr, "rectangle batch was not flushed before present\n");
        return 0;
    }

    return 1;
}

static int test_clipped_text_uses_one_syscall(void) {
    minidos_app_api_t api;
    ui_rect_t clip = ui_rect_make(13, 22, 17, 9);

    api.syscall = stub_syscall;
    stub_graphics_reset();
    clip_check_enabled = 1;
    clip_check_rect = clip;

    ui_draw_text_clipped(&api, 10, 20, "AB", 0xAABBCCu, 0x112233u, clip);

    clip_check_enabled = 0;
    if (gfx_text_clipped_calls != 1 || gfx_text_calls != 0 || gfx_rect_calls != 0) {
        fprintf(stderr, "clipped text did not use exactly one clipped-text syscall\n");
        return 0;
    }
    if (stub_event_log_count != 1 || stub_event_log[0] != STUB_EVENT_TEXT_CLIPPED) {
        fprintf(stderr, "clipped text emitted extra graphics commands\n");
        return 0;
    }
    if (clip_check_failed
        || strcmp(last_text_clipped_value, "AB") != 0
        || last_text_clipped.x != 10 || last_text_clipped.y != 20
        || last_text_clipped.fg != 0xAABBCCu
        || last_text_clipped.bg != 0x112233u
        || last_text_clipped.flags != 0U) {
        fprintf(stderr, "clipped text descriptor fields were not preserved\n");
        return 0;
    }

    return 1;
}

static int test_transparent_clipped_text_sets_flag(void) {
    minidos_app_api_t api;
    ui_rect_t clip = ui_rect_make(3, 4, 20, 8);

    api.syscall = stub_syscall;
    stub_graphics_reset();

    ui_draw_text_transparent_clipped(&api, 1, 2, "Glass", 0xFEDCBAu, clip);

    if (gfx_text_clipped_calls != 1 || gfx_text_calls != 0 || gfx_rect_calls != 0
        || strcmp(last_text_clipped_value, "Glass") != 0
        || last_text_clipped.bg != 0U
        || last_text_clipped.flags != APP_GFX_TEXT_TRANSPARENT_BG) {
        fprintf(stderr, "transparent clipped text did not set its ABI flag in one syscall\n");
        return 0;
    }

    return 1;
}

static int test_clipped_text_empty_clip_is_noop(void) {
    minidos_app_api_t api;

    api.syscall = stub_syscall;
    stub_graphics_reset();

    ui_draw_text_clipped(&api, 10, 20, "Hidden", 0xFFFFFFu, 0x000000u,
        ui_rect_make(10, 20, 0, 8));

    if (stub_event_log_count != 0
        || gfx_text_clipped_calls != 0 || gfx_text_calls != 0
        || gfx_surface_blit_calls != 0 || gfx_rect_calls != 0
        || gfx_rect_batch_calls != 0) {
        fprintf(stderr, "opaque clipped text emitted graphics commands for an empty clip\n");
        return 0;
    }

    return 1;
}

static int test_transparent_clipped_text_empty_clip_is_noop(void) {
    minidos_app_api_t api;

    api.syscall = stub_syscall;
    stub_graphics_reset();

    ui_draw_text_transparent_clipped(&api, 10, 20, "Hidden", 0xFFFFFFu,
        ui_rect_make(10, 20, 8, 0));

    if (stub_event_log_count != 0
        || gfx_text_clipped_calls != 0 || gfx_text_calls != 0
        || gfx_surface_blit_calls != 0 || gfx_rect_calls != 0
        || gfx_rect_batch_calls != 0) {
        fprintf(stderr, "transparent clipped text emitted graphics commands for an empty clip\n");
        return 0;
    }

    return 1;
}

static int test_bitmap_clipped_empty_clip_is_noop(void) {
    minidos_app_api_t api;
    ui_wallpaper_surface_t* surface = &g_ui_wallpaper_surface;

    api.syscall = stub_syscall;
    stub_graphics_reset();
    surface->valid = 1;
    surface->width = 2;
    surface->height = 2;
    surface->stride = 8;
    ui_path_copy(surface->path, "EMPTY.BMP");

    if (!ui_draw_bitmap_clipped(&api, "EMPTY.BMP", 0, 0, 320, 200,
            ui_rect_make(100, 100, 0, 0))) {
        fprintf(stderr, "empty bitmap clip should be a successful no-op\n");
        return 0;
    }
    if (stub_event_log_count != 0
        || gfx_text_clipped_calls != 0 || gfx_text_calls != 0
        || gfx_surface_blit_calls != 0 || gfx_rect_calls != 0
        || gfx_rect_batch_calls != 0) {
        fprintf(stderr, "clipped bitmap emitted graphics commands for an empty clip\n");
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
    static unsigned char pixels[16]; /* 2x2 packed ARGB8888_LE */
    app_gfx_surface_blit_t blit;

    api.syscall = stub_syscall;
    stub_graphics_reset();

    blit.buffer = pixels;
    blit.width  = 2;
    blit.height = 2;
    blit.stride = 8;
    blit.format = APP_SURFACE_FORMAT_ARGB8888_LE;
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
        || last_surface_blit.format != APP_SURFACE_FORMAT_ARGB8888_LE
        || last_surface_blit.dest_w != 8
        || last_surface_blit.dest_h != 6) {
        fprintf(stderr, "surface blit descriptor fields not passed correctly\n");
        return 0;
    }
    return 1;
}

static int test_wallpaper_surface_decode_bmp(void) {
    minidos_app_api_t api;
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

    api.syscall = stub_syscall;
    stub_graphics_reset();

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

    if (!ui_wallpaper_surface_load(&api, "TEST.BMP")) {
        fprintf(stderr, "wallpaper surface decode failed\n");
        return 0;
    }

    /* Verify decoded surface: row 0 should be blue (from bottom row of bottom-up BMP) */
    /* bmp row 0 (file bottom) = red (r=FF,g=0,b=0), decoded to y=1 in surface */
    /* bmp row 1 (file top)    = blue (r=0,g=0,b=FF), decoded to y=0 in surface */
    if (!s->valid) {
        fprintf(stderr, "surface not marked valid after decode\n");
        return 0;
    }
    /* y=0 row: blue 0x000000FF -> little-endian bytes [B,G,R,X]. */
    if (s->pixels[0] != 0xFFu || s->pixels[1] != 0x00u
        || s->pixels[2] != 0x00u || s->pixels[3] != 0x00u) {
        fprintf(stderr, "decoded blue pixel is not packed [B,G,R,X]\n");
        return 0;
    }
    /* y=1 row: red 0x00FF0000 -> little-endian bytes [B,G,R,X]. */
    if (s->pixels[8] != 0x00u || s->pixels[9] != 0x00u
        || s->pixels[10] != 0xFFu || s->pixels[11] != 0x00u) {
        fprintf(stderr, "decoded red pixel is not packed [B,G,R,X]\n");
        return 0;
    }
    return 1;
}

static int test_wallpaper_surface_fallback_on_invalid(void) {
    minidos_app_api_t api;
    int result;

    api.syscall = stub_syscall;
    stub_graphics_reset();

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
    stub_graphics_reset();

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

    if (last_surface_blit.format != APP_SURFACE_FORMAT_XRGB8888_LE) {
        fprintf(stderr, "wallpaper surface did not use packed XRGB8888_LE\n");
        return 0;
    }

    if (last_surface_blit.clip_x != 10 || last_surface_blit.clip_y != 20
        || last_surface_blit.clip_w != 30 || last_surface_blit.clip_h != 40) {
        fprintf(stderr, "scaled wallpaper blit did not preserve clip rect\n");
        return 0;
    }

    return 1;
}

static int test_wallpaper_surface_rejection_falls_back_to_scaled_runs(void) {
    minidos_app_api_t api;
    ui_wallpaper_surface_t* surface = &g_ui_wallpaper_surface;
    ui_rect_t clip = ui_rect_make(11, 20, 2, 2);
    static const unsigned char pixels[16] = {
        0x00, 0x00, 0xFF, 0x00, /* red */
        0x00, 0xFF, 0x00, 0x00, /* green */
        0xFF, 0x00, 0x00, 0x00, /* blue */
        0xFF, 0xFF, 0xFF, 0x00, /* white */
    };
    static const unsigned int expected_colors[4] = {
        0xFF0000u, 0x00FF00u, 0x0000FFu, 0xFFFFFFu,
    };
    int i;

    api.syscall = stub_syscall;
    stub_graphics_reset();
    stub_surface_blit_result = -1;
    surface->valid = 1;
    surface->width = 2;
    surface->height = 2;
    surface->stride = 8;
    memcpy(surface->pixels, pixels, sizeof(pixels));
    clip_check_enabled = 1;
    clip_check_rect = clip;

    if (!ui_wallpaper_surface_blit_scaled(&api, 10, 20, 4, 2,
            clip.x, clip.y, clip.w, clip.h)) {
        fprintf(stderr, "wallpaper packed-blit rejection did not enter its rectangle fallback\n");
        return 0;
    }
    (void)ui_gfx_rect_batch_flush(&api);
    clip_check_enabled = 0;

    if (gfx_surface_blit_calls != 1
        || last_surface_blit.format != APP_SURFACE_FORMAT_XRGB8888_LE
        || last_surface_blit.dest_x != 10 || last_surface_blit.dest_y != 20
        || last_surface_blit.dest_w != 4 || last_surface_blit.dest_h != 2
        || last_surface_blit.clip_x != clip.x || last_surface_blit.clip_y != clip.y
        || last_surface_blit.clip_w != clip.w || last_surface_blit.clip_h != clip.h) {
        fprintf(stderr, "wallpaper fallback did not first attempt the requested packed blit\n");
        return 0;
    }
    if (clip_check_failed || gfx_rect_batch_calls != 1
        || gfx_rect_legacy_calls != 0 || gfx_rect_calls != 4
        || stub_rect_log_count != 4) {
        fprintf(stderr, "wallpaper surface rejection did not produce four clipped color runs\n");
        return 0;
    }
    for (i = 0; i < 4; i++) {
        const app_gfx_rect_t* rect = &stub_rect_log[i];
        int expected_x = 11 + (i & 1);
        int expected_y = 20 + (i / 2);

        if (rect->x != expected_x || rect->y != expected_y
            || rect->w != 1 || rect->h != 1
            || rect->color != expected_colors[i]) {
            fprintf(stderr, "wallpaper fallback changed scaling order or color at run %d\n", i);
            return 0;
        }
    }
    if (stub_event_log_count != 2
        || stub_event_log[0] != STUB_EVENT_SURFACE
        || stub_event_log[1] != STUB_EVENT_RECT_BATCH) {
        fprintf(stderr, "wallpaper fallback emitted an unexpected command sequence\n");
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
    if (!test_text_box_draws_focus_cursor()) {
        test_status = 1;
        return;
    }
    if (!test_text_box_tracks_caret_position()) {
        test_status = 1;
        return;
    }
    if (!test_textinput_caret_keyboard_navigation()) {
        test_status = 1;
        return;
    }
    if (!test_clipped_text_uses_one_syscall()) {
        test_status = 1;
        return;
    }
    if (!test_transparent_clipped_text_sets_flag()) {
        test_status = 1;
        return;
    }
    if (!test_clipped_text_empty_clip_is_noop()) {
        test_status = 1;
        return;
    }
    if (!test_transparent_clipped_text_empty_clip_is_noop()) {
        test_status = 1;
        return;
    }
    if (!test_bitmap_clipped_empty_clip_is_noop()) {
        test_status = 1;
        return;
    }
    if (!test_text_clipped_partial_cell_does_not_overdraw()) {
        test_status = 1;
        return;
    }
    if (!test_listview_clipped_uses_surface_clip()) {
        test_status = 1;
        return;
    }
    if (!test_listview_surface_rejection_falls_back_to_clipped_runs()) {
        test_status = 1;
        return;
    }
    if (!test_close_window_promotes_next_visible()) {
        test_status = 1;
        return;
    }
    if (!test_checkbox_and_radio_keyboard_dispatch()) {
        test_status = 1;
        return;
    }
    if (!test_dropdown_and_scrollbar_mouse_dispatch()) {
        test_status = 1;
        return;
    }
    if (!test_control_visible_bounds_clip_to_client()) {
        test_status = 1;
        return;
    }
    if (!test_minimize_restore_and_maximize_window()) {
        test_status = 1;
        return;
    }
    if (!test_static_window_ignores_minimize_maximize()) {
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
    if (!test_rect_batch_falls_back_to_legacy_in_order()) {
        test_status = 1;
        return;
    }
    if (!test_rect_batch_flushes_before_ordered_operations()) {
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
    if (!test_wallpaper_surface_rejection_falls_back_to_scaled_runs()) {
        test_status = 1;
        return;
    }
    if (!test_rect_batch_flushes_at_capacity_in_order()) {
        test_status = 1;
        return;
    }

    puts("ui runtime regression tests passed");
    test_status = 0;
}
"""


ABI_TEST_SOURCE = r"""
#include "minidos_app.h"

#define ABI_ASSERT(name, condition) typedef char name[(condition) ? 1 : -1]

ABI_ASSERT(abi_pointer_is_32_bit, sizeof(void*) == 4);
ABI_ASSERT(abi_rect_size, sizeof(app_gfx_rect_t) == 20);
ABI_ASSERT(abi_rect_color_offset, __builtin_offsetof(app_gfx_rect_t, color) == 16);
ABI_ASSERT(abi_rect_batch_size, sizeof(app_gfx_rect_batch_t) == 8);
ABI_ASSERT(abi_rect_batch_count_offset, __builtin_offsetof(app_gfx_rect_batch_t, count) == 4);
ABI_ASSERT(abi_text_clipped_size, sizeof(app_gfx_text_clipped_t) == 40);
ABI_ASSERT(abi_text_clipped_text_offset, __builtin_offsetof(app_gfx_text_clipped_t, text) == 8);
ABI_ASSERT(abi_text_clipped_flags_offset, __builtin_offsetof(app_gfx_text_clipped_t, flags) == 36);
ABI_ASSERT(abi_surface_blit_size, sizeof(app_gfx_surface_blit_t) == 52);
ABI_ASSERT(abi_surface_blit_format_offset, __builtin_offsetof(app_gfx_surface_blit_t, format) == 16);
ABI_ASSERT(abi_surface_blit_dest_h_offset, __builtin_offsetof(app_gfx_surface_blit_t, dest_h) == 48);
ABI_ASSERT(abi_rect_batch_syscall_id, MINIDOS_SYSCALL_GFX_RECT_BATCH == 35);
ABI_ASSERT(abi_text_clipped_syscall_id, MINIDOS_SYSCALL_GFX_TEXT_CLIPPED == 36);
ABI_ASSERT(abi_legacy_surface_format, APP_SURFACE_FORMAT_XRGB8888 == 0);
ABI_ASSERT(abi_packed_xrgb_surface_format, APP_SURFACE_FORMAT_XRGB8888_LE == 1);
ABI_ASSERT(abi_packed_argb_surface_format, APP_SURFACE_FORMAT_ARGB8888_LE == 2);
ABI_ASSERT(abi_transparent_text_flag, APP_GFX_TEXT_TRANSPARENT_BG == 1);
ABI_ASSERT(abi_rect_batch_capacity, APP_GFX_RECT_BATCH_MAX == 64);
"""


def main():
    root = repo_root()
    runtime_dir = os.path.join(root, "external_apps", "runtime")

    with tempfile.TemporaryDirectory(prefix="minidos-ui-runtime-") as tmpdir:
        source_path = os.path.join(tmpdir, "ui_runtime_test.c")
        abi_source_path = os.path.join(tmpdir, "ui_runtime_abi_test.c")
        binary_path = os.path.join(tmpdir, "ui_runtime_test")

        with open(source_path, "w", encoding="ascii") as f:
            f.write(textwrap.dedent(TEST_SOURCE))
        with open(abi_source_path, "w", encoding="ascii") as f:
            f.write(textwrap.dedent(ABI_TEST_SOURCE))

        abi_compile_cmd = [
            "gcc",
            "-m32",
            "-Wall",
            "-Wextra",
            "-std=c99",
            "-ffreestanding",
            "-fsyntax-only",
            f"-I{runtime_dir}",
            abi_source_path,
        ]
        subprocess.run(abi_compile_cmd, cwd=root, check=True)

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
