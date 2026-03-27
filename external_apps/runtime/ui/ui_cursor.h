#ifndef MINIDOS_UI_CURSOR_H
#define MINIDOS_UI_CURSOR_H

#include "ui_draw.h"
#include "../minidos_cursor_bitmap.h"

static inline void ui_draw_cursor(const minidos_app_api_t* api, int x, int y, unsigned int fill, unsigned int outline) {
    int row;
    int draw_x = x - UI_CURSOR_HOTSPOT_X;
    int draw_y = y - UI_CURSOR_HOTSPOT_Y;

    for (row = 0; row < UI_CURSOR_BITMAP_HEIGHT; row++) {
        int col = 0;

        while (col < UI_CURSOR_BITMAP_WIDTH) {
            unsigned char pixel = ui_cursor_bitmap[(row * UI_CURSOR_BITMAP_WIDTH) + col];
            int start;
            unsigned int color;

            if (pixel == UI_CURSOR_PIXEL_TRANSPARENT) {
                col++;
                continue;
            }

            start = col;
            color = (pixel == UI_CURSOR_PIXEL_OUTLINE) ? outline : fill;
            while (col < UI_CURSOR_BITMAP_WIDTH
                && ui_cursor_bitmap[(row * UI_CURSOR_BITMAP_WIDTH) + col] == pixel) {
                col++;
            }
            ui_fill_rect(api, ui_rect_make(draw_x + start, draw_y + row, col - start, 1), color);
        }
    }
}

#endif
