#ifndef MINIDOS_UI_DEFS_H
#define MINIDOS_UI_DEFS_H

#include "../minidos_app.h"

#define UI_CHAR_W 8
#define UI_CHAR_H 8
#define UI_DIRTY_RECTS_MAX 8
#define UI_WM_MAX_WINDOWS 8
#define UI_WM_MAX_CONTROLS 64
#define UI_BITMAP_MAX_FILE_SIZE (256 * 1024)
#define UI_BITMAP_TRANSPARENT_COLOR 0xFF00FFu
#define UI_BITMAP_PATH_MAX 128
/* Max decoded XRGB8888 pixels for wallpaper surface (covers 24bpp source up to 256KB) */
#define UI_WALLPAPER_SURFACE_MAX_BYTES (348160)

typedef struct {
    char path[UI_BITMAP_PATH_MAX];
    int valid;
    int src_w;
    int src_h;
    int abs_src_h;
    int top_down;
    unsigned int bit_count;
    unsigned int row_stride;
    unsigned int data_offset;
    unsigned int bytes_per_pixel;
    unsigned char data[UI_BITMAP_MAX_FILE_SIZE];
} ui_bitmap_cache_t;

/* Decoded XRGB8888 wallpaper surface, populated once from the BMP file cache */
typedef struct {
    char path[UI_BITMAP_PATH_MAX];
    unsigned char pixels[UI_WALLPAPER_SURFACE_MAX_BYTES];
    int width;
    int height;
    int stride;   /* bytes per row = width * 4 */
    int valid;
} ui_wallpaper_surface_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ui_rect_t;

typedef struct {
    ui_rect_t rects[UI_DIRTY_RECTS_MAX];
    int count;
} ui_dirty_list_t;

typedef struct {
    unsigned int desktop_bg;
    unsigned int desktop_accent;
    unsigned int face;
    unsigned int face_alt;
    unsigned int light;
    unsigned int shadow;
    unsigned int dark_shadow;
    unsigned int text;
    unsigned int text_disabled;
    unsigned int title_active_bg;
    unsigned int title_active_text;
    unsigned int title_inactive_bg;
    unsigned int title_inactive_text;
    unsigned int field_bg;
    unsigned int field_text;
    unsigned int selection_bg;
    unsigned int selection_text;
    const char* desktop_bg_bitmap;
} ui_theme_t;

typedef struct {
    ui_rect_t bounds;
    const char* title;
    int active;
    int has_close_button;
} ui_window_t;

typedef struct {
    ui_rect_t bounds;
    const char* label;
    int pressed;
    int focused;
    int enabled;
} ui_button_t;

enum {
    UI_CONTROL_LABEL = 1,
    UI_CONTROL_BUTTON = 2,
    UI_CONTROL_TEXTINPUT = 3,
    UI_CONTROL_LISTVIEW = 4,
};

#define UI_LISTVIEW_MAX_ITEMS 128
#define UI_LISTVIEW_ITEM_NAME_MAX 32
#define UI_LISTVIEW_ROW_H 16
/* Max line buffer width for batch surface blit (pixels * 4 bytes XRGB8888) */
#define UI_LISTVIEW_LINE_MAX_W 640
#define UI_LISTVIEW_LINE_BUF_SIZE (UI_LISTVIEW_LINE_MAX_W * UI_LISTVIEW_ROW_H * 4)

typedef struct {
    char name[UI_LISTVIEW_ITEM_NAME_MAX];
    int is_dir;
    int icon_color;  /* 0 for default */
} ui_listview_item_t;

typedef struct {
    ui_listview_item_t items[UI_LISTVIEW_MAX_ITEMS];
    int item_count;
    int scroll_offset;   /* first visible item index */
    int selected_index;  /* currently selected item (-1 for none) */
    int visible_count;   /* how many items fit in the viewport */
    int prev_scroll_offset; /* previous scroll for delta optimization */
} ui_listview_t;

/* Pixel buffer for rendering one row via surface blit */
typedef struct {
    unsigned char pixels[UI_LISTVIEW_LINE_BUF_SIZE];
} ui_listview_line_buf_t;

typedef struct {
    int id;
    int type;
    int window_id;
    ui_rect_t bounds;
    const char* text;
    char* text_buffer;
    int text_buffer_len;
    int text_len;
    int visible;
    int enabled;
    int focused;
    int pressed;
    ui_listview_t* listview;  /* non-NULL for UI_CONTROL_LISTVIEW */
} ui_control_t;

typedef struct {
    int id;
    ui_window_t window;
    int visible;
    int z_order;
} ui_wm_window_t;

typedef struct {
    ui_theme_t theme;
    ui_wm_window_t windows[UI_WM_MAX_WINDOWS];
    ui_control_t controls[UI_WM_MAX_CONTROLS];
    int window_count;
    int control_count;
    int next_window_id;
    int next_control_id;
    int active_window_id;
    int focused_control_id;
    int pressed_window_id;
    int pressed_control_id;
    int pressed_hit_close;
} ui_window_manager_t;

static inline int ui_strlen(const char* s) {
    int n = 0;
    while (s && s[n] != '\0') {
        n++;
    }
    return n;
}

#endif
