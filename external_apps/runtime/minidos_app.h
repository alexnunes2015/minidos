#ifndef MINIDOS_APP_H
#define MINIDOS_APP_H

typedef struct {
    int (*syscall)(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2);
} minidos_app_api_t;

enum {
    MINIDOS_SYSCALL_PUTS = 1,
    MINIDOS_SYSCALL_GET_CHAR = 2,
    MINIDOS_SYSCALL_FILE_SIZE = 3,
};

static inline int app_syscall(const minidos_app_api_t* api, unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    if (api && api->syscall) {
        return api->syscall(num, a0, a1, a2);
    }
    return -1;
}

static inline void app_puts(const minidos_app_api_t* api, const char* text) {
    (void)app_syscall(api, MINIDOS_SYSCALL_PUTS, (unsigned int)text, 0, 0);
}

static inline char app_get_char(const minidos_app_api_t* api) {
    int c = app_syscall(api, MINIDOS_SYSCALL_GET_CHAR, 0, 0, 0);
    if (c < 0) {
        return 0;
    }
    return (char)c;
}

static inline int app_file_size(const minidos_app_api_t* api, const char* path) {
    return app_syscall(api, MINIDOS_SYSCALL_FILE_SIZE, (unsigned int)path, 0, 0);
}

#endif
