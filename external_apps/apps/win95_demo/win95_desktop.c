#include "win95_demo.h"
#include "win95_icons.h"

static int str_contains(const char* text, const char* needle) {
    int i;
    int j;

    if (!text || !needle || needle[0] == '\0') {
        return 0;
    }

    for (i = 0; text[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0' && text[i + j] == needle[j]; j++) {
        }
        if (needle[j] == '\0') {
            return 1;
        }
    }

    return 0;
}

static const char* desktop_item_extension(const desktop_item_t* item) {
    const char* ext = 0;
    int i;

    if (!item) {
        return 0;
    }

    for (i = 0; item->name[i] != '\0'; i++) {
        if (item->name[i] == '.') {
            ext = &item->name[i + 1];
        }
    }

    if (!ext || ext[0] == '\0') {
        return 0;
    }

    return ext;
}

static void desktop_item_display_name(const desktop_item_t* item, char out[13]) {
    int i;
    int j = 0;

    if (!out) {
        return;
    }

    out[0] = '\0';
    if (!item) {
        return;
    }

    for (i = 0; item->name[i] != '\0' && j < 12; i++) {
        if (item->name[i] == '.') {
            break;
        }
        out[j++] = item->name[i];
    }
    out[j] = '\0';
}

static int desktop_item_icon_id(const desktop_item_t* item, int selected) {
    const char* ext;

    if (!item) {
        return WIN95_ICON_FILE_UNKNOWN;
    }

    if (item->is_dir) {
        if (str_contains(item->name, "RECYC")) {
            return WIN95_ICON_RECYCLE_BIN;
        }
        if (str_contains(item->name, "NET")) {
            return WIN95_ICON_NETWORK;
        }
        if (str_contains(item->name, "SET")) {
            return WIN95_ICON_SETTINGS;
        }
        if (str_contains(item->name, "SONG") || str_contains(item->name, "MUSIC")) {
            return WIN95_ICON_SPEAKER;
        }
        return selected ? WIN95_ICON_FOLDER_OPEN : WIN95_ICON_FOLDER_CLOSED;
    }

    if (str_contains(item->name, "TERM")
        || str_contains(item->name, "SHELL")
        || str_contains(item->name, "DOS")) {
        return WIN95_ICON_TERMINAL;
    }
    if (str_contains(item->name, "NOTE")
        || str_contains(item->name, "EDIT")) {
        return WIN95_ICON_NOTEPAD;
    }
    if (str_contains(item->name, "SET")) {
        return WIN95_ICON_SETTINGS;
    }
    if (str_contains(item->name, "NET")) {
        return WIN95_ICON_NETWORK;
    }
    if (str_contains(item->name, "SPK")
        || str_contains(item->name, "SOUND")
        || str_contains(item->name, "AUDIO")) {
        return WIN95_ICON_SPEAKER;
    }
    if (str_contains(item->name, "COMP")
        || str_contains(item->name, "MYPC")) {
        return WIN95_ICON_COMPUTER;
    }
    if (str_contains(item->name, "DISK")) {
        return WIN95_ICON_FLOPPY;
    }
    if (str_contains(item->name, "DRIVE")) {
        return WIN95_ICON_HARD_DRIVE;
    }

    ext = desktop_item_extension(item);
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
    if ((ext[0] == 'I' && ext[1] == 'M' && ext[2] == 'G' && ext[3] == '\0')
        || (ext[0] == 'I' && ext[1] == 'S' && ext[2] == 'O' && ext[3] == '\0')) {
        return WIN95_ICON_HARD_DRIVE;
    }

    return WIN95_ICON_FILE_UNKNOWN;
}

static void layout_desktop_items(demo_state_t* state) {
    int i;
    int columns;
    int left;
    int top;

    if (!state) {
        return;
    }

    columns = (state->sw - 24) / DESKTOP_CELL_W;
    if (columns < 1) {
        columns = 1;
    }
    if (columns > 6) {
        columns = 6;
    }

    left = 16;
    top = 18;

    for (i = 0; i < state->desktop_item_count; i++) {
        int col = i % columns;
        int row = i / columns;

        state->desktop_items[i].bounds = ui_rect_make(
            left + (col * DESKTOP_CELL_W),
            top + (row * DESKTOP_CELL_H),
            DESKTOP_CELL_W - 6,
            DESKTOP_CELL_H - 6);
    }
}

void load_desktop_items(const minidos_app_api_t* api, demo_state_t* state) {
    int i;
    int count = 0;

    if (!api || !state) {
        return;
    }

    enter_desktop_home(api);
    for (i = 0; i < DESKTOP_MAX_ITEMS; i++) {
        char name[13];
        int is_dir = 0;

        name[0] = '\0';
        if (!app_list_entry(api, (unsigned int)i, name, &is_dir)) {
            break;
        }
        if (name[0] == '\0'
            || (name[0] == '.' && name[1] == '\0')
            || (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
            continue;
        }
        str_copy(state->desktop_items[count].name, name, (int)sizeof(state->desktop_items[count].name));
        state->desktop_items[count].is_dir = is_dir;
        state->desktop_items[count].bounds = ui_rect_make(0, 0, 0, 0);
        count++;
    }
    state->desktop_item_count = count;
    layout_desktop_items(state);
}

void draw_win95_icon_clipped(const minidos_app_api_t* api, ui_rect_t icon_rect, int icon_id, ui_rect_t clip) {
    const win95_icon_bitmap_t* icon;
    int draw_w;
    int draw_h;
    int offset_x;
    int offset_y;
    int y;

    if (!api || icon_id < 0 || icon_id >= WIN95_ICON_COUNT) {
        return;
    }

    icon = &g_win95_icons[icon_id];
    if (!icon->pixels || icon->width == 0 || icon->height == 0) {
        return;
    }

    draw_w = ((icon_rect.h + 2) * icon->width) / icon->height;
    draw_h = icon_rect.h + 2;
    if (draw_w > icon_rect.w) {
        draw_w = icon_rect.w;
        draw_h = (icon_rect.w * icon->height) / icon->width;
    }
    if (draw_w < 1) {
        draw_w = 1;
    }
    if (draw_h < 1) {
        draw_h = 1;
    }

    offset_x = icon_rect.x + ((icon_rect.w - draw_w) / 2);
    offset_y = icon_rect.y + ((icon_rect.h - draw_h) / 2);

    for (y = 0; y < draw_h; y++) {
        int src_y = (y * icon->height) / draw_h;
        int x = 0;

        while (x < draw_w) {
            int src_x = (x * icon->width) / draw_w;
            unsigned int argb = icon->pixels[(src_y * icon->width) + src_x];
            unsigned int alpha = (argb >> 24) & 0xFFu;

            if (alpha < 128u) {
                x++;
                continue;
            }

            {
                unsigned int rgb = argb & 0x00FFFFFFu;
                int run_x = x;

                while (x < draw_w) {
                    int run_src_x = (x * icon->width) / draw_w;
                    unsigned int next = icon->pixels[(src_y * icon->width) + run_src_x];
                    if (((next >> 24) & 0xFFu) < 128u || (next & 0x00FFFFFFu) != rgb) {
                        break;
                    }
                    x++;
                }

                ui_fill_rect_clipped(api,
                    ui_rect_make(offset_x + run_x, offset_y + y, x - run_x, 1),
                    rgb,
                    clip);
            }
        }
    }
}

static void draw_desktop_item(const minidos_app_api_t* api, const demo_state_t* state,
    const desktop_item_t* item, int item_index, ui_rect_t clip) {
    ui_rect_t icon_rect;
    ui_rect_t label_rect;
    ui_rect_t selected_rect;
    ui_rect_t body_rect;
    char display_name[13];
    int icon_id;
    int label_w;
    int content_w;
    int content_x;

    if (!api || !state || !item) {
        return;
    }

    body_rect = item->bounds;
    if (ui_rect_is_empty(ui_rect_intersect(body_rect, clip))) {
        return;
    }

    icon_id = desktop_item_icon_id(item, state->selected_desktop_item == item_index);
    desktop_item_display_name(item, display_name);

    label_w = ui_strlen(display_name) * UI_CHAR_W;
    if (label_w < DESKTOP_ICON_W) {
        content_w = DESKTOP_ICON_W;
    } else {
        content_w = label_w;
    }
    content_x = body_rect.x + ((body_rect.w - content_w) / 2);
    icon_rect = ui_rect_make(body_rect.x + ((body_rect.w - DESKTOP_ICON_W) / 2), body_rect.y + 2, DESKTOP_ICON_W, DESKTOP_ICON_H);
    label_rect = ui_rect_make(content_x, body_rect.y + DESKTOP_ICON_H + 7, label_w, UI_CHAR_H);
    selected_rect = ui_rect_make(content_x - 3, body_rect.y, content_w + 6, DESKTOP_ICON_H + UI_CHAR_H + 10);

    if (state->selected_desktop_item == item_index) {
        ui_fill_rect_clipped(api, selected_rect, state->wm.theme.selection_bg, clip);
    }

    draw_win95_icon_clipped(api, icon_rect, icon_id, clip);

    draw_text_transparent_clipped(api, label_rect.x, label_rect.y, display_name,
        (state->selected_desktop_item == item_index) ? state->wm.theme.selection_text : state->wm.theme.text,
        clip);
}

void draw_desktop_items(const minidos_app_api_t* api, const demo_state_t* state, ui_rect_t clip) {
    int i;

    if (!api || !state) {
        return;
    }

    for (i = 0; i < state->desktop_item_count; i++) {
        draw_desktop_item(api, state, &state->desktop_items[i], i, clip);
    }
}

int desktop_item_hit_test(const demo_state_t* state, int x, int y) {
    int i;

    if (!state) {
        return -1;
    }

    for (i = 0; i < state->desktop_item_count; i++) {
        char display_name[13];
        int label_w;
        int content_w;
        int content_x;
        ui_rect_t visual_rect;

        desktop_item_display_name(&state->desktop_items[i], display_name);
        label_w = ui_strlen(display_name) * UI_CHAR_W;
        content_w = (label_w > DESKTOP_ICON_W) ? label_w : DESKTOP_ICON_W;
        content_x = state->desktop_items[i].bounds.x + ((state->desktop_items[i].bounds.w - content_w) / 2);
        visual_rect = ui_rect_make(content_x - 3,
            state->desktop_items[i].bounds.y,
            content_w + 6,
            DESKTOP_ICON_H + UI_CHAR_H + 10);
        if (ui_rect_contains(&visual_rect, x, y)) {
            return i;
        }
    }

    return -1;
}
