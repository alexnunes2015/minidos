#ifndef MINIDOS_APP_H
#define MINIDOS_APP_H

typedef struct {
    void (*puts)(const char* text);
    char (*get_char)(void);
} minidos_app_api_t;

static inline void app_puts(const minidos_app_api_t* api, const char* text) {
    if (api && api->puts) {
        api->puts(text);
    }
}

static inline char app_get_char(const minidos_app_api_t* api) {
    if (api && api->get_char) {
        return api->get_char();
    }
    return 0;
}

#endif
