#include "minidos_app.h"

#define PTBIG_BUFFER_BYTES 0x00120000U
#define PTBIG_STEP_BYTES   0x00020000U

static volatile unsigned char g_big_buffer[PTBIG_BUFFER_BYTES];

int app_main(const minidos_app_api_t* api) {
    unsigned int checksum = 0;

    app_puts(api, "PTBIG100 start\n");

    for (unsigned int i = 0; i < PTBIG_BUFFER_BYTES; i += PTBIG_STEP_BYTES) {
        unsigned char value = (unsigned char)(0x31U + (i >> 17));
        g_big_buffer[i] = value;
        checksum += g_big_buffer[i];
    }

    g_big_buffer[PTBIG_BUFFER_BYTES - 1U] = 0x5AU;
    checksum += g_big_buffer[PTBIG_BUFFER_BYTES - 1U];

    if (g_big_buffer[0] != 0x31U || g_big_buffer[PTBIG_BUFFER_BYTES - 1U] != 0x5AU) {
        app_puts(api, "PTBIG900 memory check failed\n");
        return 1;
    }

    if (checksum == 0U) {
        app_puts(api, "PTBIG901 checksum failed\n");
        return 1;
    }

    app_puts(api, "PTBIG190 large elf ok\n");
    return 0;
}
