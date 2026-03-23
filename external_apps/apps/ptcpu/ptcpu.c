#include "minidos_app.h"

#define PTCPU_TICKS 180U

static volatile unsigned int g_mix = 0x13579BDFu;

static void append_text(char* out, int* pos, const char* text) {
    int i = 0;

    while (out && pos && text && text[i] != '\0') {
        out[(*pos)++] = text[i++];
    }
    if (out && pos) {
        out[*pos] = '\0';
    }
}

static void append_uint(char* out, int* pos, unsigned int value) {
    char tmp[16];
    int len = 0;

    if (!out || !pos) {
        return;
    }
    if (value == 0U) {
        out[(*pos)++] = '0';
        out[*pos] = '\0';
        return;
    }

    while (value > 0U && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (len > 0) {
        out[(*pos)++] = tmp[--len];
    }
    out[*pos] = '\0';
}

static void emit_done(const minidos_app_api_t* api, unsigned int ticks, unsigned int loops) {
    char line[96];
    int pos = 0;

    append_text(line, &pos, "PTCPU190 ticks=");
    append_uint(line, &pos, ticks);
    append_text(line, &pos, " loops=");
    append_uint(line, &pos, loops);
    append_text(line, &pos, " mix=");
    append_uint(line, &pos, g_mix & 0xFFFFU);
    append_text(line, &pos, "\n");
    app_puts(api, line);
}

int app_main(const minidos_app_api_t* api) {
    unsigned int start = app_get_ticks(api);
    unsigned int loops = 0;

    app_puts(api, "PTCPU100 start\n");

    while ((unsigned int)(app_get_ticks(api) - start) < PTCPU_TICKS) {
        for (unsigned int i = 0; i < 4096U; i++) {
            g_mix = (g_mix * 1664525U) + 1013904223U + loops + i;
            g_mix ^= (g_mix >> 11);
            loops++;
        }
    }

    emit_done(api, (unsigned int)(app_get_ticks(api) - start), loops);
    return 0;
}
