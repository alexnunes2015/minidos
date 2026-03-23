#include "minidos_app.h"

#define BADPTR_ADDR 0x00010000U

int app_main(const minidos_app_api_t* api) {
    int result;

    app_puts(api, "BADP100\n");
    result = app_syscall(api, MINIDOS_SYSCALL_PUTS, BADPTR_ADDR, 0, 0);
    if (result == -1) {
        app_puts(api, "BADP190\n");
        return 0;
    }

    app_puts(api, "BADP900\n");
    return 1;
}
