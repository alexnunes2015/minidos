#ifndef MINIDOS_CURSOR_BITMAP_H
#define MINIDOS_CURSOR_BITMAP_H

/* Transparent key for BMP fallback assets: #FF0000 */

#define UI_CURSOR_BITMAP_WIDTH 14
#define UI_CURSOR_BITMAP_HEIGHT 23
#define UI_CURSOR_HOTSPOT_X 0
#define UI_CURSOR_HOTSPOT_Y 0

enum {
    UI_CURSOR_PIXEL_TRANSPARENT = 0,
    UI_CURSOR_PIXEL_OUTLINE = 1,
    UI_CURSOR_PIXEL_FILL = 2,
};

static const unsigned char ui_cursor_bitmap[UI_CURSOR_BITMAP_WIDTH * UI_CURSOR_BITMAP_HEIGHT] = {
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0,
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0,
    1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
    1, 2, 2, 1, 1, 2, 2, 2, 1, 0, 0, 0, 0, 0,
    1, 2, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
};

#endif
