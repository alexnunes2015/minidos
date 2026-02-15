#include "minidos_app.h"

#define KEY_UP 0x11
#define KEY_DOWN 0x12
#define KEY_ALT_TOGGLE 0x14
#define KEY_LEFT 0x15
#define KEY_RIGHT 0x16

#define EDIT_COLS 78
#define EDIT_ROWS 24
#define TITLE_TEXT "MiniDOS Editor"
#define STATUS_HELP "ESC sair | ENTER nova linha | BACKSPACE apaga"
#define STATUS_MENU "MENU ATIVO: ALT sai | ESQ/DIR menu | CIMA/BAIXO item"
#define DEFAULT_NEW_FILE "UNTITLED.TXT"

#define MENU_COUNT 5
#define MENU_ITEMS 5
#define DLG_MAX_ENTRIES 64
#define DLG_VISIBLE_ROWS 10
#define EDIT_MAX_FILE_BYTES (EDIT_ROWS * (EDIT_COLS + 2))

static const char* g_menu_labels[MENU_COUNT] = {
    " File ", " Edit ", " Search ", " Options ", " Help "
};

static const char* g_menu_items[MENU_COUNT][MENU_ITEMS] = {
    { " Novo ", " Abrir ", " Save ", " Save As ", " Exit " },
    { " Copiar ", " Colar ", " Selecionar ", " Desfazer ", " Refazer " },
    { " Localizar ", " Proximo ", " Anterior ", " Ir para ", " Substituir " },
    { " Aparencia ", " Fonte ", " Cores ", " Margens ", " Layout " },
    { " Sobre ", " Teclas ", " Creditos ", " Versao ", " Ajuda " }
};

static const int g_menu_x[MENU_COUNT] = { 8, 56, 120, 184, 256 };

typedef struct {
    char name[13];
    int is_dir;
} dlg_entry_t;

static void mem_clear(char* dst, int len) {
    for (int i = 0; i < len; i++) {
        dst[i] = ' ';
    }
}

static void line_blank(char* line) {
    mem_clear(line, EDIT_COLS);
    line[EDIT_COLS] = '\0';
}

static void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    if (max <= 0) {
        return;
    }
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_len(const char* s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static int int_to_dec(int value, char* out, int out_size) {
    char tmp[12];
    int pos = 0;
    int n = value;
    int start = 0;

    if (out_size < 2) {
        return 0;
    }

    if (n == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }

    if (n < 0) {
        if (out_size < 3) {
            return 0;
        }
        out[pos++] = '-';
        n = -n;
    }

    while (n > 0 && start < (int)sizeof(tmp)) {
        tmp[start++] = (char)('0' + (n % 10));
        n /= 10;
    }

    for (int i = start - 1; i >= 0 && pos < out_size - 1; i--) {
        out[pos++] = tmp[i];
    }
    out[pos] = '\0';
    return pos;
}

static void draw_rect(const minidos_app_api_t* api, int x, int y, int w, int h, unsigned int color) {
    app_gfx_rect_t r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    r.color = color;
    app_gfx_rect(api, &r);
}

static void draw_text(const minidos_app_api_t* api, int x, int y, const char* text, unsigned int fg, unsigned int bg) {
    app_gfx_text_t t;
    t.x = x;
    t.y = y;
    t.text = text;
    t.fg = fg;
    t.bg = bg;
    app_gfx_text(api, &t);
}

static void draw_menu_bar(const minidos_app_api_t* api, int menu_mode, int menu_index, int sw) {
    unsigned int bar_bg = menu_mode ? 0x000080u : 0xFFFFFFu;
    unsigned int bar_fg = menu_mode ? 0xFFFFFFu : 0x000080u;

    draw_rect(api, 0, 0, sw, 16, bar_bg);

    for (int i = 0; i < MENU_COUNT; i++) {
        unsigned int fg = bar_fg;
        unsigned int bg = bar_bg;
        if (menu_mode && i == menu_index) {
            fg = 0x000080u;
            bg = 0xFFFFFFu;
        }
        draw_text(api, g_menu_x[i], 4, g_menu_labels[i], fg, bg);
    }
}

static void draw_menu_popup(const minidos_app_api_t* api, int menu_index, int item_index) {
    int px = g_menu_x[menu_index];
    int py = 16;
    int pw = 128;
    int row_h = 18;
    int top_pad = 6;
    int ph = (MENU_ITEMS * row_h) + top_pad + 6;

    draw_rect(api, px, py, pw, ph, 0xFFFFFFu);
    draw_rect(api, px + 1, py + 1, pw - 2, ph - 2, 0x000080u);

    for (int i = 0; i < MENU_ITEMS; i++) {
        unsigned int fg = 0xFFFFFFu;
        unsigned int bg = 0x000080u;
        int item_y = py + top_pad + (i * row_h);
        if (i == item_index) {
            fg = 0x000080u;
            bg = 0xFFFFFFu;
        }
        draw_rect(api, px + 4, item_y - 2, pw - 8, row_h - 2, bg);
        draw_text(api, px + 10, item_y + 2, g_menu_items[menu_index][i], fg, bg);
    }
}

static void draw_base_ui(const minidos_app_api_t* api, char lines[EDIT_ROWS][EDIT_COLS + 1], int sw, int sh, int menu_mode, int menu_index, int item_index) {
    const unsigned int color_blue = 0x0000AAu;
    const unsigned int color_white = 0xFFFFFFu;

    app_gfx_clear(api, color_blue);

    draw_menu_bar(api, menu_mode, menu_index, sw);

    draw_rect(api, 0, 16, sw, 14, color_blue);
    draw_text(api, 8, 18, TITLE_TEXT, color_white, color_blue);

    for (int row = 0; row < EDIT_ROWS; row++) {
        draw_text(api, 8, 34 + row * 16, lines[row], color_white, color_blue);
    }

    if (menu_mode) {
        draw_menu_popup(api, menu_index, item_index);
    }

    draw_rect(api, 0, sh - 16, sw, 16, color_white);
}

static void draw_cell(const minidos_app_api_t* api, char lines[EDIT_ROWS][EDIT_COLS + 1], int row, int col, int is_cursor) {
    const unsigned int color_blue = 0x0000AAu;
    const unsigned int color_white = 0xFFFFFFu;
    char text[2];
    unsigned int fg = color_white;
    unsigned int bg = color_blue;

    if (row < 0 || row >= EDIT_ROWS || col < 0 || col >= EDIT_COLS) {
        return;
    }

    text[0] = lines[row][col];
    if (text[0] == '\0') {
        text[0] = ' ';
    }
    text[1] = '\0';

    if (is_cursor) {
        fg = color_blue;
        bg = color_white;
    }

    draw_text(api, 8 + (col * 8), 34 + (row * 16), text, fg, bg);
}

static int line_used_len(const char* line) {
    int end = EDIT_COLS;
    while (end > 0 && line[end - 1] == ' ') {
        end--;
    }
    return end;
}

static void build_status(char* status, int status_size, int cur_row, int cur_col, int menu_mode) {
    char nbuf[16];
    int status_pos = 0;
    const char* base = menu_mode ? STATUS_MENU : STATUS_HELP;

    str_copy(status, base, status_size);
    while (status[status_pos]) {
        status_pos++;
    }

    if (status_pos < status_size - 2) status[status_pos++] = ' ';
    if (status_pos < status_size - 2) status[status_pos++] = '|';
    if (status_pos < status_size - 2) status[status_pos++] = ' ';

    str_copy(nbuf, "Ln ", sizeof(nbuf));
    int_to_dec(cur_row + 1, nbuf + 3, (int)sizeof(nbuf) - 3);
    for (int i = 0; nbuf[i] && status_pos < status_size - 1; i++) {
        status[status_pos++] = nbuf[i];
    }

    if (status_pos < status_size - 2) status[status_pos++] = ' ';

    str_copy(nbuf, "Col ", sizeof(nbuf));
    int_to_dec(cur_col + 1, nbuf + 4, (int)sizeof(nbuf) - 4);
    for (int i = 0; nbuf[i] && status_pos < status_size - 1; i++) {
        status[status_pos++] = nbuf[i];
    }

    status[status_pos] = '\0';
}

static void draw_status_text(const minidos_app_api_t* api, const char* text, int sw, int sh) {
    draw_rect(api, 0, sh - 16, sw, 16, 0xFFFFFFu);
    draw_text(api, 8, sh - 12, text, 0x000080u, 0xFFFFFFu);
}

static void build_file_status(char* out, int out_size, const char* file_name) {
    const char* prefix = "File: ";
    int p = 0;
    int i = 0;

    if (!out || out_size < 2) {
        return;
    }

    while (prefix[i] && p < out_size - 1) {
        out[p++] = prefix[i++];
    }

    i = 0;
    while (file_name[i] && p < out_size - 1) {
        out[p++] = file_name[i++];
    }
    out[p] = '\0';
}

static void editor_clear(char lines[EDIT_ROWS][EDIT_COLS + 1], int* cur_row, int* cur_col) {
    for (int i = 0; i < EDIT_ROWS; i++) {
        line_blank(lines[i]);
    }
    *cur_row = 0;
    *cur_col = 0;
}

static void editor_import_buffer(char lines[EDIT_ROWS][EDIT_COLS + 1], int* cur_row, int* cur_col, const unsigned char* data, int size) {
    int row = 0;
    int col = 0;
    editor_clear(lines, &row, &col);

    for (int i = 0; i < size && row < EDIT_ROWS; i++) {
        unsigned char c = data[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            row++;
            col = 0;
            continue;
        }
        if (c >= 32 && c <= 126) {
            lines[row][col] = (char)c;
            col++;
            if (col >= EDIT_COLS) {
                col = 0;
                row++;
            }
        }
    }

    if (row >= EDIT_ROWS) {
        row = EDIT_ROWS - 1;
        col = EDIT_COLS - 1;
    }
    *cur_row = row;
    *cur_col = col;
}

static int editor_export_buffer(char lines[EDIT_ROWS][EDIT_COLS + 1], unsigned char* out, int out_max) {
    int last_row = -1;
    int pos = 0;

    for (int r = 0; r < EDIT_ROWS; r++) {
        if (line_used_len(lines[r]) > 0) {
            last_row = r;
        }
    }
    if (last_row < 0) {
        return 0;
    }

    for (int r = 0; r <= last_row; r++) {
        int len = line_used_len(lines[r]);
        for (int c = 0; c < len && pos < out_max; c++) {
            out[pos++] = (unsigned char)lines[r][c];
        }
        if (r < last_row && pos < out_max) {
            out[pos++] = '\n';
        }
    }
    return pos;
}

static int load_dialog_entries(const minidos_app_api_t* api, dlg_entry_t* entries, int max_entries) {
    char name[16];
    int is_dir = 0;
    int count = 0;
    unsigned int i = 0;

    while (i < 512u && count < max_entries) {
        if (!app_list_entry(api, i, name, &is_dir)) {
            break;
        }
        str_copy(entries[count].name, name, (int)sizeof(entries[count].name));
        entries[count].is_dir = is_dir;
        count++;
        i++;
    }
    return count;
}

static void path_go_root(char* path, int path_size) {
    if (path_size < 4) return;
    path[0] = 'A';
    path[1] = ':';
    path[2] = '\\';
    path[3] = '\0';
}

static void path_go_up(char* path) {
    int len = str_len(path);
    if (len <= 3) {
        return;
    }
    if (path[len - 1] == '\\') {
        len--;
    }
    while (len > 3 && path[len - 1] != '\\') {
        len--;
    }
    path[len] = '\0';
}

static void path_enter_dir(char* path, int path_size, const char* dir_name) {
    int len = str_len(path);
    int i = 0;
    if (len + 2 >= path_size) return;
    if (path[len - 1] != '\\') {
        path[len++] = '\\';
    }
    while (dir_name[i] && len < path_size - 2) {
        path[len++] = dir_name[i++];
    }
    path[len++] = '\\';
    path[len] = '\0';
}

static void draw_dialog_frame(const minidos_app_api_t* api, const char* title, const char* path, int save_as_mode, const char* info, int sw, int sh) {
    int x = 56;
    int y = 54;
    int w = sw - 112;
    int h = sh - 108;

    draw_rect(api, x, y, w, h, 0xFFFFFFu);
    draw_rect(api, x + 2, y + 2, w - 4, h - 4, 0x000080u);
    draw_rect(api, x + 6, y + 6, w - 12, 16, 0xFFFFFFu);
    draw_text(api, x + 10, y + 10, title, 0x000080u, 0xFFFFFFu);
    draw_text(api, x + 160, y + 10, path, 0x000080u, 0xFFFFFFu);

    draw_rect(api, x + 6, y + 28, w - 12, 178, 0x0000AAu);

    if (save_as_mode) {
        draw_rect(api, x + 6, y + 210, w - 12, 16, 0xFFFFFFu);
        draw_text(api, x + 10, y + 214, "Nome:", 0x000080u, 0xFFFFFFu);
    }

    draw_rect(api, x + 6, y + h - 30, w - 12, 16, 0xFFFFFFu);
    draw_text(api, x + 10, y + h - 26, info, 0x000080u, 0xFFFFFFu);
}

static void draw_dialog_list(const minidos_app_api_t* api, dlg_entry_t* entries, int count, int selected, int top, int sw) {
    int x = 56;
    int y = 54;
    int w = sw - 112;
    draw_rect(api, x + 6, y + 28, w - 12, 178, 0x0000AAu);
    for (int row = 0; row < DLG_VISIBLE_ROWS; row++) {
        int idx = top + row;
        unsigned int bg = 0x0000AAu;
        unsigned int fg = 0xFFFFFFu;
        if (idx == selected) {
            bg = 0xFFFFFFu;
            fg = 0x000080u;
        }
        draw_rect(api, x + 8, y + 30 + (row * 16), w - 16, 16, bg);
        if (idx < count) {
            draw_text(api, x + 12, y + 34 + (row * 16), entries[idx].is_dir ? "[DIR]" : "[FIL]", fg, bg);
            draw_text(api, x + 68, y + 34 + (row * 16), entries[idx].name, fg, bg);
        }
    }
}

static void draw_dialog_name_input(const minidos_app_api_t* api, const char* name_input, int sw) {
    int x = 56;
    int y = 54;
    int w = sw - 112;
    draw_rect(api, x + 58, y + 210, w - 64, 16, 0xFFFFFFu);
    draw_text(api, x + 58, y + 214, name_input, 0x000080u, 0xFFFFFFu);
}

static int run_file_dialog(const minidos_app_api_t* api, const char* title, int save_as_mode, char* out_name, int out_name_size, int sw, int sh) {
    dlg_entry_t entries[DLG_MAX_ENTRIES];
    char path[128];
    char name_input[13];
    char info[96];
    int selected = 0;
    int top = 0;
    int count = 0;

    for (int i = 0; i < 32; i++) {
        if (!app_chdir(api, "..")) {
            break;
        }
    }
    path_go_root(path, (int)sizeof(path));
    name_input[0] = '\0';
    str_copy(info, "ENTER abre/seleciona | BACKSPACE sobe | ESC cancela", (int)sizeof(info));
    count = load_dialog_entries(api, entries, DLG_MAX_ENTRIES);
    draw_dialog_frame(api, title, path, save_as_mode, info, sw, sh);
    draw_dialog_list(api, entries, count, selected, top, sw);
    if (save_as_mode) {
        draw_dialog_name_input(api, name_input, sw);
    }

    while (1) {
        unsigned char c;
        int old_selected = selected;
        int old_top = top;
        int refresh_list = 0;
        int refresh_name = 0;

        if (selected >= count && count > 0) selected = count - 1;
        if (selected < 0) selected = 0;
        if (selected < top) top = selected;
        if (selected >= top + DLG_VISIBLE_ROWS) top = selected - DLG_VISIBLE_ROWS + 1;
        if (top < 0) top = 0;
        c = (unsigned char)app_get_char(api);

        if (c == 27) {
            return 0;
        }

        if (c == KEY_UP) {
            if (selected > 0) {
                selected--;
                refresh_list = 1;
            }
        } else if (c == KEY_DOWN) {
            if (selected + 1 < count) {
                selected++;
                refresh_list = 1;
            }
        }

        if (c == '\b' || c == 127) {
            int len = str_len(name_input);
            if (save_as_mode && len > 0) {
                name_input[len - 1] = '\0';
                refresh_name = 1;
            } else if (app_chdir(api, "..")) {
                path_go_up(path);
                count = load_dialog_entries(api, entries, DLG_MAX_ENTRIES);
                selected = 0;
                top = 0;
                draw_dialog_frame(api, title, path, save_as_mode, info, sw, sh);
                refresh_list = 1;
            }
        } else if (c == '\r' || c == '\n') {
            if (save_as_mode && name_input[0] != '\0') {
                str_copy(out_name, name_input, out_name_size);
                return 1;
            }
            if (count <= 0) {
                continue;
            }
            if (entries[selected].is_dir) {
                if (app_chdir(api, entries[selected].name)) {
                    path_enter_dir(path, (int)sizeof(path), entries[selected].name);
                    count = load_dialog_entries(api, entries, DLG_MAX_ENTRIES);
                    selected = 0;
                    top = 0;
                    draw_dialog_frame(api, title, path, save_as_mode, info, sw, sh);
                    refresh_list = 1;
                }
            } else {
                str_copy(out_name, entries[selected].name, out_name_size);
                return 1;
            }
        } else if (save_as_mode && c >= 32 && c <= 126) {
            int len = str_len(name_input);
            if (len < 12) {
                name_input[len] = (char)c;
                name_input[len + 1] = '\0';
                refresh_name = 1;
            }
        }

        if (selected < top) top = selected;
        if (selected >= top + DLG_VISIBLE_ROWS) top = selected - DLG_VISIBLE_ROWS + 1;
        if (top < 0) top = 0;

        if (selected != old_selected || top != old_top) {
            refresh_list = 1;
        }

        if (refresh_list) {
            draw_dialog_list(api, entries, count, selected, top, sw);
        }
        if (save_as_mode && refresh_name) {
            draw_dialog_name_input(api, name_input, sw);
        }
    }
}

int app_main(const minidos_app_api_t* api) {
    char lines[EDIT_ROWS][EDIT_COLS + 1];
    unsigned char file_buffer[EDIT_MAX_FILE_BYTES];
    int sw = 640;
    int sh = 480;
    int cur_row = 0;
    int cur_col = 0;
    int menu_mode = 0;
    int menu_index = 0;
    int menu_item = 0;
    int running = 1;
    char current_file[16];
    int has_current_file = 0;
    int is_new_file = 1;
    int dirty = 0;
    char status[112];

    if (!api) {
        return 1;
    }

    app_gfx_size(api, &sw, &sh);

    for (int i = 0; i < EDIT_ROWS; i++) {
        line_blank(lines[i]);
    }
    str_copy(current_file, DEFAULT_NEW_FILE, (int)sizeof(current_file));
    has_current_file = 1;
    is_new_file = 1;
    dirty = 0;

    draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
    build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
    draw_text(api, 8, sh - 12, status, 0x000080u, 0xFFFFFFu);
    draw_cell(api, lines, cur_row, cur_col, 1);

    while (running) {
        int old_row = cur_row;
        int old_col = cur_col;
        int moved = 0;
        int changed = 0;
        unsigned char c = (unsigned char)app_get_char(api);

        if (c == 27) {
            app_gfx_clear(api, 0x000000u);
            return 0;
        }

        if (c == KEY_ALT_TOGGLE) {
            menu_mode = !menu_mode;
            if (menu_mode) {
                menu_index = 0;
                menu_item = 0;
            }
            draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
            if (menu_mode) {
                build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
            } else {
                build_file_status(status, sizeof(status), current_file);
            }
            draw_text(api, 8, sh - 12, status, 0x000080u, 0xFFFFFFu);
            if (!menu_mode) {
                draw_cell(api, lines, cur_row, cur_col, 1);
            }
            continue;
        }

        if (menu_mode) {
            if (c == KEY_LEFT) {
                menu_index = (menu_index + MENU_COUNT - 1) % MENU_COUNT;
                menu_item = 0;
            } else if (c == KEY_RIGHT) {
                menu_index = (menu_index + 1) % MENU_COUNT;
                menu_item = 0;
            } else if (c == KEY_UP) {
                menu_item = (menu_item + MENU_ITEMS - 1) % MENU_ITEMS;
            } else if (c == KEY_DOWN) {
                menu_item = (menu_item + 1) % MENU_ITEMS;
            } else if (c == '\r' || c == '\n') {
                if (menu_index == 0 && menu_item == 0) {
                    menu_mode = 0;
                    editor_clear(lines, &cur_row, &cur_col);
                    str_copy(current_file, DEFAULT_NEW_FILE, (int)sizeof(current_file));
                    has_current_file = 1;
                    is_new_file = 1;
                    dirty = 0;
                    str_copy(status, "Novo arquivo criado", (int)sizeof(status));
                } else if (menu_index == 0 && menu_item == 1) {
                    char file_name[16];
                    int bytes_read = 0;
                    menu_mode = 0;
                    draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
                    build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
                    draw_status_text(api, status, sw, sh);
                    draw_cell(api, lines, cur_row, cur_col, 1);
                    if (run_file_dialog(api, "Open", 0, file_name, (int)sizeof(file_name), sw, sh)) {
                        bytes_read = app_file_read(api, file_name, file_buffer, (int)sizeof(file_buffer));
                        if (bytes_read >= 0) {
                            editor_import_buffer(lines, &cur_row, &cur_col, file_buffer, bytes_read);
                            str_copy(current_file, file_name, (int)sizeof(current_file));
                            has_current_file = 1;
                            is_new_file = 0;
                            dirty = 0;
                            str_copy(status, "Open: arquivo carregado", (int)sizeof(status));
                        } else {
                            str_copy(status, "Open: falha ao ler arquivo", (int)sizeof(status));
                        }
                    } else {
                        build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
                    }
                } else if (menu_index == 0 && menu_item == 2) {
                    int bytes_to_write = 0;
                    int ok = 0;
                    menu_mode = 0;
                    if (!has_current_file) {
                        str_copy(status, "Save: use Save As primeiro", (int)sizeof(status));
                    } else {
                        bytes_to_write = editor_export_buffer(lines, file_buffer, (int)sizeof(file_buffer));
                        ok = app_file_write(api, current_file, file_buffer, bytes_to_write);
                        if (ok) {
                            dirty = 0;
                            is_new_file = 0;
                        }
                        str_copy(status, ok ? "Save: arquivo gravado" : "Save: falha ao gravar", (int)sizeof(status));
                    }
                } else if (menu_index == 0 && menu_item == 3) {
                    char file_name[16];
                    int bytes_to_write = 0;
                    int ok = 0;
                    menu_mode = 0;
                    draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
                    build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
                    draw_status_text(api, status, sw, sh);
                    draw_cell(api, lines, cur_row, cur_col, 1);
                    if (run_file_dialog(api, "Save As", 1, file_name, (int)sizeof(file_name), sw, sh)) {
                        bytes_to_write = editor_export_buffer(lines, file_buffer, (int)sizeof(file_buffer));
                        ok = app_file_write(api, file_name, file_buffer, bytes_to_write);
                        if (ok) {
                            str_copy(current_file, file_name, (int)sizeof(current_file));
                            has_current_file = 1;
                            is_new_file = 0;
                            dirty = 0;
                            str_copy(status, "Save As: arquivo gravado", (int)sizeof(status));
                        } else {
                            str_copy(status, "Save As: falha ao gravar", (int)sizeof(status));
                        }
                    } else {
                        build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
                    }
                } else if (menu_index == 0 && menu_item == 4) {
                    menu_mode = 0;
                    if (dirty) {
                        int ok = 0;
                        int bytes_to_write = editor_export_buffer(lines, file_buffer, (int)sizeof(file_buffer));
                        if (is_new_file) {
                            char file_name[16];
                            if (run_file_dialog(api, "Save As", 1, file_name, (int)sizeof(file_name), sw, sh)) {
                                ok = app_file_write(api, file_name, file_buffer, bytes_to_write);
                                if (ok) {
                                    str_copy(current_file, file_name, (int)sizeof(current_file));
                                    has_current_file = 1;
                                    is_new_file = 0;
                                    dirty = 0;
                                    running = 0;
                                } else {
                                    str_copy(status, "Exit: falha ao gravar", (int)sizeof(status));
                                }
                            } else {
                                str_copy(status, "Exit cancelado", (int)sizeof(status));
                            }
                        } else {
                            ok = app_file_write(api, current_file, file_buffer, bytes_to_write);
                            if (ok) {
                                dirty = 0;
                                running = 0;
                            } else {
                                str_copy(status, "Exit: falha no Save", (int)sizeof(status));
                            }
                        }
                    } else {
                        running = 0;
                    }
                }
            }

            draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
            if (menu_mode) {
                draw_status_text(api, status, sw, sh);
            } else {
                char file_status[112];
                build_file_status(file_status, (int)sizeof(file_status), current_file);
                draw_status_text(api, file_status, sw, sh);
            }
            if (!menu_mode) {
                draw_cell(api, lines, cur_row, cur_col, 1);
            }
            continue;
        }

        if (c == KEY_UP) {
            if (cur_row > 0) {
                cur_row--;
                moved = 1;
            }
        } else if (c == KEY_DOWN) {
            if (cur_row < EDIT_ROWS - 1) {
                cur_row++;
                moved = 1;
            }
        } else if (c == '\r' || c == '\n') {
            if (cur_row < EDIT_ROWS - 1) {
                cur_row++;
                cur_col = 0;
                moved = 1;
            }
        } else if (c == '\b' || c == 127) {
            if (cur_col > 0) {
                cur_col--;
                lines[cur_row][cur_col] = ' ';
                moved = 1;
                changed = 1;
            } else if (cur_row > 0) {
                int prev_len;
                cur_row--;
                prev_len = line_used_len(lines[cur_row]);
                cur_col = prev_len;
                if (cur_col > 0) {
                    cur_col--;
                    lines[cur_row][cur_col] = ' ';
                    changed = 1;
                }
                moved = 1;
            }
        } else if (c >= 32 && c <= 126) {
            lines[cur_row][cur_col] = (char)c;
            changed = 1;
            if (cur_col < EDIT_COLS - 1) {
                cur_col++;
                moved = 1;
            } else if (cur_row < EDIT_ROWS - 1) {
                cur_col = 0;
                cur_row++;
                moved = 1;
            }
        }

        if (changed) {
            dirty = 1;
            draw_cell(api, lines, old_row, old_col, 0);
        }

        if (moved) {
            draw_cell(api, lines, old_row, old_col, 0);
            draw_cell(api, lines, cur_row, cur_col, 1);
            build_file_status(status, sizeof(status), current_file);
            draw_status_text(api, status, sw, sh);
        } else if (changed) {
            draw_cell(api, lines, cur_row, cur_col, 1);
        }
    }
    app_gfx_clear(api, 0x000000u);
    return 0;
}
