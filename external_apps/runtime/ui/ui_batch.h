#ifndef MINIDOS_UI_BATCH_H
#define MINIDOS_UI_BATCH_H

#include "ui_defs.h"

typedef struct {
    app_gfx_rect_t rects[APP_GFX_RECT_BATCH_MAX];
    unsigned int count;
    const minidos_app_api_t* api;
} ui_gfx_rect_batch_state_t;

#ifdef MINIDOS_UI_IMPLEMENTATION
ui_gfx_rect_batch_state_t g_ui_gfx_rect_batch;
#else
extern ui_gfx_rect_batch_state_t g_ui_gfx_rect_batch;
#endif

static inline int ui_gfx_rect_batch_flush(const minidos_app_api_t* api) {
    app_gfx_rect_batch_t batch;
    const minidos_app_api_t* target_api;
    unsigned int count;
    unsigned int i;
    int result;

    if (g_ui_gfx_rect_batch.count == 0U) {
        return 1;
    }

    target_api = g_ui_gfx_rect_batch.api ? g_ui_gfx_rect_batch.api : api;
    count = g_ui_gfx_rect_batch.count;
    batch.rects = g_ui_gfx_rect_batch.rects;
    batch.count = count;
    g_ui_gfx_rect_batch.count = 0U;
    g_ui_gfx_rect_batch.api = 0;

    result = app_gfx_rect_batch(target_api, &batch);
    if (result > 0) {
        return 1;
    }

    result = 1;
    for (i = 0; i < count; i++) {
        if (app_gfx_rect(target_api, &g_ui_gfx_rect_batch.rects[i]) <= 0) {
            result = 0;
        }
    }
    return result > 0;
}

static inline int ui_gfx_rect_batch_enqueue(const minidos_app_api_t* api,
    const app_gfx_rect_t* rect) {
    if (!api || !rect) {
        return 0;
    }
    if (g_ui_gfx_rect_batch.count > 0U && g_ui_gfx_rect_batch.api != api) {
        (void)ui_gfx_rect_batch_flush(g_ui_gfx_rect_batch.api);
    }
    if (g_ui_gfx_rect_batch.count == APP_GFX_RECT_BATCH_MAX) {
        (void)ui_gfx_rect_batch_flush(api);
    }

    g_ui_gfx_rect_batch.api = api;
    g_ui_gfx_rect_batch.rects[g_ui_gfx_rect_batch.count++] = *rect;
    if (g_ui_gfx_rect_batch.count == APP_GFX_RECT_BATCH_MAX) {
        return ui_gfx_rect_batch_flush(api);
    }
    return 1;
}

static inline void ui_gfx_rect_batch_reset(void) {
    g_ui_gfx_rect_batch.count = 0U;
    g_ui_gfx_rect_batch.api = 0;
}

#endif
