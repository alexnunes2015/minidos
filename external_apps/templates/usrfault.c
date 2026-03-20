#include "minidos_app.h"

#define USER_FAULT_ADDR 0x00010000U

int app_main(const minidos_app_api_t* api) {
    volatile const unsigned int* probe = (const volatile unsigned int*)USER_FAULT_ADDR;

    app_puts(api, "USRF100\n");
    (void)*probe;
    app_puts(api, "USRF900\n");
    return 1;
}
