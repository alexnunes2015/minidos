#ifndef MINIDOS_UI_RECT_H
#define MINIDOS_UI_RECT_H

#include "ui_defs.h"

static inline ui_rect_t ui_rect_make(int x, int y, int w, int h) {
    ui_rect_t rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    return rect;
}

static inline ui_rect_t ui_rect_inset(ui_rect_t rect, int amount) {
    rect.x += amount;
    rect.y += amount;
    rect.w -= amount * 2;
    rect.h -= amount * 2;
    if (rect.w < 0) {
        rect.w = 0;
    }
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static inline int ui_rect_is_empty(ui_rect_t rect) {
    return rect.w <= 0 || rect.h <= 0;
}

static inline int ui_rect_contains(const ui_rect_t* rect, int px, int py) {
    if (!rect) {
        return 0;
    }
    return px >= rect->x && py >= rect->y
        && px < (rect->x + rect->w)
        && py < (rect->y + rect->h);
}

static inline int ui_rect_contains_rect(const ui_rect_t* outer, const ui_rect_t* inner) {
    if (!outer || !inner || ui_rect_is_empty(*inner)) {
        return 0;
    }
    return inner->x >= outer->x
        && inner->y >= outer->y
        && (inner->x + inner->w) <= (outer->x + outer->w)
        && (inner->y + inner->h) <= (outer->y + outer->h);
}

static inline int ui_rects_intersect(ui_rect_t a, ui_rect_t b) {
    if (ui_rect_is_empty(a) || ui_rect_is_empty(b)) {
        return 0;
    }
    return a.x < (b.x + b.w) && (a.x + a.w) > b.x
        && a.y < (b.y + b.h) && (a.y + a.h) > b.y;
}

static inline ui_rect_t ui_rect_intersect(ui_rect_t a, ui_rect_t b) {
    int x = (a.x > b.x) ? a.x : b.x;
    int y = (a.y > b.y) ? a.y : b.y;
    int x2 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    int y2 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
    if (x2 <= x || y2 <= y) {
        return ui_rect_make(0, 0, 0, 0);
    }
    return ui_rect_make(x, y, x2 - x, y2 - y);
}

static inline ui_rect_t ui_rect_union(ui_rect_t a, ui_rect_t b) {
    int right;
    int bottom;
    ui_rect_t out;

    if (ui_rect_is_empty(a)) {
        return b;
    }
    if (ui_rect_is_empty(b)) {
        return a;
    }

    out.x = (a.x < b.x) ? a.x : b.x;
    out.y = (a.y < b.y) ? a.y : b.y;
    right = ((a.x + a.w) > (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    bottom = ((a.y + a.h) > (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
    out.w = right - out.x;
    out.h = bottom - out.y;
    return out;
}

static inline int ui_rect_subtract(ui_rect_t rect, ui_rect_t cutout, ui_rect_t* out_rects, int max_rects) {
    ui_rect_t overlap;
    int count = 0;
    int rect_right;
    int rect_bottom;
    int overlap_right;
    int overlap_bottom;

    if (!out_rects || max_rects <= 0 || ui_rect_is_empty(rect)) {
        return 0;
    }

    overlap = ui_rect_intersect(rect, cutout);
    if (ui_rect_is_empty(overlap)) {
        out_rects[0] = rect;
        return 1;
    }

    rect_right = rect.x + rect.w;
    rect_bottom = rect.y + rect.h;
    overlap_right = overlap.x + overlap.w;
    overlap_bottom = overlap.y + overlap.h;

    if (overlap.y > rect.y && count < max_rects) {
        out_rects[count++] = ui_rect_make(rect.x, rect.y, rect.w, overlap.y - rect.y);
    }
    if (overlap_bottom < rect_bottom && count < max_rects) {
        out_rects[count++] = ui_rect_make(rect.x, overlap_bottom, rect.w, rect_bottom - overlap_bottom);
    }
    if (overlap.x > rect.x && count < max_rects) {
        out_rects[count++] = ui_rect_make(rect.x, overlap.y, overlap.x - rect.x, overlap.h);
    }
    if (overlap_right < rect_right && count < max_rects) {
        out_rects[count++] = ui_rect_make(overlap_right, overlap.y, rect_right - overlap_right, overlap.h);
    }

    return count;
}

static inline void ui_dirty_list_init(ui_dirty_list_t* list) {
    if (!list) {
        return;
    }
    list->count = 0;
}

static inline int ui_dirty_list_add(ui_dirty_list_t* list, ui_rect_t rect) {
    int i;

    if (!list || ui_rect_is_empty(rect)) {
        return 0;
    }

    for (i = 0; i < list->count; i++) {
        if (ui_rect_contains_rect(&list->rects[i], &rect)) {
            return 1;
        }
        if (ui_rect_contains_rect(&rect, &list->rects[i])) {
            list->rects[i] = rect;
            return 1;
        }
    }

    if (list->count < UI_DIRTY_RECTS_MAX) {
        list->rects[list->count++] = rect;
        return 1;
    }

    list->rects[list->count - 1] = ui_rect_union(list->rects[list->count - 1], rect);
    return 1;
}

static inline int ui_dirty_list_add_clipped(ui_dirty_list_t* list, ui_rect_t rect, ui_rect_t clip) {
    return ui_dirty_list_add(list, ui_rect_intersect(rect, clip));
}

#endif
