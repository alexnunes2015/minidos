#include "minidos_app.h"

#define KEY_UP   0x11
#define KEY_DOWN 0x12
#define UI_MAX_ROWS 20

typedef struct {
    char name[13];
    int is_dir;
} file_item_t;

static void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

static void draw_shell_frame(const minidos_app_api_t* api, int sw, int sh) {
    app_gfx_clear(api, 0x101722u);

    draw_rect(api, 8, 8, sw - 16, sh - 16, 0x0B1019u);
    draw_rect(api, 12, 12, sw - 24, sh - 24, 0x1A2333u);
    draw_rect(api, 12, 12, sw - 24, 2, 0x5E7FB8u);
    draw_rect(api, 12, 14, 2, sh - 26, 0x5E7FB8u);

    draw_rect(api, 20, 20, sw - 40, 24, 0x3F5D93u);
    draw_rect(api, 20, 44, sw - 40, 8, 0x253552u);
    draw_text(api, 28, 28, "MINIDOS DOSSHELL", 0xFFFFFFu, 0x3F5D93u);
    draw_text(api, sw - 180, 28, "A:\\ FILE MANAGER", 0xD6E4FFu, 0x3F5D93u);

    draw_rect(api, 24, 56, sw - 48, sh - 136, 0x162133u);
    draw_rect(api, 24, 56, sw - 48, 2, 0x46628Fu);
    draw_text(api, 32, 64, "ARQUIVOS", 0xC7D9FFu, 0x162133u);

    draw_rect(api, 24, sh - 72, sw - 48, 32, 0x2A3B59u);
    draw_text(api, 32, sh - 64, "V COLA AQUI | SHIFT+V COLA NA PASTA SEL | C/M | R RENOMEAR", 0xF2F7FFu, 0x2A3B59u);
}

static void draw_file_list(const minidos_app_api_t* api, file_item_t* items, int count, int selected, int sw) {
    int list_x = 32;
    int list_y = 72;
    int row_h = 16;
    int list_w = sw - 64;
    int list_h = UI_MAX_ROWS * row_h;
    int row_inner_pad_x = 12;
    int text_baseline_y = 4;
    int type_x = list_x + row_inner_pad_x;
    int name_x = list_x + row_inner_pad_x + 56;

    draw_rect(api, list_x, list_y, list_w, list_h, 0x1A2740u);

    for (int i = 0; i < count && i < UI_MAX_ROWS; i++) {
        int y = list_y + i * row_h;
        if ((i & 1) == 1) {
            draw_rect(api, list_x, y, list_w, row_h, 0x1C2A45u);
        }
        if (i == selected) {
            draw_rect(api, list_x, y, list_w, row_h, 0x7FA8F0u);
            draw_rect(api, list_x, y, 3, row_h, 0xEAF2FFu);
        }
        draw_text(api, type_x, y + text_baseline_y, items[i].is_dir ? "[DIR]" : "[FIL]",
            i == selected ? 0x0F1A2Bu : 0x93AFDFu, i == selected ? 0x7FA8F0u : (((i & 1) == 1) ? 0x1C2A45u : 0x1A2740u));
        draw_text(api, name_x, y + text_baseline_y, items[i].name,
            i == selected ? 0x0F1A2Bu : 0xEAF2FFu, i == selected ? 0x7FA8F0u : (((i & 1) == 1) ? 0x1C2A45u : 0x1A2740u));
    }
}

static void draw_status(const minidos_app_api_t* api, const char* status, int sh) {
    draw_rect(api, 32, sh - 56, 560, 8, 0x2A3B59u);
    draw_text(api, 32, sh - 56, status, 0xFFE9A8u, 0x2A3B59u);
}

static void draw_ui(const minidos_app_api_t* api, file_item_t* items, int count, int selected, const char* status, int sw, int sh, int full_redraw) {
    if (full_redraw) {
        draw_shell_frame(api, sw, sh);
    }
    draw_file_list(api, items, count, selected, sw);
    draw_status(api, status, sh);
}

static int read_line(const minidos_app_api_t* api, char* out, int out_size, file_item_t* items, int count, int selected, const char* prompt, int sw, int sh) {
    int pos = 0;
    if (!out || out_size < 2) {
        return 0;
    }

    draw_ui(api, items, count, selected, prompt, sw, sh, 1);
    draw_rect(api, 96, 176, sw - 192, 88, 0x152033u);
    draw_rect(api, 104, 208, sw - 208, 16, 0x0D1727u);
    draw_text(api, 112, 184, prompt, 0xEAF2FFu, 0x152033u);
    draw_text(api, 112, 208, out, 0xCFE3FFu, 0x0D1727u);

    while (1) {
        char c = app_get_char(api);
        if (c == '\r' || c == '\n') {
            out[pos] = '\0';
            return 1;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                out[pos] = '\0';
            }
        } else if (c >= 32 && c <= 126 && pos < out_size - 1) {
            out[pos++] = c;
            out[pos] = '\0';
        }
        draw_rect(api, 104, 208, sw - 208, 16, 0x0D1727u);
        draw_text(api, 112, 208, out, 0xCFE3FFu, 0x0D1727u);
    }
}

static int load_entries(const minidos_app_api_t* api, file_item_t* items, int max_items) {
    char name[16];
    int is_dir = 0;
    int count = 0;
    for (unsigned int i = 0; i < 256 && count < max_items; i++) {
        if (!app_list_entry(api, i, name, &is_dir)) {
            break;
        }
        str_copy(items[count].name, name, 13);
        items[count].is_dir = is_dir;
        count++;
    }
    return count;
}

int app_main(const minidos_app_api_t* api) {
    file_item_t items[64];
    char status[80];
    int count = 0;
    int selected = 0;
    int sw = 640;
    int sh = 480;
    int need_full_redraw = 1;

    if (!api) {
        return 1;
    }
    app_gfx_size(api, &sw, &sh);

    str_copy(status, "INTERFACE GRAFICA ATIVA.", sizeof(status));

    while (1) {
        char c;
        count = load_entries(api, items, 64);
        if (selected >= count && count > 0) {
            selected = count - 1;
        }
        if (count == 0) {
            selected = 0;
        }
        draw_ui(api, items, count, selected, status, sw, sh, need_full_redraw);
        need_full_redraw = 0;
        c = app_get_char(api);

        if (c == 'q' || c == 'Q') {
            app_gfx_clear(api, 0x000000u);
            return 0;
        }
        if (c == '\b' || c == 127) {
            if (app_chdir(api, "..")) {
                str_copy(status, "SUBIU PARA PASTA PAI.", sizeof(status));
                need_full_redraw = 1;
            } else {
                str_copy(status, "NAO FOI POSSIVEL SUBIR.", sizeof(status));
            }
            continue;
        }
        if (c == 'w' || c == 'W' || c == KEY_UP) {
            if (count > 0 && selected > 0) {
                selected--;
            }
            continue;
        }
        if (c == 's' || c == 'S' || c == KEY_DOWN) {
            if (count > 0 && selected < count - 1) {
                selected++;
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (count > 0 && items[selected].is_dir) {
                if (app_chdir(api, items[selected].name)) {
                    str_copy(status, "PASTA ABERTA.", sizeof(status));
                    need_full_redraw = 1;
                } else {
                    str_copy(status, "FALHA AO ABRIR PASTA.", sizeof(status));
                }
            } else {
                str_copy(status, "SELECIONE UMA PASTA.", sizeof(status));
            }
            continue;
        }

        if (c == 'n' || c == 'N') {
            char name[16];
            name[0] = '\0';
            if (read_line(api, name, sizeof(name), items, count, selected, "NOME DA PASTA (8.3):", sw, sh)) {
                if (app_mkdir(api, name)) {
                    str_copy(status, "PASTA CRIADA.", sizeof(status));
                    need_full_redraw = 1;
                } else {
                    str_copy(status, "FALHA AO CRIAR PASTA.", sizeof(status));
                }
            }
            need_full_redraw = 1;
            continue;
        }

        if (c == 'x' || c == 'X') {
            if (count > 0) {
                if (app_delete_entry(api, items[selected].name)) {
                    str_copy(status, "ITEM REMOVIDO.", sizeof(status));
                    need_full_redraw = 1;
                } else {
                    str_copy(status, "FALHA AO REMOVER (PASTA VAZIA?).", sizeof(status));
                }
            } else {
                str_copy(status, "SELECIONE UM ITEM.", sizeof(status));
            }
            continue;
        }

        if ((c == 'c' || c == 'C') && count > 0 && !items[selected].is_dir) {
            if (app_clip_set(api, items[selected].name, 1)) {
                str_copy(status, "COPIAR SELECIONADO. ESCOLHA PASTA E TECLE V.", sizeof(status));
            } else {
                str_copy(status, "FALHA AO PREPARAR COPIA.", sizeof(status));
            }
            continue;
        }

        if ((c == 'm' || c == 'M') && count > 0 && !items[selected].is_dir) {
            if (app_clip_set(api, items[selected].name, 2)) {
                str_copy(status, "MOVER SELECIONADO. ESCOLHA PASTA E TECLE V.", sizeof(status));
            } else {
                str_copy(status, "FALHA AO PREPARAR MOVE.", sizeof(status));
            }
            continue;
        }

        if (c == 'v') {
            int ok = app_clip_paste(api, "");
            if (ok) {
                str_copy(status, "COLADO NA PASTA ABERTA.", sizeof(status));
                need_full_redraw = 1;
            } else {
                str_copy(status, "FALHA AO COLAR NA PASTA ABERTA.", sizeof(status));
            }
            continue;
        }

        if (c == 'V') {
            if (count > 0 && items[selected].is_dir) {
                int ok = app_clip_paste(api, items[selected].name);
                if (ok) {
                    str_copy(status, "COLADO NA PASTA SELECIONADA.", sizeof(status));
                    need_full_redraw = 1;
                } else {
                    str_copy(status, "FALHA AO COLAR NA PASTA SELECIONADA.", sizeof(status));
                }
            } else {
                str_copy(status, "SHIFT+V REQUER PASTA SELECIONADA.", sizeof(status));
            }
            continue;
        }

        if ((c == 'r' || c == 'R') && count > 0) {
            char new_name[16];
            new_name[0] = '\0';
            if (read_line(api, new_name, sizeof(new_name), items, count, selected, "NOVO NOME (8.3):", sw, sh)) {
                if (app_rename_entry(api, items[selected].name, new_name)) {
                    str_copy(status, "ITEM RENOMEADO.", sizeof(status));
                    need_full_redraw = 1;
                } else {
                    str_copy(status, "FALHA AO RENOMEAR.", sizeof(status));
                }
            }
            need_full_redraw = 1;
            continue;
        }

        str_copy(status, "COMANDO INVALIDO.", sizeof(status));
    }
}
