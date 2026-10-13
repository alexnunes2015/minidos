#include "minidos_app.h"

#define FAULTGROUP_BAD_ADDR 0x00010000U

static void faultgroup_worker(const minidos_app_api_t* api, void* arg) {
    (void)arg;
    app_puts(api, "FGCHILD100\n");
    while (1) {
        app_thread_yield(api);
    }
}

int app_main(const minidos_app_api_t* api) {
    app_thread_create_t spec;
    volatile const unsigned int* bad_address;

    app_puts(api, "FGMAIN100\n");
    spec.name = "fault-worker";
    spec.entry = faultgroup_worker;
    spec.arg = 0;
    if (app_thread_create(api, &spec) < 0) {
        app_puts(api, "FGMAIN900\n");
        return 1;
    }

    app_puts(api, "FGMAIN110\n");
    app_thread_yield(api);
    bad_address = (volatile const unsigned int*)FAULTGROUP_BAD_ADDR;
    (void)*bad_address;
    app_puts(api, "FGMAIN990\n");
    return 1;
}
