#include "minidos_app.h"

#define OLD_APP_WINDOW_ADDR 0x00200000U

int app_main(const minidos_app_api_t* api) {
    volatile const unsigned int* probe = (const volatile unsigned int*)OLD_APP_WINDOW_ADDR;

    app_puts(api, "OLDM100\n");
    (void)*probe;
    app_puts(api, "OLDM900\n");
    return 1;
}
