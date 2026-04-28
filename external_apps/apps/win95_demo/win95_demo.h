#ifndef WIN95_DEMO_H
#define WIN95_DEMO_H

#include "minidos_ui.h"

#define KEY_TAB   9
#define KEY_BACKSPACE 8
#define KEY_ENTER 13
#define KEY_ESC   27
#define KEY_SPACE 32
#define KEY_UP    0x11
#define KEY_DOWN  0x12
#define KEY_LEFT  0x15
#define KEY_RIGHT 0x16
#define TASKBAR_H 28
/* User-supplied wallpaper BMP (optional; loaded once at startup) */
#define RESOURCE_HOME_DIR_1 "AIOS"
#define WALLPAPER_BMP_PATH "BG.BMP"
#define QUICKLAUNCH_W 64
#define TASKBAR_TRAY_W 120
#define TASKBAR_TRAY_ICON_W 24
#define TASKBAR_TRAY_GAP 4
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
#define DOUBLE_CLICK_TICKS 30
#define EXPLORER_MAX_DEPTH 12
#define EXPLORER_TITLE_LEN 32
#define EXPLORER_PATH_LEN 96
#define EXPLORER_STATUS_LEN 96
#define EXPLORER_MENU_H 18
#define EXPLORER_TOOLBAR_H 28
#define EXPLORER_ADDR_H 22
#define EXPLORER_SIDEBAR_W 140
#define EXPLORER_GRID_CELL_W 76
#define EXPLORER_GRID_CELL_H 64
#define EXPLORER_GRID_ICON_W 34
#define EXPLORER_GRID_ICON_H 30
#define EXPLORER_MAX_WINDOWS (UI_WM_MAX_WINDOWS - 1)
#define TASK_BUTTON_MIN_W 24
#define TASK_BUTTON_MAX_W 170
#define WINDOW_RESIZE_MARGIN 5
#define WINDOW_MIN_W 220
#define WINDOW_MIN_H 150
#define RESIZE_EDGE_LEFT 1
#define RESIZE_EDGE_RIGHT 2
#define RESIZE_EDGE_TOP 4
#define RESIZE_EDGE_BOTTOM 8

enum {
    START_MENU_ITEM_PROGRAMAS = 0,
    START_MENU_ITEM_DOCUMENTOS = 1,
    START_MENU_ITEM_DEFINICOES = 2,
    START_MENU_ITEM_AJUDA = 3,
    START_MENU_ITEM_BACK_TO_DOS = 4,
};

typedef struct {
    char name[13];
    int is_dir;
    ui_rect_t bounds;
} desktop_item_t;

typedef struct {
    int window_id;
    int path_label_id;
    int path_value_id;
    int status_label_id;
    int status_count_id;
    int status_extra_id;
    int up_button_id;
    int open_button_id;
    int listview_id;
    int visible;
    int showing_drives;
    int drive;
    int depth;
    int pressed_index;
    int last_click_index;
    unsigned int last_click_ticks;
    char dirs[EXPLORER_MAX_DEPTH][13];
    char title[EXPLORER_TITLE_LEN];
    char path_text[EXPLORER_PATH_LEN];
    char status_text[EXPLORER_STATUS_LEN];
    char status_count_text[32];
    char status_extra_text[32];
    ui_listview_t listview;
} explorer_state_t;

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
    int resizing;
    int dragging_window_id;
    int resizing_window_id;
    int resize_edges;
    int resize_hover_window_id;
    int resize_hover_edges;
    int mouse_pressed_control_id;
    int title_button_pressed_window_id;
    int title_button_pressed_action;
    int last_title_click_window_id;
    unsigned int last_title_click_ticks;
    int taskbar_pressed_window_id;
    int drag_offset_x;
    int drag_offset_y;
    int resize_start_mouse_x;
    int resize_start_mouse_y;
    ui_rect_t resize_start_bounds;
    ui_rect_t drag_preview_bounds;
    int layout_version;
    int start_menu_open;
    int start_button_pressed;
    int start_menu_pressed_item;
    int start_menu_hot_item;
    int selected_desktop_item;
    int last_desktop_click_index;
    unsigned int last_desktop_click_ticks;
    int desktop_item_count;
    desktop_item_t desktop_items[DESKTOP_MAX_ITEMS];
    int active_explorer_index;
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
int open_desktop_folder(const minidos_app_api_t* api, demo_state_t* state, int item_index);

void explorer_init(explorer_state_t* explorer);
void explorer_init_all(demo_state_t* state);
int explorer_is_visible(const demo_state_t* state);
ui_rect_t explorer_window_rect(const demo_state_t* state);
explorer_state_t* explorer_active(demo_state_t* state);
const explorer_state_t* explorer_active_const(const demo_state_t* state);
explorer_state_t* explorer_for_window(demo_state_t* state, int window_id);
explorer_state_t* explorer_for_control(demo_state_t* state, int control_id);
int explorer_hit_test_item(const demo_state_t* state, int x, int y);
int explorer_hit_test_item_in(demo_state_t* state, explorer_state_t* explorer, int x, int y);
void explorer_select_item(demo_state_t* state, int item_index);
void explorer_select_item_in(demo_state_t* state, explorer_state_t* explorer, int item_index);
int explorer_open_desktop_folder(const minidos_app_api_t* api, demo_state_t* state, const char* folder_name);
int explorer_open_selected(const minidos_app_api_t* api, demo_state_t* state);
int explorer_open_selected_in(const minidos_app_api_t* api, demo_state_t* state, explorer_state_t* explorer);
int explorer_go_up(const minidos_app_api_t* api, demo_state_t* state);
int explorer_go_up_in(const minidos_app_api_t* api, demo_state_t* state, explorer_state_t* explorer);
void explorer_close(demo_state_t* state);
void explorer_close_window(demo_state_t* state, int window_id);
void explorer_move_selection(demo_state_t* state, int delta);
void explorer_move_selection_in(demo_state_t* state, explorer_state_t* explorer, int delta);
void explorer_relayout_window(demo_state_t* state, int window_id);
void draw_explorer_grid(const minidos_app_api_t* api, const demo_state_t* state,
    const explorer_state_t* explorer, ui_rect_t bounds, ui_rect_t clip);

ui_rect_t taskbar_rect(const demo_state_t* state);
ui_rect_t start_button_rect(const demo_state_t* state);
ui_rect_t taskbar_clock_rect(const demo_state_t* state);
ui_rect_t taskbar_button_rect(const demo_state_t* state, int window_id);
int taskbar_button_hit_test(const demo_state_t* state, int x, int y);
ui_rect_t window_desktop_bounds(const demo_state_t* state);
int main_window_is_visible(const demo_state_t* state);
ui_rect_t cursor_rect_at(int x, int y);
ui_rect_t current_window_rect(const demo_state_t* state);
ui_rect_t current_title_bar_rect(const demo_state_t* state);
int cursor_touches_title_bar(const demo_state_t* state, ui_rect_t previous_cursor_rect, ui_rect_t current_cursor_rect);
int cursor_crosses_window_chrome(const demo_state_t* state, ui_rect_t previous_cursor_rect, ui_rect_t current_cursor_rect);
ui_rect_t current_drag_preview_rect(const demo_state_t* state);
int window_resize_hit_test(const demo_state_t* state, int window_id, int x, int y);
void add_window_damage_for_cursor(ui_dirty_list_t* dirty, const demo_state_t* state,
    ui_rect_t previous_cursor_rect, ui_rect_t current_cursor_rect);

ui_rect_t start_menu_rect(const demo_state_t* state);
ui_rect_t start_menu_item_rect(const demo_state_t* state, int item_index);
ui_rect_t start_menu_exit_rect(const demo_state_t* state);
void dismiss_start_menu(demo_state_t* state);
void open_start_menu(demo_state_t* state);
void close_start_menu(demo_state_t* state, const char* reason);
int start_menu_hit_test(const demo_state_t* state, int x, int y);
int handle_start_menu_action(demo_state_t* state, int action_id);
void clamp_rect_to_desktop(const demo_state_t* state, ui_rect_t* rect);

void draw_drag_outline(const minidos_app_api_t* api, const demo_state_t* state);
void draw_resize_hint(const minidos_app_api_t* api, const demo_state_t* state);
void draw_start_menu(const minidos_app_api_t* api, const demo_state_t* state);
void draw_taskbar_overlay(const minidos_app_api_t* api, const demo_state_t* state);

void redraw_region(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t rect);
void render_clock_update(const minidos_app_api_t* api, const demo_state_t* state);
void render_partial_motion(const minidos_app_api_t* api, const demo_state_t* state,
    const app_mouse_state_t* previous_mouse);
void render_partial_drag(const minidos_app_api_t* api, const demo_state_t* state,
    ui_rect_t previous_drag_rect, const app_mouse_state_t* previous_mouse);
void render(const minidos_app_api_t* api, const demo_state_t* state);

int handle_keyboard(const minidos_app_api_t* api, demo_state_t* state, char c);
int handle_mouse(const minidos_app_api_t* api, demo_state_t* state, const app_mouse_state_t* previous_mouse);
void init_demo(demo_state_t* state, const minidos_app_api_t* api);

#endif
