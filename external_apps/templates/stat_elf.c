#include "minidos_app.h"

static void append_uint(char* out, int* pos, unsigned int value) {
    char tmp[16];
    int len = 0;
    if (value == 0) {
        out[(*pos)++] = '0';
        return;
    }
    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (int i = len - 1; i >= 0; i--) {
        out[(*pos)++] = tmp[i];
    }
}

int app_main(const minidos_app_api_t* api) {
    char msg[96];
    int pos = 0;
    int size = app_file_size(api, "HELLOELF.ELF");

    if (size < 0) {
        app_puts(api, "stat_elf: HELLOELF.ELF not found\n");
        return 1;
    }

    msg[pos++] = 's';
    msg[pos++] = 't';
    msg[pos++] = 'a';
    msg[pos++] = 't';
    msg[pos++] = '_';
    msg[pos++] = 'e';
    msg[pos++] = 'l';
    msg[pos++] = 'f';
    msg[pos++] = ':';
    msg[pos++] = ' ';
    append_uint(msg, &pos, (unsigned int)size);
    msg[pos++] = ' ';
    msg[pos++] = 'b';
    msg[pos++] = 'y';
    msg[pos++] = 't';
    msg[pos++] = 'e';
    msg[pos++] = 's';
    msg[pos++] = '\n';
    msg[pos] = '\0';

    app_puts(api, msg);
    return 0;
}
