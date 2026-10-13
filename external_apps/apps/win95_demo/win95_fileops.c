#include "win95_demo.h"

/* Context menu, modal dialogs (new folder / rename / delete) and a simple
 * text-file viewer window for the win95 file manager. */

static const char* ctx_action_label(int action) {
    switch (action) {
        case CTX_ACTION_OPEN:      return "Abrir";
        case CTX_ACTION_CUT:       return "Cortar";
        case CTX_ACTION_COPY:      return "Copiar";
        case CTX_ACTION_PASTE:     return "Colar";
        case CTX_ACTION_DELETE:    return "Apagar";
        case CTX_ACTION_RENAME:    return "Renomear";
        case CTX_ACTION_NEWFOLDER: return "Nova Pasta";
        default:                   return "";
    }
}

static ui_rect_t fileops_screen_clip(const demo_state_t* state) {
    return ui_rect_make(0, 0, state->sw, state->sh);
}

void fileops_init(demo_state_t* state) {
    int i;

    if (!state) {
        return;
    }
    state->ctx_menu_open = 0;
    state->ctx_menu_x = 0;
    state->ctx_menu_y = 0;
    state->ctx_menu_item_count = 0;
    state->ctx_menu_hot = -1;
    state->ctx_menu_explorer_window = 0;
    for (i = 0; i < CTX_MENU_MAX_ITEMS; i++) {
        state->ctx_menu_actions[i] = 0;
    }
    state->modal_kind = MODAL_NONE;
    state->modal_action = FILEOP_NONE;
    state->modal_explorer_window = 0;
    state->modal_title[0] = '\0';
    state->modal_message[0] = '\0';
    state->modal_input[0] = '\0';
    state->modal_target[0] = '\0';
    state->viewer_window_id = 0;
    state->viewer_len = 0;
    state->viewer_title[0] = '\0';
    state->viewer_text[0] = '\0';
}

/* ---- context menu ---- */

int ctx_menu_is_open(const demo_state_t* state) {
    return state && state->ctx_menu_open;
}

void ctx_menu_dismiss(demo_state_t* state) {
    if (!state) {
        return;
    }
    state->ctx_menu_open = 0;
    state->ctx_menu_hot = -1;
    state->layout_version++;
}

ui_rect_t ctx_menu_rect(const demo_state_t* state) {
    int h;

    if (!state || !state->ctx_menu_open) {
        return ui_rect_make(0, 0, 0, 0);
    }
    h = state->ctx_menu_item_count * CTX_MENU_ITEM_H + 4;
    return ui_rect_make(state->ctx_menu_x, state->ctx_menu_y, CTX_MENU_W, h);
}

static void ctx_menu_add(demo_state_t* state, int action) {
    if (state->ctx_menu_item_count < CTX_MENU_MAX_ITEMS) {
        state->ctx_menu_actions[state->ctx_menu_item_count++] = action;
    }
}

void ctx_menu_open_for(demo_state_t* state, explorer_state_t* explorer, int x, int y) {
    const ui_listview_item_t* item;
    ui_rect_t rect;

    if (!state || !explorer) {
        return;
    }

    state->ctx_menu_item_count = 0;
    state->ctx_menu_explorer_window = explorer->window_id;
    item = explorer_selected_item(explorer);

    if (explorer->showing_drives) {
        if (item) {
            ctx_menu_add(state, CTX_ACTION_OPEN);
        }
    } else {
        if (item) {
            ctx_menu_add(state, CTX_ACTION_OPEN);
            if (!item->is_dir) {
                ctx_menu_add(state, CTX_ACTION_CUT);
                ctx_menu_add(state, CTX_ACTION_COPY);
            }
            ctx_menu_add(state, CTX_ACTION_RENAME);
            ctx_menu_add(state, CTX_ACTION_DELETE);
        }
        ctx_menu_add(state, CTX_ACTION_NEWFOLDER);
        ctx_menu_add(state, CTX_ACTION_PASTE);
    }

    if (state->ctx_menu_item_count == 0) {
        state->ctx_menu_open = 0;
        return;
    }

    state->ctx_menu_x = x;
    state->ctx_menu_y = y;
    state->ctx_menu_open = 1;
    state->ctx_menu_hot = -1;

    /* Keep the whole menu on screen. */
    rect = ctx_menu_rect(state);
    if (rect.x + rect.w > state->sw - 2) {
        state->ctx_menu_x = state->sw - rect.w - 2;
    }
    if (rect.y + rect.h > state->sh - TASKBAR_H - 2) {
        state->ctx_menu_y = state->sh - TASKBAR_H - rect.h - 2;
    }
    if (state->ctx_menu_x < 2) {
        state->ctx_menu_x = 2;
    }
    if (state->ctx_menu_y < 2) {
        state->ctx_menu_y = 2;
    }
    state->layout_version++;
}

int ctx_menu_hit_test(const demo_state_t* state, int x, int y) {
    ui_rect_t rect;
    int row;

    if (!ctx_menu_is_open(state)) {
        return -1;
    }
    rect = ctx_menu_rect(state);
    if (!ui_rect_contains(&rect, x, y)) {
        return -1;
    }
    row = (y - (rect.y + 2)) / CTX_MENU_ITEM_H;
    if (row < 0 || row >= state->ctx_menu_item_count) {
        return -1;
    }
    return row;
}

/* Forward declarations for modal openers used by the menu. */
static void modal_open_prompt(demo_state_t* state, explorer_state_t* explorer,
    int action, const char* title, const char* prefill);
static void modal_open_confirm(demo_state_t* state, explorer_state_t* explorer, const char* name);

static void ctx_menu_execute(const minidos_app_api_t* api, demo_state_t* state, int row) {
    explorer_state_t* explorer;
    const ui_listview_item_t* item;
    int action;

    if (row < 0 || row >= state->ctx_menu_item_count) {
        return;
    }
    action = state->ctx_menu_actions[row];
    explorer = explorer_for_window(state, state->ctx_menu_explorer_window);
    ctx_menu_dismiss(state);
    if (!explorer) {
        return;
    }

    item = explorer_selected_item(explorer);
    switch (action) {
        case CTX_ACTION_OPEN:
            (void)explorer_open_selected_in(api, state, explorer);
            break;
        case CTX_ACTION_CUT:
            if (item) {
                (void)explorer_exec_clip(api, state, explorer, item->name, 2);
            }
            break;
        case CTX_ACTION_COPY:
            if (item) {
                (void)explorer_exec_clip(api, state, explorer, item->name, 1);
            }
            break;
        case CTX_ACTION_PASTE:
            (void)explorer_exec_paste(api, state, explorer);
            break;
        case CTX_ACTION_DELETE:
            if (item) {
                modal_open_confirm(state, explorer, item->name);
            }
            break;
        case CTX_ACTION_RENAME:
            if (item) {
                modal_open_prompt(state, explorer, FILEOP_RENAME, "Renomear", item->name);
            }
            break;
        case CTX_ACTION_NEWFOLDER:
            modal_open_prompt(state, explorer, FILEOP_NEWFOLDER, "Nova Pasta", "");
            break;
        default:
            break;
    }
}

void ctx_menu_handle_mouse(const minidos_app_api_t* api, demo_state_t* state, int x, int y, int left_pressed) {
    int row;

    if (!ctx_menu_is_open(state)) {
        return;
    }
    row = ctx_menu_hit_test(state, x, y);
    if (row != state->ctx_menu_hot) {
        state->ctx_menu_hot = row;
        state->layout_version++;
    }
    if (!left_pressed) {
        return;
    }
    if (row >= 0) {
        ctx_menu_execute(api, state, row);
    } else {
        ctx_menu_dismiss(state);
    }
}

void draw_ctx_menu(const minidos_app_api_t* api, const demo_state_t* state) {
    const ui_theme_t* theme;
    ui_rect_t rect;
    ui_rect_t clip;
    int i;

    if (!api || !ctx_menu_is_open(state)) {
        return;
    }
    theme = &state->wm.theme;
    rect = ctx_menu_rect(state);
    clip = fileops_screen_clip(state);

    ui_draw_panel_clipped(api, theme, rect, 1, clip);

    for (i = 0; i < state->ctx_menu_item_count; i++) {
        ui_rect_t row = ui_rect_make(rect.x + 2, rect.y + 2 + i * CTX_MENU_ITEM_H,
            rect.w - 4, CTX_MENU_ITEM_H);
        unsigned int fg = theme->text;

        if (i == state->ctx_menu_hot) {
            ui_fill_rect_clipped(api, row, theme->selection_bg, clip);
            fg = theme->selection_text;
        }
        ui_draw_text_clipped(api, row.x + 8, row.y + 5,
            ctx_action_label(state->ctx_menu_actions[i]), fg,
            (i == state->ctx_menu_hot) ? theme->selection_bg : theme->face, clip);
    }
}

/* ---- modal dialogs ---- */

int modal_is_active(const demo_state_t* state) {
    return state && state->modal_kind != MODAL_NONE;
}

static ui_rect_t modal_rect(const demo_state_t* state) {
    int w = 252;
    int h = 126;
    return ui_rect_make((state->sw - w) / 2, (state->sh - h) / 2, w, h);
}

static ui_rect_t modal_input_rect(const demo_state_t* state) {
    ui_rect_t box = modal_rect(state);
    return ui_rect_make(box.x + 14, box.y + 54, box.w - 28, 20);
}

static ui_rect_t modal_ok_rect(const demo_state_t* state) {
    ui_rect_t box = modal_rect(state);
    return ui_rect_make(box.x + box.w - 160, box.y + box.h - 30, 70, 22);
}

static ui_rect_t modal_cancel_rect(const demo_state_t* state) {
    ui_rect_t box = modal_rect(state);
    return ui_rect_make(box.x + box.w - 82, box.y + box.h - 30, 70, 22);
}

static void modal_open_prompt(demo_state_t* state, explorer_state_t* explorer,
    int action, const char* title, const char* prefill) {
    if (!state || !explorer) {
        return;
    }
    state->modal_kind = MODAL_PROMPT;
    state->modal_action = action;
    state->modal_explorer_window = explorer->window_id;
    str_copy(state->modal_title, title, (int)sizeof(state->modal_title));
    str_copy(state->modal_input, prefill ? prefill : "", (int)sizeof(state->modal_input));
    str_copy(state->modal_target, prefill ? prefill : "", (int)sizeof(state->modal_target));
    state->modal_message[0] = '\0';
    state->layout_version++;
}

static void modal_open_confirm(demo_state_t* state, explorer_state_t* explorer, const char* name) {
    if (!state || !explorer || !name) {
        return;
    }
    state->modal_kind = MODAL_CONFIRM;
    state->modal_action = FILEOP_DELETE;
    state->modal_explorer_window = explorer->window_id;
    str_copy(state->modal_title, "Apagar", (int)sizeof(state->modal_title));
    str_copy(state->modal_target, name, (int)sizeof(state->modal_target));
    str_copy(state->modal_message, "Apagar este item?", (int)sizeof(state->modal_message));
    state->modal_input[0] = '\0';
    state->layout_version++;
}

void fileops_request_new_folder(demo_state_t* state, explorer_state_t* explorer) {
    if (!state || !explorer || explorer->showing_drives) {
        return;
    }
    modal_open_prompt(state, explorer, FILEOP_NEWFOLDER, "Nova Pasta", "");
}

void fileops_request_rename(demo_state_t* state, explorer_state_t* explorer) {
    const ui_listview_item_t* item;

    if (!state || !explorer || explorer->showing_drives) {
        return;
    }
    item = explorer_selected_item(explorer);
    if (item) {
        modal_open_prompt(state, explorer, FILEOP_RENAME, "Renomear", item->name);
    }
}

void fileops_request_delete(demo_state_t* state, explorer_state_t* explorer) {
    const ui_listview_item_t* item;

    if (!state || !explorer || explorer->showing_drives) {
        return;
    }
    item = explorer_selected_item(explorer);
    if (item) {
        modal_open_confirm(state, explorer, item->name);
    }
}

static void modal_close(demo_state_t* state) {
    state->modal_kind = MODAL_NONE;
    state->modal_action = FILEOP_NONE;
    state->layout_version++;
}

static void modal_confirm(const minidos_app_api_t* api, demo_state_t* state) {
    explorer_state_t* explorer = explorer_for_window(state, state->modal_explorer_window);
    int action = state->modal_action;

    if (explorer) {
        switch (action) {
            case FILEOP_NEWFOLDER:
                (void)explorer_exec_new_folder(api, state, explorer, state->modal_input);
                break;
            case FILEOP_RENAME:
                (void)explorer_exec_rename(api, state, explorer, state->modal_target, state->modal_input);
                break;
            case FILEOP_DELETE:
                (void)explorer_exec_delete(api, state, explorer, state->modal_target);
                break;
            default:
                break;
        }
    }
    modal_close(state);
}

void modal_handle_mouse(const minidos_app_api_t* api, demo_state_t* state, int x, int y, int left_pressed) {
    ui_rect_t ok;
    ui_rect_t cancel;

    if (!modal_is_active(state) || !left_pressed) {
        return;
    }
    ok = modal_ok_rect(state);
    cancel = modal_cancel_rect(state);
    if (ui_rect_contains(&ok, x, y)) {
        modal_confirm(api, state);
    } else if (ui_rect_contains(&cancel, x, y)) {
        modal_close(state);
    }
}

void modal_handle_key(const minidos_app_api_t* api, demo_state_t* state, char c) {
    int len;

    if (!modal_is_active(state)) {
        return;
    }

    if (c == KEY_ESC) {
        modal_close(state);
        return;
    }
    if (c == KEY_ENTER) {
        modal_confirm(api, state);
        return;
    }

    if (state->modal_kind != MODAL_PROMPT) {
        return;
    }

    len = ui_strlen(state->modal_input);
    if (c == KEY_BACKSPACE) {
        if (len > 0) {
            state->modal_input[len - 1] = '\0';
            state->layout_version++;
        }
        return;
    }
    if (c >= 32 && c <= 126 && len < (int)sizeof(state->modal_input) - 1) {
        state->modal_input[len] = c;
        state->modal_input[len + 1] = '\0';
        state->layout_version++;
    }
}

static void draw_modal_button(const minidos_app_api_t* api, const ui_theme_t* theme,
    ui_rect_t rect, const char* label, ui_rect_t clip) {
    int tx;

    ui_draw_panel_clipped(api, theme, rect, 1, clip);
    tx = rect.x + (rect.w - ui_strlen(label) * UI_CHAR_W) / 2;
    ui_draw_text_clipped(api, tx, rect.y + (rect.h - UI_CHAR_H) / 2, label,
        theme->text, theme->face, clip);
}

void draw_modal(const minidos_app_api_t* api, const demo_state_t* state) {
    const ui_theme_t* theme;
    ui_rect_t box;
    ui_rect_t title_bar;
    ui_rect_t clip;

    if (!api || !modal_is_active(state)) {
        return;
    }
    theme = &state->wm.theme;
    box = modal_rect(state);
    clip = fileops_screen_clip(state);

    ui_draw_panel_clipped(api, theme, box, 1, clip);
    title_bar = ui_rect_make(box.x + 3, box.y + 3, box.w - 6, 16);
    ui_fill_rect_clipped(api, title_bar, theme->title_active_bg, clip);
    ui_draw_text_clipped(api, title_bar.x + 4, title_bar.y + 4, state->modal_title,
        theme->title_active_text, theme->title_active_bg, clip);

    if (state->modal_kind == MODAL_PROMPT) {
        ui_rect_t field = modal_input_rect(state);
        char shown[MODAL_INPUT_MAX + 1];
        int len = ui_strlen(state->modal_input);

        ui_draw_text_clipped(api, box.x + 14, box.y + 30, "Nome:",
            theme->text, theme->face, clip);
        ui_draw_panel_clipped(api, theme, field, 0, clip);
        str_copy(shown, state->modal_input, (int)sizeof(shown));
        if (len < (int)sizeof(shown) - 1) {
            shown[len] = '_';
            shown[len + 1] = '\0';
        }
        ui_draw_text_clipped(api, field.x + 4, field.y + 6, shown,
            theme->field_text, theme->field_bg, clip);
    } else {
        ui_draw_text_clipped(api, box.x + 14, box.y + 40, state->modal_message,
            theme->text, theme->face, clip);
        ui_draw_text_clipped(api, box.x + 14, box.y + 54, state->modal_target,
            theme->text, theme->face, clip);
    }

    draw_modal_button(api, theme, modal_ok_rect(state),
        state->modal_kind == MODAL_CONFIRM ? "Sim" : "OK", clip);
    draw_modal_button(api, theme, modal_cancel_rect(state),
        state->modal_kind == MODAL_CONFIRM ? "Nao" : "Cancelar", clip);
}

/* ---- text file viewer ---- */

int viewer_owns_window(const demo_state_t* state, int window_id) {
    return state && window_id != 0 && state->viewer_window_id == window_id;
}

void viewer_close_window(demo_state_t* state, int window_id) {
    if (!state || !viewer_owns_window(state, window_id)) {
        return;
    }
    ui_wm_close_window(&state->wm, state->viewer_window_id);
    state->viewer_window_id = 0;
    state->layout_version++;
}

static void viewer_build_title(demo_state_t* state, const char* name) {
    str_copy(state->viewer_title, "Notas - ", (int)sizeof(state->viewer_title));
    {
        int pos = ui_strlen(state->viewer_title);
        int i = 0;
        while (name[i] != '\0' && pos < (int)sizeof(state->viewer_title) - 1) {
            state->viewer_title[pos++] = name[i++];
        }
        state->viewer_title[pos] = '\0';
    }
}

int open_file_viewer(const minidos_app_api_t* api, demo_state_t* state, explorer_state_t* explorer, const char* name) {
    int n;
    ui_rect_t bounds;
    ui_wm_window_t* win;

    if (!api || !state || !explorer || !name || name[0] == '\0' || explorer->showing_drives) {
        return 0;
    }

    if (!explorer_enter_current_dir(api, explorer)) {
        update_status_text(state, "Falha ao abrir a pasta.");
        return 0;
    }

    n = app_file_read(api, name, (unsigned char*)state->viewer_text, VIEWER_MAX_BYTES - 1);
    if (n < 0) {
        update_status_text(state, "Falha ao ler o ficheiro.");
        return 0;
    }
    if (n > VIEWER_MAX_BYTES - 1) {
        n = VIEWER_MAX_BYTES - 1;
    }
    state->viewer_text[n] = '\0';
    state->viewer_len = n;
    viewer_build_title(state, name);

    bounds = ui_rect_make((state->sw - 360) / 2 + 24, 72, 360, 260);
    if (bounds.x < 8) {
        bounds.x = 8;
    }

    if (state->viewer_window_id == 0) {
        state->viewer_window_id = ui_wm_create_window_ex(&state->wm, bounds, state->viewer_title, 1, 1, 1);
    } else {
        win = ui_wm_find_window(&state->wm, state->viewer_window_id);
        if (win) {
            win->visible = 1;
            win->window.minimized = 0;
            win->window.title = state->viewer_title;
        } else {
            state->viewer_window_id = ui_wm_create_window_ex(&state->wm, bounds, state->viewer_title, 1, 1, 1);
        }
    }

    if (state->viewer_window_id != 0) {
        ui_wm_bring_to_front(&state->wm, state->viewer_window_id);
    }
    update_status_text(state, "Ficheiro aberto.");
    state->layout_version++;
    return 1;
}

void draw_viewer_chrome_clipped(const minidos_app_api_t* api, const demo_state_t* state,
    const ui_wm_window_t* win, ui_rect_t clip) {
    const ui_theme_t* theme;
    ui_rect_t client;
    ui_rect_t text_clip;
    int x0;
    int y;
    int y_bottom;
    int max_cols;
    int i;
    int col;
    char line[96];

    if (!api || !state || !win) {
        return;
    }

    theme = &state->wm.theme;
    client = ui_window_client_rect(&win->window);
    if (ui_rect_is_empty(ui_rect_intersect(client, clip))) {
        return;
    }

    ui_fill_rect_clipped(api, client, theme->field_bg, clip);
    text_clip = ui_rect_intersect(ui_rect_inset(client, 4), clip);

    x0 = client.x + 6;
    y = client.y + 4;
    y_bottom = client.y + client.h - 2;
    max_cols = (client.w - 12) / UI_CHAR_W;
    if (max_cols < 1) {
        max_cols = 1;
    }
    if (max_cols > (int)sizeof(line) - 1) {
        max_cols = (int)sizeof(line) - 1;
    }
    col = 0;

    for (i = 0; i < state->viewer_len && y + UI_CHAR_H <= y_bottom; i++) {
        char c = state->viewer_text[i];

        if (c == '\r') {
            continue;
        }
        if (c == '\n' || col >= max_cols) {
            line[col] = '\0';
            ui_draw_text_clipped(api, x0, y, line, theme->field_text, theme->field_bg, text_clip);
            y += UI_CHAR_H + 2;
            col = 0;
            if (c == '\n') {
                continue;
            }
        }
        if (c == '\t') {
            c = ' ';
        }
        if (c >= 32 && c <= 126) {
            line[col++] = c;
        }
    }

    if (col > 0 && y + UI_CHAR_H <= y_bottom) {
        line[col] = '\0';
        ui_draw_text_clipped(api, x0, y, line, theme->field_text, theme->field_bg, text_clip);
    }
}
