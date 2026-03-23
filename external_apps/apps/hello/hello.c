#include "minidos_app.h"

int app_main(const minidos_app_api_t* api) {
    app_puts(api, "Hello from external app (ELF)\n");
    return 0;
}
