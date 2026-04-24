#ifndef WIN95_DEMO_H
#define WIN95_DEMO_H

#include "minidos_ui.h"

#define KEY_TAB   9
#define KEY_ENTER 13
#define KEY_ESC   27
#define KEY_SPACE 32
#define TASKBAR_H 28
/* User-supplied wallpaper BMP (optional; loaded once at startup) */
#define RESOURCE_HOME_DIR_1 "AIOS"
#define WALLPAPER_BMP_PATH "BG.BMP"
#define START_BUTTON_W 86
#define START_BUTTON_H 20
#define START_MENU_W 220
#define START_MENU_H 188
#define START_MENU_STRIP_W 28
#define START_MENU_ITEM_NONE (-1)
#define START_MENU_ITEM_COUNT 4
#define DESKTOP_MAX_ITEMS 16
#define DESKTOP_CELL_W 96
#define DESKTOP_CELL_H 70
#define DESKTOP_ICON_W 42
#define DESKTOP_ICON_H 32
#define CLOCK_TEXT_LEN 9
#define CLOCK_REFRESH_MS 1000

typedef struct {
    char name[13];
    int is_dir;
    ui_rect_t bounds;
} desktop_item_t;

typedef struct {
    ui_window_manager_t wm;
    app_mouse_state_t mouse;
    int sw;
    int sh;
    int window_id;
    int label_mouse_id;
    int label_status_id;
    int button_ok_id;
    int button_cancel_id;
    int input_id;
    int dragging;
    int mouse_pressed_control_id;
    int drag_offset_x;
    int drag_offset_y;
    ui_rect_t drag_preview_bounds;
    int start_menu_open;
    int start_button_pressed;
    int start_menu_pressed_item;
    int start_menu_hot_item;
    int selected_desktop_item;
    int desktop_item_count;
    desktop_item_t desktop_items[DESKTOP_MAX_ITEMS];
    char mouse_text[48];
    char status_text[96];
    char input_text[64];
    char clock_text[CLOCK_TEXT_LEN];
} demo_state_t;

void str_copy(char* dst, const char* src, int max_len);
int str_equal(const char* a, const char* b);
int rect_equal(ui_rect_t a, ui_rect_t b);
void enter_resource_home(const minidos_app_api_t* api);
void update_mouse_label_text(demo_state_t* state);
void update_status_text(demo_state_t* state, const char* text);
void append_two_digits(char* out, unsigned int value);
void update_clock_text(demo_state_t* state, const minidos_app_api_t* api);
void enter_desktop_home(const minidos_app_api_t* api);
void draw_text_transparent_clipped(const minidos_app_api_t* api, int x, int y, const char* text,
    unsigned int fg, ui_rect_t clip);
void draw_win95_icon_clipped(const minidos_app_api_t* api, ui_rect_t rect, int icon_id, ui_rect_t clip);

void load_desktop_items(const minidos_app_api_t* api, demo_state_t* state);
void draw_desktop_items(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t clip);
int desktop_item_hit_test(const demo_state_t* state, int x, int y);

#endif
