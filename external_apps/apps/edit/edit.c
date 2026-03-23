#include "minidos_app.h"

#define KEY_UP 0x11
#define KEY_DOWN 0x12
#define KEY_ALT_TOGGLE 0x14
#define KEY_LEFT 0x15
#define KEY_RIGHT 0x16

#define EDIT_COLS 78
#define EDIT_ROWS 24
#define STATUS_HELP "ESC sair | ENTER nova linha | BACKSPACE apaga"
#define STATUS_MENU "MENU ATIVO: ALT sai | ESQ/DIR menu | CIMA/BAIXO item"
#define DEFAULT_NEW_FILE "UNTITLED.TXT"

#define MENU_COUNT 4
#define MENU_ITEMS 5
#define DLG_MAX_ENTRIES 64
#define DLG_VISIBLE_ROWS 10
#define EDIT_MAX_FILE_BYTES (EDIT_ROWS * (EDIT_COLS + 2))
#define CURSOR_BLINK_TICKS 90

static const char* g_menu_labels[MENU_COUNT] = {
    " File ", " Edit ", " Search ", " Help "
};

static const char* g_menu_items[MENU_COUNT][MENU_ITEMS] = {
    { " Novo ", " Abrir ", " Save ", " Save As ", " Exit " },
    { " Copiar ", " Colar ", " Selecionar ", " Desfazer ", " Refazer " },
    { " Localizar ", " Proximo ", " Anterior ", " Ir para ", " Substituir " },
    { " Sobre ", " Teclas ", " Creditos ", " Versao ", " Ajuda " }
};

static const int g_menu_x[MENU_COUNT] = { 8, 56, 120, 184 };

static const unsigned int UI_BG_OUTER = 0x101722u;
static const unsigned int UI_BG_FRAME = 0x0B1019u;
static const unsigned int UI_BG_PANEL = 0x1A2333u;
static const unsigned int UI_ACCENT = 0x5E7FB8u;
static const unsigned int UI_TITLE_BG = 0x3F5D93u;
static const unsigned int UI_TITLE_FG = 0xFFFFFFu;
static const unsigned int UI_TEXT_BG = 0x162133u;
static const unsigned int UI_TEXT_FG = 0xEAF2FFu;
static const unsigned int UI_STATUS_BG = 0x2A3B59u;
static const unsigned int UI_STATUS_FG = 0xFFE9A8u;
static const unsigned int UI_CURSOR_BG = 0x7FA8F0u;
static const unsigned int UI_CURSOR_FG = 0x0F1A2Bu;

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
    unsigned int bar_bg = UI_TITLE_BG;
    unsigned int bar_fg = UI_TITLE_FG;

    draw_rect(api, 20, 20, sw - 40, 24, bar_bg);
    draw_rect(api, 20, 44, sw - 40, 8, 0x253552u);

    for (int i = 0; i < MENU_COUNT; i++) {
        unsigned int fg = bar_fg;
        unsigned int bg = bar_bg;
        if (menu_mode && i == menu_index) {
            fg = UI_CURSOR_FG;
            bg = UI_CURSOR_BG;
        }
        draw_text(api, 28 + g_menu_x[i], 28, g_menu_labels[i], fg, bg);
    }
}

static void draw_menu_popup(const minidos_app_api_t* api, int menu_index, int item_index) {
    int px = 28 + g_menu_x[menu_index];
    int py = 52;
    int pw = 128;
    int row_h = 18;
    int top_pad = 6;
    int ph = (MENU_ITEMS * row_h) + top_pad + 6;

    draw_rect(api, px, py, pw, ph, UI_ACCENT);
    draw_rect(api, px + 1, py + 1, pw - 2, ph - 2, UI_BG_PANEL);

    for (int i = 0; i < MENU_ITEMS; i++) {
        unsigned int fg = UI_TEXT_FG;
        unsigned int bg = UI_BG_PANEL;
        int item_y = py + top_pad + (i * row_h);
        if (i == item_index) {
            fg = UI_CURSOR_FG;
            bg = UI_CURSOR_BG;
        }
        draw_rect(api, px + 4, item_y - 2, pw - 8, row_h - 2, bg);
        draw_text(api, px + 10, item_y + 2, g_menu_items[menu_index][i], fg, bg);
    }
}

static void draw_menu_overlay(const minidos_app_api_t* api, int menu_mode, int menu_index, int item_index, int sw) {
    draw_rect(api, 24, 52, sw - 48, 112, UI_TEXT_BG);
    draw_menu_bar(api, menu_mode, menu_index, sw);
    if (menu_mode) {
        draw_menu_popup(api, menu_index, item_index);
    }
}

static void draw_base_ui(const minidos_app_api_t* api, char lines[EDIT_ROWS][EDIT_COLS + 1], int sw, int sh, int menu_mode, int menu_index, int item_index) {
    app_gfx_clear(api, UI_BG_OUTER);

    draw_rect(api, 8, 8, sw - 16, sh - 16, UI_BG_FRAME);
    draw_rect(api, 12, 12, sw - 24, sh - 24, UI_BG_PANEL);
    draw_rect(api, 12, 12, sw - 24, 2, UI_ACCENT);
    draw_rect(api, 12, 14, 2, sh - 26, UI_ACCENT);

    draw_menu_bar(api, menu_mode, menu_index, sw);

    draw_rect(api, 24, 56, sw - 48, sh - 96, UI_TEXT_BG);
    draw_rect(api, 24, 56, sw - 48, 2, 0x46628Fu);

    for (int row = 0; row < EDIT_ROWS; row++) {
        draw_text(api, 32, 64 + row * 16, lines[row], UI_TEXT_FG, UI_TEXT_BG);
    }

    if (menu_mode) {
        draw_menu_popup(api, menu_index, item_index);
    }

    draw_rect(api, 24, sh - 32, sw - 48, 16, UI_STATUS_BG);
}

static void draw_cell(const minidos_app_api_t* api, char lines[EDIT_ROWS][EDIT_COLS + 1], int row, int col, int is_cursor) {
    char text[2];
    unsigned int fg = UI_TEXT_FG;
    unsigned int bg = UI_TEXT_BG;

    if (row < 0 || row >= EDIT_ROWS || col < 0 || col >= EDIT_COLS) {
        return;
    }

    text[0] = lines[row][col];
    if (text[0] == '\0') {
        text[0] = ' ';
    }
    text[1] = '\0';

    if (is_cursor) {
        fg = UI_CURSOR_FG;
        bg = UI_CURSOR_BG;
    }

    draw_text(api, 32 + (col * 8), 64 + (row * 16), text, fg, bg);
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
    draw_rect(api, 24, sh - 32, sw - 48, 16, UI_STATUS_BG);
    draw_text(api, 32, sh - 28, text, UI_STATUS_FG, UI_STATUS_BG);
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

    draw_rect(api, x, y, w, h, UI_ACCENT);
    draw_rect(api, x + 2, y + 2, w - 4, h - 4, UI_BG_PANEL);
    draw_rect(api, x + 6, y + 6, w - 12, 16, UI_TITLE_BG);
    draw_text(api, x + 10, y + 10, title, UI_TITLE_FG, UI_TITLE_BG);
    draw_text(api, x + 180, y + 10, path, 0xD6E4FFu, UI_TITLE_BG);

    draw_rect(api, x + 6, y + 28, w - 12, 178, UI_TEXT_BG);

    if (save_as_mode) {
        draw_rect(api, x + 6, y + 210, w - 12, 16, UI_BG_PANEL);
        draw_text(api, x + 10, y + 214, "Nome:", UI_TEXT_FG, UI_BG_PANEL);
    }

    draw_rect(api, x + 6, y + h - 30, w - 12, 16, UI_STATUS_BG);
    draw_text(api, x + 10, y + h - 26, info, UI_STATUS_FG, UI_STATUS_BG);
}

static void draw_dialog_list(const minidos_app_api_t* api, dlg_entry_t* entries, int count, int selected, int top, int sw) {
    int x = 56;
    int y = 54;
    int w = sw - 112;
    draw_rect(api, x + 6, y + 28, w - 12, 178, UI_TEXT_BG);
    for (int row = 0; row < DLG_VISIBLE_ROWS; row++) {
        int idx = top + row;
        unsigned int bg = UI_TEXT_BG;
        unsigned int fg = UI_TEXT_FG;
        if (idx == selected) {
            bg = UI_CURSOR_BG;
            fg = UI_CURSOR_FG;
        } else if ((row & 1) == 1) {
            bg = 0x1C2A45u;
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
    draw_rect(api, x + 58, y + 210, w - 64, 16, UI_TEXT_BG);
    draw_text(api, x + 58, y + 214, name_input, UI_TEXT_FG, UI_TEXT_BG);
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

static int run_unsaved_dialog(const minidos_app_api_t* api, int sw, int sh) {
    int x = (sw - 360) / 2;
    int y = (sh - 120) / 2;

    draw_rect(api, x, y, 360, 120, UI_ACCENT);
    draw_rect(api, x + 2, y + 2, 356, 116, UI_BG_PANEL);
    draw_rect(api, x + 6, y + 6, 348, 18, UI_TITLE_BG);
    draw_text(api, x + 12, y + 10, "Alteracoes pendentes", UI_TITLE_FG, UI_TITLE_BG);
    draw_text(api, x + 12, y + 38, "Guardar antes de sair?", UI_TEXT_FG, UI_BG_PANEL);
    draw_rect(api, x + 10, y + 84, 340, 20, UI_STATUS_BG);
    draw_text(api, x + 12, y + 88, "S=Guardar | N=Nao guardar | ESC=Cancelar", UI_STATUS_FG, UI_STATUS_BG);

    while (1) {
        unsigned char c = (unsigned char)app_get_char(api);
        if (c == 's' || c == 'S') {
            return 1;
        }
        if (c == 'n' || c == 'N') {
            return 2;
        }
        if (c == 27) {
            return 0;
        }
    }
}

static void run_about_dialog(const minidos_app_api_t* api, int sw, int sh) {
    int x = (sw - 430) / 2;
    int y = (sh - 220) / 2;

    draw_rect(api, x, y, 430, 220, UI_ACCENT);
    draw_rect(api, x + 2, y + 2, 426, 216, UI_BG_PANEL);

    draw_rect(api, x + 8, y + 8, 414, 20, UI_TITLE_BG);
    draw_text(api, x + 12, y + 12, "About MiniDOS EDIT", UI_TITLE_FG, UI_TITLE_BG);

    draw_rect(api, x + 10, y + 36, 410, 144, UI_TEXT_BG);
    draw_text(api, x + 14, y + 46, "MiniDOS EDIT", UI_TEXT_FG, UI_TEXT_BG);
    draw_text(api, x + 14, y + 62, "Editor de texto 80x25 para MiniDOS", UI_TEXT_FG, UI_TEXT_BG);
    draw_text(api, x + 14, y + 86, "Versao: 0.1 template", UI_TEXT_FG, UI_TEXT_BG);
    draw_text(api, x + 14, y + 102, "UI: File, Edit, Search, Help", UI_TEXT_FG, UI_TEXT_BG);
    draw_text(api, x + 14, y + 126, "Atalhos: ALT menu | ESC sair", UI_TEXT_FG, UI_TEXT_BG);
    draw_text(api, x + 14, y + 142, "Autor: MiniDOS team", UI_TEXT_FG, UI_TEXT_BG);

    draw_rect(api, x + 10, y + 188, 410, 18, UI_STATUS_BG);
    draw_text(api, x + 14, y + 192, "ESC ou ENTER para fechar", UI_STATUS_FG, UI_STATUS_BG);

    while (1) {
        unsigned char c = (unsigned char)app_get_char(api);
        if (c == 27 || c == '\r' || c == '\n') {
            return;
        }
    }
}

static int handle_exit_request(const minidos_app_api_t* api,
                               char lines[EDIT_ROWS][EDIT_COLS + 1],
                               unsigned char* file_buffer,
                               int file_buffer_size,
                               char* current_file,
                               int current_file_size,
                               int* has_current_file,
                               int* is_new_file,
                               int* dirty,
                               char* status,
                               int status_size,
                               int sw,
                               int sh) {
    if (!dirty || !*dirty) {
        return 1;
    }

    {
        int choice = run_unsaved_dialog(api, sw, sh);
        if (choice == 0) {
            str_copy(status, "Saida cancelada", status_size);
            return 0;
        }
        if (choice == 2) {
            return 1;
        }
    }

    {
        int bytes_to_write = editor_export_buffer(lines, file_buffer, file_buffer_size);
        int ok = 0;

        if (*is_new_file || !*has_current_file) {
            char file_name[16];
            if (!run_file_dialog(api, "Save As", 1, file_name, (int)sizeof(file_name), sw, sh)) {
                str_copy(status, "Saida cancelada", status_size);
                return 0;
            }
            ok = app_file_write(api, file_name, file_buffer, bytes_to_write);
            if (ok) {
                str_copy(current_file, file_name, current_file_size);
                *has_current_file = 1;
                *is_new_file = 0;
                *dirty = 0;
                return 1;
            }
            str_copy(status, "Exit: falha ao gravar", status_size);
            return 0;
        }

        ok = app_file_write(api, current_file, file_buffer, bytes_to_write);
        if (ok) {
            *dirty = 0;
            return 1;
        }
        str_copy(status, "Exit: falha no Save", status_size);
        return 0;
    }
}

static void lines_copy(char dst[EDIT_ROWS][EDIT_COLS + 1], char src[EDIT_ROWS][EDIT_COLS + 1]) {
    for (int r = 0; r < EDIT_ROWS; r++) {
        for (int c = 0; c <= EDIT_COLS; c++) {
            dst[r][c] = src[r][c];
        }
    }
}

static void push_undo_snapshot(char lines[EDIT_ROWS][EDIT_COLS + 1],
                               char undo_lines[EDIT_ROWS][EDIT_COLS + 1],
                               int* can_undo,
                               int* can_redo) {
    lines_copy(undo_lines, lines);
    if (can_undo) {
        *can_undo = 1;
    }
    if (can_redo) {
        *can_redo = 0;
    }
}

static void set_line_from_text(char* line, const char* text) {
    int i = 0;
    line_blank(line);
    while (text[i] && i < EDIT_COLS) {
        line[i] = text[i];
        i++;
    }
}

static int copy_current_line(char lines[EDIT_ROWS][EDIT_COLS + 1], int cur_row, char* out_clip, int out_clip_size) {
    int len = 0;
    if (!out_clip || out_clip_size < 2 || cur_row < 0 || cur_row >= EDIT_ROWS) {
        return 0;
    }
    len = line_used_len(lines[cur_row]);
    if (len <= 0) {
        return 0;
    }
    if (len > out_clip_size - 1) {
        len = out_clip_size - 1;
    }
    for (int i = 0; i < len; i++) {
        out_clip[i] = lines[cur_row][i];
    }
    out_clip[len] = '\0';
    return 1;
}

static int paste_on_current_line(char lines[EDIT_ROWS][EDIT_COLS + 1], int* cur_row, int* cur_col, const char* clip_text) {
    int col = 0;
    int i = 0;
    if (!cur_row || !cur_col || !clip_text) {
        return 0;
    }
    if (*cur_row < 0 || *cur_row >= EDIT_ROWS || *cur_col < 0 || *cur_col >= EDIT_COLS) {
        return 0;
    }
    col = *cur_col;
    while (clip_text[i] && col < EDIT_COLS) {
        lines[*cur_row][col++] = clip_text[i++];
    }
    if (i <= 0) {
        return 0;
    }
    *cur_col = col < EDIT_COLS ? col : (EDIT_COLS - 1);
    return 1;
}

static int char_lower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static int str_contains_at_ci(const char* hay, int hay_len, int pos, const char* needle, int needle_len) {
    if (pos < 0 || needle_len <= 0 || pos + needle_len > hay_len) {
        return 0;
    }
    for (int i = 0; i < needle_len; i++) {
        if (char_lower(hay[pos + i]) != char_lower(needle[i])) {
            return 0;
        }
    }
    return 1;
}

static int find_text_forward(char lines[EDIT_ROWS][EDIT_COLS + 1],
                             const char* query,
                             int start_row,
                             int start_col,
                             int* out_row,
                             int* out_col) {
    int qlen = str_len(query);
    if (qlen <= 0) {
        return 0;
    }
    for (int r = start_row; r < EDIT_ROWS; r++) {
        int begin = (r == start_row) ? start_col : 0;
        int used = line_used_len(lines[r]);
        if (begin < 0) begin = 0;
        if (used < qlen) continue;
        for (int c = begin; c <= used - qlen; c++) {
            if (str_contains_at_ci(lines[r], used, c, query, qlen)) {
                *out_row = r;
                *out_col = c;
                return 1;
            }
        }
    }
    return 0;
}

static int find_text_backward(char lines[EDIT_ROWS][EDIT_COLS + 1],
                              const char* query,
                              int start_row,
                              int start_col,
                              int* out_row,
                              int* out_col) {
    int qlen = str_len(query);
    if (qlen <= 0) {
        return 0;
    }
    for (int r = start_row; r >= 0; r--) {
        int used = line_used_len(lines[r]);
        int begin = used - qlen;
        if (begin < 0) {
            continue;
        }
        if (r == start_row && begin > start_col) {
            begin = start_col;
        }
        for (int c = begin; c >= 0; c--) {
            if (str_contains_at_ci(lines[r], used, c, query, qlen)) {
                *out_row = r;
                *out_col = c;
                return 1;
            }
        }
    }
    return 0;
}

static int parse_positive_int(const char* text) {
    int value = 0;
    int i = 0;
    if (!text || !text[0]) {
        return -1;
    }
    while (text[i]) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = (value * 10) + (text[i] - '0');
        i++;
    }
    return value;
}

static int run_text_input_dialog(const minidos_app_api_t* api,
                                 const char* title,
                                 const char* prompt,
                                 char* value,
                                 int value_size,
                                 int sw,
                                 int sh) {
    int x = (sw - 440) / 2;
    int y = (sh - 140) / 2;
    if (!value || value_size < 2) {
        return 0;
    }

    draw_rect(api, x, y, 440, 140, UI_ACCENT);
    draw_rect(api, x + 2, y + 2, 436, 136, UI_BG_PANEL);
    draw_rect(api, x + 8, y + 8, 424, 18, UI_TITLE_BG);
    draw_text(api, x + 12, y + 12, title, UI_TITLE_FG, UI_TITLE_BG);
    draw_text(api, x + 12, y + 38, prompt, UI_TEXT_FG, UI_BG_PANEL);
    draw_rect(api, x + 10, y + 58, 420, 18, UI_TEXT_BG);
    draw_text(api, x + 12, y + 62, value, UI_TEXT_FG, UI_TEXT_BG);
    draw_rect(api, x + 10, y + 110, 420, 16, UI_STATUS_BG);
    draw_text(api, x + 12, y + 114, "ENTER confirma | ESC cancela", UI_STATUS_FG, UI_STATUS_BG);

    while (1) {
        unsigned char c = (unsigned char)app_get_char(api);
        int len = str_len(value);
        if (c == 27) {
            return 0;
        }
        if (c == '\r' || c == '\n') {
            return 1;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                value[len - 1] = '\0';
            }
        } else if (c >= 32 && c <= 126) {
            if (len < value_size - 1) {
                value[len] = (char)c;
                value[len + 1] = '\0';
            }
        }
        draw_rect(api, x + 10, y + 58, 420, 18, UI_TEXT_BG);
        draw_text(api, x + 12, y + 62, value, UI_TEXT_FG, UI_TEXT_BG);
    }
}

static int replace_all_occurrences(char lines[EDIT_ROWS][EDIT_COLS + 1], const char* find_text, const char* replace_text) {
    int replacements = 0;
    int find_len = str_len(find_text);
    int repl_len = str_len(replace_text);

    if (find_len <= 0) {
        return 0;
    }

    for (int r = 0; r < EDIT_ROWS; r++) {
        char merged[EDIT_COLS + 1];
        int out_pos = 0;
        int used = line_used_len(lines[r]);
        int changed = 0;

        for (int i = 0; i < used && out_pos < EDIT_COLS; ) {
            if (i + find_len <= used && str_contains_at_ci(lines[r], used, i, find_text, find_len)) {
                for (int j = 0; j < repl_len && out_pos < EDIT_COLS; j++) {
                    merged[out_pos++] = replace_text[j];
                }
                i += find_len;
                replacements++;
                changed = 1;
            } else {
                merged[out_pos++] = lines[r][i++];
            }
        }
        merged[out_pos] = '\0';
        if (changed) {
            set_line_from_text(lines[r], merged);
        }
    }
    return replacements;
}

int app_main(const minidos_app_api_t* api) {
    char lines[EDIT_ROWS][EDIT_COLS + 1];
    char undo_lines[EDIT_ROWS][EDIT_COLS + 1];
    char redo_lines[EDIT_ROWS][EDIT_COLS + 1];
    unsigned char file_buffer[EDIT_MAX_FILE_BYTES];
    char clipboard_line[EDIT_COLS + 1];
    char last_search[32];
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
    int can_undo = 0;
    int can_redo = 0;
    int has_clipboard_line = 0;
    int cursor_visible = 1;
    unsigned int last_blink_tick = 0;
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
    clipboard_line[0] = '\0';
    last_search[0] = '\0';

    draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
    build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
    draw_status_text(api, status, sw, sh);
    draw_cell(api, lines, cur_row, cur_col, cursor_visible);
    last_blink_tick = app_get_ticks(api);

    while (running) {
        int old_row = cur_row;
        int old_col = cur_col;
        int moved = 0;
        int changed = 0;
        int snapshot_taken = 0;
        int have_char = 0;
        unsigned char c = 0;

        while (!have_char) {
            char in = 0;
            if (app_get_char_nonblock(api, &in)) {
                c = (unsigned char)in;
                have_char = 1;
                break;
            }

            if (!menu_mode) {
                unsigned int now = app_get_ticks(api);
                if ((unsigned int)(now - last_blink_tick) >= CURSOR_BLINK_TICKS) {
                    last_blink_tick = now;
                    cursor_visible = !cursor_visible;
                    draw_cell(api, lines, cur_row, cur_col, cursor_visible);
                }
            }
        }

        if (!menu_mode && !cursor_visible) {
            cursor_visible = 1;
            last_blink_tick = app_get_ticks(api);
            draw_cell(api, lines, cur_row, cur_col, 1);
        }

        if (c == 27) {
            if (handle_exit_request(api, lines, file_buffer, (int)sizeof(file_buffer),
                                    current_file, (int)sizeof(current_file),
                                    &has_current_file, &is_new_file, &dirty,
                                    status, (int)sizeof(status), sw, sh)) {
                app_gfx_clear(api, 0x000000u);
                return 0;
            }
            draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
            draw_status_text(api, status, sw, sh);
            draw_cell(api, lines, cur_row, cur_col, 1);
            continue;
        }

        if (c == KEY_ALT_TOGGLE) {
            menu_mode = !menu_mode;
            if (menu_mode) {
                menu_index = 0;
                menu_item = 0;
                cursor_visible = 0;
            }
            draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
            if (menu_mode) {
                build_status(status, sizeof(status), cur_row, cur_col, menu_mode);
            } else {
                build_file_status(status, sizeof(status), current_file);
                cursor_visible = 1;
            }
            draw_status_text(api, status, sw, sh);
            if (!menu_mode) {
                draw_cell(api, lines, cur_row, cur_col, 1);
            }
            continue;
        }

        if (menu_mode) {
            int menu_redraw = 0;
            int need_full_redraw = 0;

            if (c == KEY_LEFT) {
                menu_index = (menu_index + MENU_COUNT - 1) % MENU_COUNT;
                menu_item = 0;
                menu_redraw = 1;
            } else if (c == KEY_RIGHT) {
                menu_index = (menu_index + 1) % MENU_COUNT;
                menu_item = 0;
                menu_redraw = 1;
            } else if (c == KEY_UP) {
                menu_item = (menu_item + MENU_ITEMS - 1) % MENU_ITEMS;
                menu_redraw = 1;
            } else if (c == KEY_DOWN) {
                menu_item = (menu_item + 1) % MENU_ITEMS;
                menu_redraw = 1;
            } else if (c == '\r' || c == '\n') {
                if (menu_index == 0 && menu_item == 0) {
                    menu_mode = 0;
                    push_undo_snapshot(lines, undo_lines, &can_undo, &can_redo);
                    editor_clear(lines, &cur_row, &cur_col);
                    str_copy(current_file, DEFAULT_NEW_FILE, (int)sizeof(current_file));
                    has_current_file = 1;
                    is_new_file = 1;
                    dirty = 0;
                    str_copy(status, "Novo arquivo criado", (int)sizeof(status));
                    need_full_redraw = 1;
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
                            push_undo_snapshot(lines, undo_lines, &can_undo, &can_redo);
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
                    need_full_redraw = 1;
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
                    need_full_redraw = 1;
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
                    need_full_redraw = 1;
                } else if (menu_index == 0 && menu_item == 4) {
                    menu_mode = 0;
                    if (handle_exit_request(api, lines, file_buffer, (int)sizeof(file_buffer),
                                            current_file, (int)sizeof(current_file),
                                            &has_current_file, &is_new_file, &dirty,
                                            status, (int)sizeof(status), sw, sh)) {
                        running = 0;
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 1 && menu_item == 0) {
                    menu_mode = 0;
                    if (copy_current_line(lines, cur_row, clipboard_line, (int)sizeof(clipboard_line))) {
                        has_clipboard_line = 1;
                        str_copy(status, "Linha copiada", (int)sizeof(status));
                    } else {
                        str_copy(status, "Copiar: linha vazia", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 1 && menu_item == 1) {
                    menu_mode = 0;
                    if (!has_clipboard_line) {
                        str_copy(status, "Colar: clipboard vazio", (int)sizeof(status));
                    } else {
                        push_undo_snapshot(lines, undo_lines, &can_undo, &can_redo);
                        if (paste_on_current_line(lines, &cur_row, &cur_col, clipboard_line)) {
                            dirty = 1;
                            str_copy(status, "Linha colada", (int)sizeof(status));
                        } else {
                            str_copy(status, "Colar: sem alteracao", (int)sizeof(status));
                        }
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 1 && menu_item == 2) {
                    menu_mode = 0;
                    if (editor_export_buffer(lines, file_buffer, (int)sizeof(file_buffer)) > 0) {
                        str_copy(status, "Selecionar: documento ativo", (int)sizeof(status));
                    } else {
                        str_copy(status, "Selecionar: documento vazio", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 1 && menu_item == 3) {
                    menu_mode = 0;
                    if (!can_undo) {
                        str_copy(status, "Desfazer: nada a desfazer", (int)sizeof(status));
                    } else {
                        lines_copy(redo_lines, lines);
                        can_redo = 1;
                        lines_copy(lines, undo_lines);
                        can_undo = 0;
                        dirty = 1;
                        str_copy(status, "Desfazer executado", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 1 && menu_item == 4) {
                    menu_mode = 0;
                    if (!can_redo) {
                        str_copy(status, "Refazer: nada a refazer", (int)sizeof(status));
                    } else {
                        lines_copy(undo_lines, lines);
                        can_undo = 1;
                        lines_copy(lines, redo_lines);
                        can_redo = 0;
                        dirty = 1;
                        str_copy(status, "Refazer executado", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 2 && menu_item == 0) {
                    char query[32];
                    int found_row = 0;
                    int found_col = 0;
                    menu_mode = 0;
                    query[0] = '\0';
                    if (run_text_input_dialog(api, "Localizar", "Texto:", query, (int)sizeof(query), sw, sh) && query[0]) {
                        if (find_text_forward(lines, query, 0, 0, &found_row, &found_col)) {
                            str_copy(last_search, query, (int)sizeof(last_search));
                            cur_row = found_row;
                            cur_col = found_col;
                            str_copy(status, "Localizar: ocorrencia encontrada", (int)sizeof(status));
                        } else {
                            str_copy(status, "Localizar: sem resultados", (int)sizeof(status));
                        }
                    } else {
                        str_copy(status, "Localizar cancelado", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 2 && menu_item == 1) {
                    int found_row = 0;
                    int found_col = 0;
                    menu_mode = 0;
                    if (!last_search[0]) {
                        str_copy(status, "Proximo: sem termo de pesquisa", (int)sizeof(status));
                    } else if (find_text_forward(lines, last_search, cur_row, cur_col + 1, &found_row, &found_col) ||
                               find_text_forward(lines, last_search, 0, 0, &found_row, &found_col)) {
                        cur_row = found_row;
                        cur_col = found_col;
                        str_copy(status, "Proximo: ocorrencia encontrada", (int)sizeof(status));
                    } else {
                        str_copy(status, "Proximo: sem resultados", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 2 && menu_item == 2) {
                    int found_row = 0;
                    int found_col = 0;
                    menu_mode = 0;
                    if (!last_search[0]) {
                        str_copy(status, "Anterior: sem termo de pesquisa", (int)sizeof(status));
                    } else if (find_text_backward(lines, last_search, cur_row, cur_col - 1, &found_row, &found_col) ||
                               find_text_backward(lines, last_search, EDIT_ROWS - 1, EDIT_COLS - 1, &found_row, &found_col)) {
                        cur_row = found_row;
                        cur_col = found_col;
                        str_copy(status, "Anterior: ocorrencia encontrada", (int)sizeof(status));
                    } else {
                        str_copy(status, "Anterior: sem resultados", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 2 && menu_item == 3) {
                    char line_text[8];
                    int target = -1;
                    menu_mode = 0;
                    line_text[0] = '\0';
                    if (run_text_input_dialog(api, "Ir para", "Linha (1-24):", line_text, (int)sizeof(line_text), sw, sh)) {
                        target = parse_positive_int(line_text);
                        if (target >= 1 && target <= EDIT_ROWS) {
                            cur_row = target - 1;
                            if (cur_col >= EDIT_COLS) {
                                cur_col = EDIT_COLS - 1;
                            }
                            str_copy(status, "Cursor movido", (int)sizeof(status));
                        } else {
                            str_copy(status, "Ir para: linha invalida", (int)sizeof(status));
                        }
                    } else {
                        str_copy(status, "Ir para cancelado", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                } else if (menu_index == 2 && menu_item == 4) {
                    char find_text[32];
                    char replace_text[32];
                    int replaced = 0;
                    menu_mode = 0;
                    find_text[0] = '\0';
                    replace_text[0] = '\0';
                    if (!run_text_input_dialog(api, "Substituir", "Texto a localizar:", find_text, (int)sizeof(find_text), sw, sh) || !find_text[0]) {
                        str_copy(status, "Substituir cancelado", (int)sizeof(status));
                        need_full_redraw = 1;
                    } else if (!run_text_input_dialog(api, "Substituir", "Novo texto:", replace_text, (int)sizeof(replace_text), sw, sh)) {
                        str_copy(status, "Substituir cancelado", (int)sizeof(status));
                        need_full_redraw = 1;
                    } else {
                        push_undo_snapshot(lines, undo_lines, &can_undo, &can_redo);
                        replaced = replace_all_occurrences(lines, find_text, replace_text);
                        if (replaced > 0) {
                            dirty = 1;
                            str_copy(last_search, find_text, (int)sizeof(last_search));
                            str_copy(status, "Substituir: alteracoes aplicadas", (int)sizeof(status));
                        } else {
                            str_copy(status, "Substituir: sem ocorrencias", (int)sizeof(status));
                        }
                        need_full_redraw = 1;
                    }
                } else if (menu_index == 3) {
                    menu_mode = 0;
                    if (menu_item == 0) {
                        run_about_dialog(api, sw, sh);
                        str_copy(status, "About fechado", (int)sizeof(status));
                    } else if (menu_item == 1) {
                        str_copy(status, "Teclas: ALT menu | ESC sair | setas mover", (int)sizeof(status));
                    } else if (menu_item == 2) {
                        str_copy(status, "Creditos: MiniDOS team", (int)sizeof(status));
                    } else if (menu_item == 3) {
                        str_copy(status, "Versao: 0.1 template", (int)sizeof(status));
                    } else {
                        str_copy(status, "Ajuda: use File para abrir/gravar", (int)sizeof(status));
                    }
                    need_full_redraw = 1;
                }
            }

            if (menu_mode && menu_redraw) {
                draw_menu_overlay(api, menu_mode, menu_index, menu_item, sw);
                draw_status_text(api, status, sw, sh);
            } else if (!menu_mode && running) {
                cursor_visible = 1;
                draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
                draw_status_text(api, status, sw, sh);
                draw_cell(api, lines, cur_row, cur_col, 1);
            } else if (menu_mode && !menu_redraw && need_full_redraw) {
                draw_base_ui(api, lines, sw, sh, menu_mode, menu_index, menu_item);
                draw_status_text(api, status, sw, sh);
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
                if (!snapshot_taken) {
                    push_undo_snapshot(lines, undo_lines, &can_undo, &can_redo);
                    snapshot_taken = 1;
                }
                cur_col--;
                lines[cur_row][cur_col] = ' ';
                moved = 1;
                changed = 1;
            } else if (cur_row > 0) {
                int prev_len;
                if (!snapshot_taken) {
                    push_undo_snapshot(lines, undo_lines, &can_undo, &can_redo);
                    snapshot_taken = 1;
                }
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
            if (!snapshot_taken) {
                push_undo_snapshot(lines, undo_lines, &can_undo, &can_redo);
                snapshot_taken = 1;
            }
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
            draw_cell(api, lines, cur_row, cur_col, cursor_visible);
            build_file_status(status, sizeof(status), current_file);
            draw_status_text(api, status, sw, sh);
        } else if (changed) {
            draw_cell(api, lines, cur_row, cur_col, cursor_visible);
        }
    }
    app_gfx_clear(api, 0x000000u);
    return 0;
}
