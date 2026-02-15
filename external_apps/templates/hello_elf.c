#include "minidos_app.h"

int app_main(const minidos_app_api_t* api) {
    app_puts(api, "hello_elf: running\n");
    return 0;
}
