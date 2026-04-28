#include "win95_demo.h"
#include "win95_icons.h"

static explorer_state_t g_explorer_slots[EXPLORER_MAX_WINDOWS];

static int explorer_grid_columns_for_width(int width) {
    int columns = width / EXPLORER_GRID_CELL_W;
    if (columns < 1) {
        columns = 1;
    }
    return columns;
}

static int explorer_grid_visible_count_for_bounds(ui_rect_t bounds) {
    int columns = explorer_grid_columns_for_width(bounds.w);
    int rows = bounds.h / EXPLORER_GRID_CELL_H;
    if (rows < 1) {
        rows = 1;
    }
    return columns * rows;
}

explorer_state_t* explorer_active(demo_state_t* state) {
    if (!state || state->active_explorer_index < 0 || state->active_explorer_index >= EXPLORER_MAX_WINDOWS) {
        return 0;
    }
    return &g_explorer_slots[state->active_explorer_index];
}

const explorer_state_t* explorer_active_const(const demo_state_t* state) {
    if (!state || state->active_explorer_index < 0 || state->active_explorer_index >= EXPLORER_MAX_WINDOWS) {
        return 0;
    }
    return &g_explorer_slots[state->active_explorer_index];
}

static void explorer_set_active(demo_state_t* state, explorer_state_t* explorer) {
    int i;

    if (!state || !explorer) {
        return;
    }
    for (i = 0; i < EXPLORER_MAX_WINDOWS; i++) {
        if (&g_explorer_slots[i] == explorer) {
            state->active_explorer_index = i;
            return;
        }
    }
}

static int explorer_slot_index(const explorer_state_t* explorer) {
    int i;

    if (!explorer) {
        return 0;
    }
    for (i = 0; i < EXPLORER_MAX_WINDOWS; i++) {
        if (&g_explorer_slots[i] == explorer) {
            return i;
        }
    }
    return 0;
}

explorer_state_t* explorer_for_window(demo_state_t* state, int window_id) {
    int i;

    if (!state || window_id == 0) {
        return 0;
    }
    for (i = 0; i < EXPLORER_MAX_WINDOWS; i++) {
        if (g_explorer_slots[i].window_id == window_id) {
            return &g_explorer_slots[i];
        }
    }
    return 0;
}

explorer_state_t* explorer_for_control(demo_state_t* state, int control_id) {
    int i;

    if (!state || control_id == 0) {
        return 0;
    }
    for (i = 0; i < EXPLORER_MAX_WINDOWS; i++) {
        explorer_state_t* explorer = &g_explorer_slots[i];
        if (explorer->up_button_id == control_id
            || explorer->open_button_id == control_id
            || explorer->listview_id == control_id
            || explorer->path_label_id == control_id
            || explorer->path_value_id == control_id
            || explorer->status_label_id == control_id
            || explorer->status_count_id == control_id
            || explorer->status_extra_id == control_id) {
            return explorer;
        }
    }
    return 0;
}

static int explorer_grid_columns_for_explorer(const demo_state_t* state, const explorer_state_t* explorer) {
    const ui_control_t* control;
    ui_rect_t abs_bounds;

    if (!state || !explorer || explorer->listview_id == 0) {
        return 1;
    }

    control = ui_wm_find_control_const(&state->wm, explorer->listview_id);
    if (!control) {
        return 1;
    }

    abs_bounds = ui_wm_control_abs_bounds(&state->wm, control);
    return explorer_grid_columns_for_width(abs_bounds.w);
}

static void explorer_update_grid_visible_count(demo_state_t* state, explorer_state_t* explorer) {
    ui_control_t* control;
    ui_rect_t abs_bounds;

    if (!state || !explorer || explorer->listview_id == 0) {
        return;
    }

    control = ui_wm_find_control(&state->wm, explorer->listview_id);
    if (!control || !control->listview) {
        return;
    }

    abs_bounds = ui_wm_control_abs_bounds(&state->wm, control);
    control->listview->visible_count = explorer_grid_visible_count_for_bounds(abs_bounds);
}

static void bump_layout(demo_state_t* state) {
    if (state) {
        state->layout_version++;
    }
}

static void explorer_set_status(explorer_state_t* explorer, const char* text) {
    if (!explorer) {
        return;
    }

    str_copy(explorer->status_text, text ? text : "", (int)sizeof(explorer->status_text));
}

static void append_path_part(char* out, int max_len, const char* text) {
    int pos = 0;

    if (!out || max_len <= 1 || !text) {
        return;
    }

    while (pos < (max_len - 1) && out[pos] != '\0') {
        pos++;
    }
    while (pos < (max_len - 1) && *text != '\0') {
        out[pos++] = *text++;
    }
    out[pos] = '\0';
}

static void write_uint(char* out, int out_len, unsigned int value) {
    char tmp[16];
    int tmp_len = 0;
    int pos = 0;

    if (!out || out_len <= 1) {
        return;
    }

    if (value == 0u) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }

    while (value > 0u && tmp_len < (int)sizeof(tmp)) {
        tmp[tmp_len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (pos < (out_len - 1) && tmp_len > 0) {
        out[pos++] = tmp[--tmp_len];
    }
    out[pos] = '\0';
}

static void explorer_set_count_text(explorer_state_t* explorer, int count, int drive_mode) {
    char number[16];
    const char* suffix;

    if (!explorer) {
        return;
    }

    if (drive_mode) {
        suffix = (count == 1) ? " drive" : " drives";
    } else {
        suffix = (count == 1) ? " item" : " itens";
    }

    explorer->status_count_text[0] = '\0';
    write_uint(number, (int)sizeof(number), (unsigned int)(count < 0 ? 0 : count));
    str_copy(explorer->status_count_text, number, (int)sizeof(explorer->status_count_text));
    append_path_part(explorer->status_count_text, (int)sizeof(explorer->status_count_text), suffix);
}

static void explorer_update_texts(explorer_state_t* explorer) {
    int i;

    if (!explorer) {
        return;
    }

    if (explorer->showing_drives) {
        str_copy(explorer->title, "Computer", (int)sizeof(explorer->title));
        str_copy(explorer->path_text, "Computer", (int)sizeof(explorer->path_text));
        return;
    }

    explorer->path_text[0] = (char)('A' + explorer->drive);
    explorer->path_text[1] = ':';
    explorer->path_text[2] = '\\';
    explorer->path_text[3] = '\0';
    str_copy(explorer->title, explorer->path_text, (int)sizeof(explorer->title));

    for (i = 0; i < explorer->depth; i++) {
        if (i > 0) {
            append_path_part(explorer->path_text, (int)sizeof(explorer->path_text), "\\");
        }
        append_path_part(explorer->path_text, (int)sizeof(explorer->path_text), explorer->dirs[i]);
    }

    if (explorer->depth > 0) {
        str_copy(explorer->title, explorer->dirs[explorer->depth - 1], (int)sizeof(explorer->title));
    }
}

static int enter_explorer_dir(const minidos_app_api_t* api, const explorer_state_t* explorer) {
    int i;

    if (!api || !explorer) {
        return 0;
    }

    {
        char drive_name[3];

        drive_name[0] = (char)('A' + explorer->drive);
        drive_name[1] = ':';
        drive_name[2] = '\0';
        if (!app_chdir(api, drive_name)) {
            return 0;
        }
    }
    if (!app_chdir(api, "\\")) {
        return 0;
    }
    for (i = 0; i < explorer->depth; i++) {
        if (!app_chdir(api, explorer->dirs[i])) {
            return 0;
        }
    }

    return 1;
}

static void explorer_refresh_list(const minidos_app_api_t* api, demo_state_t* state, explorer_state_t* explorer) {
    int i;
    int count = 0;
    int selected = -1;
    char selected_name[13];

    if (!api || !state || !explorer) {
        return;
    }

    selected_name[0] = '\0';

    if (explorer->showing_drives) {
        for (i = 0; i < 26 && count < UI_LISTVIEW_MAX_ITEMS; i++) {
            if (!app_drive_valid(api, (unsigned int)i)) {
                continue;
            }
            explorer->listview.items[count].name[0] = (char)('A' + i);
            explorer->listview.items[count].name[1] = ':';
            explorer->listview.items[count].name[2] = '\0';
            explorer->listview.items[count].is_dir = 1;
            explorer->listview.items[count].icon_color = 0;
            count++;
        }
        explorer->listview.item_count = count;
        explorer->listview.selected_index = count > 0 ? 0 : -1;
        explorer->listview.scroll_offset = 0;
        explorer->listview.prev_scroll_offset = 0;
        explorer_update_texts(explorer);
        explorer_set_status(explorer, count > 0 ? "Enter para abrir um drive." : "Nenhum drive encontrado.");
        explorer_set_count_text(explorer, count, 1);
        str_copy(explorer->status_extra_text, "Computer", (int)sizeof(explorer->status_extra_text));
        return;
    }

    if (explorer->listview.selected_index >= 0
        && explorer->listview.selected_index < explorer->listview.item_count) {
        str_copy(selected_name,
            explorer->listview.items[explorer->listview.selected_index].name,
            (int)sizeof(selected_name));
    }

    if (!enter_explorer_dir(api, explorer)) {
        explorer_set_status(explorer, "Falha ao abrir a pasta.");
        return;
    }

    for (i = 0; i < UI_LISTVIEW_MAX_ITEMS; i++) {
        char name[13];
        int is_dir = 0;

        if (!app_list_entry(api, (unsigned int)i, name, &is_dir)) {
            break;
        }
        if (name[0] == '\0'
            || (name[0] == '.' && name[1] == '\0')
            || (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
            continue;
        }

        str_copy(explorer->listview.items[count].name, name, UI_LISTVIEW_ITEM_NAME_MAX);
        explorer->listview.items[count].is_dir = is_dir;
        explorer->listview.items[count].icon_color = 0;
        if (str_equal(selected_name, name)) {
            selected = count;
        }
        count++;
        if (count >= UI_LISTVIEW_MAX_ITEMS) {
            break;
        }
    }

    explorer->listview.item_count = count;
    explorer_set_count_text(explorer, count, 0);
    if (count <= 0) {
        explorer->listview.selected_index = -1;
        explorer->listview.scroll_offset = 0;
    } else {
        if (selected < 0) {
            explorer->listview.selected_index = -1;
            explorer->listview.scroll_offset = 0;
        } else {
            if (selected >= count) {
                selected = count - 1;
            }
            explorer->listview.selected_index = selected;
            ui_listview_ensure_visible(&explorer->listview);
        }
    }
    explorer->listview.prev_scroll_offset = explorer->listview.scroll_offset;

    explorer_update_texts(explorer);
    if (count == 0) {
        explorer_set_status(explorer, "Pasta vazia.");
    } else {
        explorer_set_status(explorer, "Duplo clique ou Enter para abrir.");
    }
    str_copy(explorer->status_extra_text, explorer->path_text, (int)sizeof(explorer->status_extra_text));
}

static ui_rect_t explorer_default_bounds(const demo_state_t* state) {
    int w;
    int h;
    int x;
    int y;

    if (!state) {
        return ui_rect_make(80, 56, 430, 280);
    }

    w = state->sw - 120;
    if (w > 430) {
        w = 430;
    }
    if (w < 300) {
        w = 300;
    }

    h = state->sh - TASKBAR_H - 76;
    if (h > 280) {
        h = 280;
    }
    if (h < 180) {
        h = 180;
    }

    x = (state->sw - w) / 2;
    y = 56;
    if (y + h > state->sh - TASKBAR_H - 4) {
        y = (state->sh - TASKBAR_H - 4) - h;
    }
    if (y < 8) {
        y = 8;
    }

    return ui_rect_make(x, y, w, h);
}

void explorer_relayout_window(demo_state_t* state, int window_id) {
    explorer_state_t* explorer;
    const ui_wm_window_t* win;
    ui_control_t* control;
    ui_rect_t client;
    int client_w;
    int client_h;
    int field_w;
    int top_h;
    int list_x;
    int list_y;
    int list_w;
    int list_h;
    int status_y;

    if (!state || window_id == 0) {
        return;
    }

    explorer = explorer_for_window(state, window_id);
    win = ui_wm_find_window_const(&state->wm, window_id);
    if (!explorer || !win || !win->visible) {
        return;
    }

    client = ui_window_client_rect(&win->window);
    client_w = client.w;
    client_h = client.h;
    top_h = EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + EXPLORER_ADDR_H;
    status_y = client_h - 22;

    control = ui_wm_find_control(&state->wm, explorer->up_button_id);
    if (control) {
        control->bounds = ui_rect_make(client_w - 150, EXPLORER_MENU_H + 4, 64, 22);
    }
    control = ui_wm_find_control(&state->wm, explorer->open_button_id);
    if (control) {
        control->bounds = ui_rect_make(client_w - 78, EXPLORER_MENU_H + 4, 64, 22);
    }
    control = ui_wm_find_control(&state->wm, explorer->path_label_id);
    if (control) {
        control->bounds = ui_rect_make(12, EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + 6, 56, UI_CHAR_H);
    }
    field_w = client_w - 252;
    if (field_w < 80) {
        field_w = 80;
    }
    control = ui_wm_find_control(&state->wm, explorer->path_value_id);
    if (control) {
        control->bounds = ui_rect_make(74, EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + 4, field_w, 16);
    }
    control = ui_wm_find_control(&state->wm, explorer->status_label_id);
    if (control) {
        control->bounds = ui_rect_make(12, status_y, client_w - 268, 16);
    }
    control = ui_wm_find_control(&state->wm, explorer->status_count_id);
    if (control) {
        control->bounds = ui_rect_make(client_w - 252, status_y, 96, 16);
    }
    control = ui_wm_find_control(&state->wm, explorer->status_extra_id);
    if (control) {
        control->bounds = ui_rect_make(client_w - 152, status_y, 140, 16);
    }
    control = ui_wm_find_control(&state->wm, explorer->listview_id);
    if (control) {
        list_x = 12 + EXPLORER_SIDEBAR_W + 10;
        list_y = 6 + top_h;
        list_w = client_w - list_x - 12;
        list_h = status_y - list_y - 6;
        if (list_w < 40) {
            list_w = 40;
        }
        if (list_h < 40) {
            list_h = 40;
        }
        control->bounds = ui_rect_make(list_x, list_y, list_w, list_h);
    }
    explorer_update_grid_visible_count(state, explorer);
}

static explorer_state_t* explorer_alloc_window_slot(demo_state_t* state) {
    int i;

    if (!state) {
        return 0;
    }

    for (i = 0; i < EXPLORER_MAX_WINDOWS; i++) {
        explorer_state_t* explorer = &g_explorer_slots[i];
        const ui_wm_window_t* win = explorer->window_id
            ? ui_wm_find_window_const(&state->wm, explorer->window_id)
            : 0;

        if (explorer->window_id == 0 || !win || !win->visible) {
            explorer_init(explorer);
            explorer_set_active(state, explorer);
            return explorer;
        }
    }

    return 0;
}

static void explorer_ensure_window(demo_state_t* state, explorer_state_t* explorer) {
    ui_rect_t bounds;
    ui_wm_window_t* win;

    if (!state || !explorer) {
        return;
    }

    bounds = explorer_default_bounds(state);
    {
        int slot_offset = explorer_slot_index(explorer) * 18;
        bounds.x += slot_offset;
        bounds.y += slot_offset;
        if (bounds.x + bounds.w > state->sw - 8) {
            bounds.x = state->sw - bounds.w - 8;
        }
        if (bounds.y + bounds.h > state->sh - TASKBAR_H - 4) {
            bounds.y = state->sh - TASKBAR_H - bounds.h - 4;
        }
        if (bounds.x < 8) {
            bounds.x = 8;
        }
        if (bounds.y < 8) {
            bounds.y = 8;
        }
    }

    if (explorer->window_id == 0) {
        int client_w;
        int client_h;
        int field_w;
        int top_h;
        int list_x;
        int list_y;
        int list_w;
        int list_h;
        int status_y;

        explorer->window_id = ui_wm_create_window_ex(&state->wm, bounds, explorer->title, 1, 1, 1);
        ui_wm_set_window_icon(&state->wm, explorer->window_id, UI_WINDOW_ICON_FOLDER);
        client_w = bounds.w - 8;
        client_h = bounds.h - 26;
        ui_listview_init(&explorer->listview, client_h - 72);
        top_h = EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + EXPLORER_ADDR_H;
        status_y = client_h - 22;

        explorer->path_label_id = ui_wm_add_label(&state->wm, explorer->window_id,
            ui_rect_make(12, EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + 6, 56, UI_CHAR_H), "Endereco:");
        explorer->up_button_id = ui_wm_add_button(&state->wm, explorer->window_id,
            ui_rect_make(client_w - 150, EXPLORER_MENU_H + 4, 64, 22), "Subir");
        explorer->open_button_id = ui_wm_add_button(&state->wm, explorer->window_id,
            ui_rect_make(client_w - 78, EXPLORER_MENU_H + 4, 64, 22), "Abrir");
        explorer->status_label_id = ui_wm_add_label(&state->wm, explorer->window_id,
            ui_rect_make(12, status_y, client_w - 268, 16), explorer->status_text);
        explorer->status_count_id = ui_wm_add_label(&state->wm, explorer->window_id,
            ui_rect_make(client_w - 252, status_y, 96, 16), explorer->status_count_text);
        explorer->status_extra_id = ui_wm_add_label(&state->wm, explorer->window_id,
            ui_rect_make(client_w - 152, status_y, 140, 16), explorer->status_extra_text);
        field_w = client_w - 252;
        if (field_w < 80) {
            field_w = 80;
        }
        explorer->path_value_id = ui_wm_add_label(&state->wm, explorer->window_id,
            ui_rect_make(74, EXPLORER_MENU_H + EXPLORER_TOOLBAR_H + 4, field_w, 16), explorer->path_text);

        list_x = 12 + EXPLORER_SIDEBAR_W + 10;
        list_y = 6 + top_h;
        list_w = client_w - list_x - 12;
        list_h = status_y - list_y - 6;
        if (list_w < 40) { list_w = 40; }
        if (list_h < 40) { list_h = 40; }
        explorer->listview_id = ui_wm_add_listview(&state->wm, explorer->window_id,
            ui_rect_make(list_x, list_y, list_w, list_h), &explorer->listview);
        explorer_relayout_window(state, explorer->window_id);
    } else {
        win = ui_wm_find_window(&state->wm, explorer->window_id);
        if (win) {
            win->visible = 1;
            win->window.bounds = bounds;
            win->window.title = explorer->title;
            win->window.icon_id = UI_WINDOW_ICON_FOLDER;
            win->window.has_close_button = 1;
            win->window.has_minimize_button = 1;
            win->window.has_maximize_button = 1;
            win->window.minimized = 0;
        }
        explorer_relayout_window(state, explorer->window_id);
    }

    ui_wm_bring_to_front(&state->wm, explorer->window_id);
    ui_wm_set_focus_control(&state->wm, explorer->listview_id);
    explorer->visible = 1;
    explorer_set_active(state, explorer);
}

void explorer_init(explorer_state_t* explorer) {
    int i;

    if (!explorer) {
        return;
    }

    explorer->window_id = 0;
    explorer->path_label_id = 0;
    explorer->path_value_id = 0;
    explorer->status_label_id = 0;
    explorer->status_count_id = 0;
    explorer->status_extra_id = 0;
    explorer->up_button_id = 0;
    explorer->open_button_id = 0;
    explorer->listview_id = 0;
    explorer->visible = 0;
    explorer->showing_drives = 0;
    explorer->drive = 0;
    explorer->depth = 0;
    explorer->pressed_index = -1;
    explorer->last_click_index = -1;
    explorer->last_click_ticks = 0;
    explorer->title[0] = '\0';
    explorer->path_text[0] = '\0';
    explorer->status_text[0] = '\0';
    explorer->status_count_text[0] = '\0';
    explorer->status_extra_text[0] = '\0';
    for (i = 0; i < EXPLORER_MAX_DEPTH; i++) {
        explorer->dirs[i][0] = '\0';
    }
    ui_listview_init(&explorer->listview, 160);
    explorer->listview.selected_index = -1;
}

void explorer_init_all(demo_state_t* state) {
    int i;

    if (!state) {
        return;
    }

    for (i = 0; i < EXPLORER_MAX_WINDOWS; i++) {
        explorer_init(&g_explorer_slots[i]);
    }
    state->active_explorer_index = 0;
}

int explorer_is_visible(const demo_state_t* state) {
    const explorer_state_t* explorer;
    const ui_wm_window_t* win;

    explorer = explorer_active_const(state);
    if (!state || !explorer || explorer->window_id == 0) {
        return 0;
    }

    win = ui_wm_find_window_const(&state->wm, explorer->window_id);
    return win && win->visible;
}

ui_rect_t explorer_window_rect(const demo_state_t* state) {
    int i;
    ui_rect_t out = ui_rect_make(0, 0, 0, 0);

    if (!state) {
        return out;
    }

    for (i = 0; i < EXPLORER_MAX_WINDOWS; i++) {
        const explorer_state_t* explorer = &g_explorer_slots[i];
        const ui_wm_window_t* win;

        if (explorer->window_id == 0) {
            continue;
        }
        win = ui_wm_find_window_const(&state->wm, explorer->window_id);
        if (!win || !win->visible) {
            continue;
        }
        if (ui_rect_is_empty(out)) {
            out = win->window.bounds;
        } else {
            int x1 = out.x < win->window.bounds.x ? out.x : win->window.bounds.x;
            int y1 = out.y < win->window.bounds.y ? out.y : win->window.bounds.y;
            int x2 = (out.x + out.w) > (win->window.bounds.x + win->window.bounds.w)
                ? (out.x + out.w) : (win->window.bounds.x + win->window.bounds.w);
            int y2 = (out.y + out.h) > (win->window.bounds.y + win->window.bounds.h)
                ? (out.y + out.h) : (win->window.bounds.y + win->window.bounds.h);
            out = ui_rect_make(x1, y1, x2 - x1, y2 - y1);
        }
    }

    return out;
}

int explorer_hit_test_item_in(demo_state_t* state, explorer_state_t* explorer, int x, int y) {
    const ui_control_t* control;
    ui_rect_t abs_bounds;
    int row;
    int item_index;

    if (!state || !explorer || explorer->listview_id == 0) {
        return -1;
    }

    control = ui_wm_find_control_const(&state->wm, explorer->listview_id);
    if (!control || !control->listview) {
        return -1;
    }

    abs_bounds = ui_wm_control_abs_bounds(&state->wm, control);
    if (!ui_rect_contains(&abs_bounds, x, y)) {
        return -1;
    }

    row = (y - abs_bounds.y) / EXPLORER_GRID_CELL_H;
    item_index = control->listview->scroll_offset
        + (row * explorer_grid_columns_for_explorer(state, explorer))
        + ((x - abs_bounds.x) / EXPLORER_GRID_CELL_W);
    if (item_index < 0 || item_index >= control->listview->item_count) {
        return -1;
    }

    return item_index;
}

int explorer_hit_test_item(const demo_state_t* state, int x, int y) {
    return explorer_hit_test_item_in((demo_state_t*)state, explorer_active((demo_state_t*)state), x, y);
}

void explorer_select_item_in(demo_state_t* state, explorer_state_t* explorer, int item_index) {
    if (!state || !explorer || item_index < 0 || item_index >= explorer->listview.item_count) {
        return;
    }

    explorer_update_grid_visible_count(state, explorer);
    explorer->listview.selected_index = item_index;
    ui_listview_ensure_visible(&explorer->listview);
    explorer_set_active(state, explorer);
    bump_layout(state);
}

void explorer_select_item(demo_state_t* state, int item_index) {
    explorer_select_item_in(state, explorer_active(state), item_index);
}

void explorer_move_selection_in(demo_state_t* state, explorer_state_t* explorer, int delta) {
    int next;

    if (!state || !explorer || explorer->listview.item_count <= 0) {
        return;
    }

    explorer_update_grid_visible_count(state, explorer);
    next = explorer->listview.selected_index + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= explorer->listview.item_count) {
        next = explorer->listview.item_count - 1;
    }
    if (next != explorer->listview.selected_index) {
        explorer->listview.selected_index = next;
        ui_listview_ensure_visible(&explorer->listview);
        explorer_set_active(state, explorer);
        bump_layout(state);
    }
}

void explorer_move_selection(demo_state_t* state, int delta) {
    explorer_move_selection_in(state, explorer_active(state), delta);
}

int explorer_open_desktop_folder(const minidos_app_api_t* api, demo_state_t* state, const char* folder_name) {
    explorer_state_t* explorer;
    int i;

    if (!api || !state || !folder_name || folder_name[0] == '\0') {
        return 0;
    }

    explorer = explorer_alloc_window_slot(state);
    if (!explorer) {
        update_status_text(state, "Limite de janelas atingido.");
        return 0;
    }

    explorer->visible = 0;
    explorer->showing_drives = 0;
    explorer->drive = 0;
    explorer->depth = 0;
    explorer->pressed_index = -1;
    explorer->last_click_index = -1;
    explorer->last_click_ticks = 0;
    for (i = 0; i < EXPLORER_MAX_DEPTH; i++) {
        explorer->dirs[i][0] = '\0';
    }
    explorer->listview.item_count = 0;
    explorer->listview.scroll_offset = 0;
    explorer->listview.selected_index = -1;
    explorer->listview.prev_scroll_offset = 0;
    str_copy(explorer->dirs[0], "USER", (int)sizeof(explorer->dirs[0]));
    str_copy(explorer->dirs[1], "ADM", (int)sizeof(explorer->dirs[1]));
    str_copy(explorer->dirs[2], "Desktop", (int)sizeof(explorer->dirs[2]));
    str_copy(explorer->dirs[3], folder_name, (int)sizeof(explorer->dirs[3]));
    explorer->depth = 4;
    explorer_refresh_list(api, state, explorer);
    explorer_ensure_window(state, explorer);
    update_status_text(state, "Explorer aberto.");
    bump_layout(state);
    return 1;
}

int explorer_open_selected_in(const minidos_app_api_t* api, demo_state_t* state, explorer_state_t* explorer) {
    ui_listview_item_t* item;

    if (!api || !state || !explorer) {
        return 0;
    }

    explorer_set_active(state, explorer);
    if (explorer->listview.selected_index < 0
        || explorer->listview.selected_index >= explorer->listview.item_count) {
        return 0;
    }

    item = &explorer->listview.items[explorer->listview.selected_index];
    if (explorer->showing_drives) {
        if (item->name[0] < 'A' || item->name[0] > 'Z' || item->name[1] != ':') {
            return 0;
        }
        explorer->drive = item->name[0] - 'A';
        explorer->showing_drives = 0;
        explorer->depth = 0;
        explorer->last_click_index = -1;
        explorer_refresh_list(api, state, explorer);
        update_status_text(state, "Drive aberto no Explorer.");
        bump_layout(state);
        return 1;
    }

    if (!item->is_dir || explorer->depth >= EXPLORER_MAX_DEPTH) {
        explorer_set_status(explorer, item->is_dir ? "Profundidade maxima atingida." : "Abrir ficheiros vem a seguir.");
        bump_layout(state);
        return 0;
    }

    str_copy(explorer->dirs[explorer->depth], item->name, (int)sizeof(explorer->dirs[explorer->depth]));
    explorer->depth++;
    explorer->last_click_index = -1;
    explorer_refresh_list(api, state, explorer);
    update_status_text(state, "Pasta aberta no Explorer.");
    bump_layout(state);
    return 1;
}

int explorer_open_selected(const minidos_app_api_t* api, demo_state_t* state) {
    return explorer_open_selected_in(api, state, explorer_active(state));
}

int explorer_go_up_in(const minidos_app_api_t* api, demo_state_t* state, explorer_state_t* explorer) {
    if (!api || !state || !explorer) {
        return 0;
    }

    explorer_set_active(state, explorer);
    if (explorer->showing_drives) {
        explorer_set_status(explorer, "Ja esta em Computer.");
        bump_layout(state);
        return 0;
    }

    if (explorer->depth <= 0) {
        explorer->showing_drives = 1;
        explorer->last_click_index = -1;
        explorer_refresh_list(api, state, explorer);
        update_status_text(state, "A mostrar todos os drives.");
        bump_layout(state);
        return 1;
    }

    explorer->depth--;
    explorer->dirs[explorer->depth][0] = '\0';
    explorer->last_click_index = -1;
    explorer_refresh_list(api, state, explorer);
    update_status_text(state, "Voltou para a pasta anterior.");
    bump_layout(state);
    return 1;
}

int explorer_go_up(const minidos_app_api_t* api, demo_state_t* state) {
    return explorer_go_up_in(api, state, explorer_active(state));
}

void explorer_close_window(demo_state_t* state, int window_id) {
    explorer_state_t* explorer = explorer_for_window(state, window_id);

    if (!state || !explorer || explorer->window_id == 0) {
        return;
    }

    ui_wm_close_window(&state->wm, explorer->window_id);
    explorer->visible = 0;
    explorer->pressed_index = -1;
    explorer->last_click_index = -1;
    update_status_text(state, "Explorer fechado.");
    bump_layout(state);
}

void explorer_close(demo_state_t* state) {
    explorer_state_t* explorer = explorer_active(state);

    if (!explorer) {
        return;
    }
    explorer_close_window(state, explorer->window_id);
}

static int explorer_item_icon_id(const explorer_state_t* explorer, const ui_listview_item_t* item, int selected) {
    const char* name;
    const char* ext = 0;
    int i;

    if (!explorer || !item) {
        return WIN95_ICON_FILE_UNKNOWN;
    }

    name = item->name;
    if (explorer->showing_drives) {
        if (name[0] == 'A' && name[1] == ':') {
            return WIN95_ICON_FLOPPY;
        }
        return WIN95_ICON_HARD_DRIVE;
    }
    if (item->is_dir) {
        return selected ? WIN95_ICON_FOLDER_OPEN : WIN95_ICON_FOLDER_CLOSED;
    }

    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '.') {
            ext = &name[i + 1];
        }
    }
    if (!ext) {
        return WIN95_ICON_FILE_UNKNOWN;
    }
    if ((ext[0] == 'E' && ext[1] == 'L' && ext[2] == 'F' && ext[3] == '\0')
        || (ext[0] == 'C' && ext[1] == 'O' && ext[2] == 'M' && ext[3] == '\0')
        || (ext[0] == 'E' && ext[1] == 'X' && ext[2] == 'E' && ext[3] == '\0')) {
        return WIN95_ICON_APP;
    }
    if ((ext[0] == 'T' && ext[1] == 'X' && ext[2] == 'T' && ext[3] == '\0')
        || (ext[0] == 'M' && ext[1] == 'D' && ext[2] == '\0')) {
        return WIN95_ICON_FILE_TXT;
    }
    if ((ext[0] == 'W' && ext[1] == 'A' && ext[2] == 'V' && ext[3] == '\0')
        || (ext[0] == 'A' && ext[1] == 'U' && ext[2] == 'D' && ext[3] == '\0')) {
        return WIN95_ICON_FILE_WAV;
    }
    if ((ext[0] == 'B' && ext[1] == 'M' && ext[2] == 'P' && ext[3] == '\0')
        || (ext[0] == 'P' && ext[1] == 'N' && ext[2] == 'G' && ext[3] == '\0')
        || (ext[0] == 'J' && ext[1] == 'P' && ext[2] == 'G' && ext[3] == '\0')
        || (ext[0] == 'G' && ext[1] == 'I' && ext[2] == 'F' && ext[3] == '\0')) {
        return WIN95_ICON_FILE_IMAGE;
    }
    return WIN95_ICON_FILE_UNKNOWN;
}

static void explorer_item_display_lines(const ui_listview_item_t* item, char line1[9], char line2[9]) {
    int i;
    int j = 0;
    int stop_at_dot = 0;

    if (!line1 || !line2) {
        return;
    }
    line1[0] = '\0';
    line2[0] = '\0';
    if (!item) {
        return;
    }

    stop_at_dot = !item->is_dir;
    for (i = 0; i < 8 && item->name[i] != '\0'; i++) {
        if (stop_at_dot && item->name[i] == '.') {
            break;
        }
        line1[i] = item->name[i];
    }
    line1[i] = '\0';

    while (item->name[i] != '\0' && j < 8) {
        if (stop_at_dot && item->name[i] == '.') {
            break;
        }
        line2[j++] = item->name[i++];
    }
    line2[j] = '\0';
}

void draw_explorer_grid(const minidos_app_api_t* api, const demo_state_t* state,
    const explorer_state_t* explorer, ui_rect_t bounds, ui_rect_t clip) {
    int columns;
    int rows;
    int slot;

    if (!api || !state || !explorer) {
        return;
    }

    ui_fill_rect_clipped(api, bounds, state->wm.theme.field_bg, clip);

    columns = explorer_grid_columns_for_width(bounds.w);
    rows = bounds.h / EXPLORER_GRID_CELL_H;
    if (rows < 1) {
        rows = 1;
    }

    for (slot = 0; slot < columns * rows; slot++) {
        int item_index = explorer->listview.scroll_offset + slot;
        int col = slot % columns;
        int row = slot / columns;
        int selected;
        int label_w1;
        int label_w2;
        int label_x1;
        int label_x2;
        int icon_id;
        char label1[9];
        char label2[9];
        ui_rect_t cell;
        ui_rect_t icon_rect;
        ui_rect_t label_rect1;
        ui_rect_t label_rect2;
        ui_rect_t selected_rect;
        const ui_listview_item_t* item;

        if (item_index < 0 || item_index >= explorer->listview.item_count) {
            continue;
        }

        item = &explorer->listview.items[item_index];
        selected = item_index == explorer->listview.selected_index;
        cell = ui_rect_make(bounds.x + (col * EXPLORER_GRID_CELL_W),
            bounds.y + (row * EXPLORER_GRID_CELL_H),
            EXPLORER_GRID_CELL_W,
            EXPLORER_GRID_CELL_H);
        if (ui_rect_is_empty(ui_rect_intersect(cell, clip))) {
            continue;
        }

        explorer_item_display_lines(item, label1, label2);
        label_w1 = ui_strlen(label1) * UI_CHAR_W;
        label_w2 = ui_strlen(label2) * UI_CHAR_W;
        label_x1 = cell.x + ((cell.w - label_w1) / 2);
        label_x2 = cell.x + ((cell.w - label_w2) / 2);
        icon_rect = ui_rect_make(cell.x + ((cell.w - EXPLORER_GRID_ICON_W) / 2),
            cell.y + 5,
            EXPLORER_GRID_ICON_W,
            EXPLORER_GRID_ICON_H);
        label_rect1 = ui_rect_make(label_x1, cell.y + EXPLORER_GRID_ICON_H + 10, label_w1, UI_CHAR_H);
        label_rect2 = ui_rect_make(label_x2, label_rect1.y + UI_CHAR_H, label_w2, UI_CHAR_H);
        selected_rect = ui_rect_make(cell.x + 7, icon_rect.y - 3, cell.w - 14,
            EXPLORER_GRID_ICON_H + (UI_CHAR_H * 2) + 17);

        if (selected) {
            ui_fill_rect_clipped(api, selected_rect, state->wm.theme.selection_bg, clip);
        }

        icon_id = explorer_item_icon_id(explorer, item, selected);
        draw_win95_icon_clipped(api, icon_rect, icon_id, clip);
        draw_text_transparent_clipped(api, label_rect1.x, label_rect1.y, label1,
            selected ? state->wm.theme.selection_text : state->wm.theme.text,
            clip);
        draw_text_transparent_clipped(api, label_rect2.x, label_rect2.y, label2,
            selected ? state->wm.theme.selection_text : state->wm.theme.text,
            clip);
    }
}

int open_desktop_folder(const minidos_app_api_t* api, demo_state_t* state, int item_index) {
    const desktop_item_t* item;

    if (!api || !state || item_index < 0 || item_index >= state->desktop_item_count) {
        return 0;
    }

    item = &state->desktop_items[item_index];
    if (!item->is_dir) {
        update_status_text(state, "Abrir ficheiros do desktop vem a seguir.");
        return 0;
    }

    return explorer_open_desktop_folder(api, state, item->name);
}
