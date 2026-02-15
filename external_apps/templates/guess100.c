#include "minidos_app.h"

static void put_char(const minidos_app_api_t* api, char c) {
    char s[2];
    s[0] = c;
    s[1] = '\0';
    app_puts(api, s);
}

static void put_uint(const minidos_app_api_t* api, unsigned int value) {
    char tmp[16];
    int len = 0;

    if (value == 0) {
        put_char(api, '0');
        return;
    }

    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (len > 0) {
        put_char(api, tmp[--len]);
    }
}

static int read_line(const minidos_app_api_t* api, char* out, int max_len, unsigned int* seed) {
    int len = 0;

    while (1) {
        char c = app_get_char(api);
        if (seed) {
            *seed = (*seed * 1664525u) + (unsigned int)(unsigned char)c + 1013904223u;
        }

        if (c == '\r' || c == '\n') {
            put_char(api, '\n');
            out[len] = '\0';
            return 1;
        }

        if (c == 8 || c == 127) {
            if (len > 0) {
                len--;
                app_puts(api, "\b \b");
            }
            continue;
        }

        if (len < max_len - 1 && c >= 32 && c <= 126) {
            out[len++] = c;
            put_char(api, c);
        }
    }
}

static int streq(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int parse_guess(const char* text, int* out_value) {
    int i = 0;
    int value = 0;

    if (!text[0]) {
        return 0;
    }

    while (text[i]) {
        char c = text[i];
        if (c < '0' || c > '9') {
            return 0;
        }
        value = value * 10 + (c - '0');
        if (value > 100) {
            return 0;
        }
        i++;
    }

    *out_value = value;
    return 1;
}

int app_main(const minidos_app_api_t* api) {
    unsigned int seed = 0xC0DEF00Du;
    int target;
    int attempts = 0;
    char line[16];

    app_puts(api, "=== Guess 100 ===\n");
    app_puts(api, "Try to guess a number between 0 and 100.\n");
    app_puts(api, "Type 'exit' to quit.\n\n");

    seed ^= (unsigned int)(unsigned long)api;
    target = (int)(seed % 101u);

    while (1) {
        int guess = 0;
        app_puts(api, "Your guess (0-100): ");
        read_line(api, line, (int)sizeof(line), &seed);

        if (streq(line, "exit")) {
            app_puts(api, "Leaving game.\n");
            return 0;
        }

        if (!parse_guess(line, &guess)) {
            app_puts(api, "Invalid input. Use a number 0..100.\n");
            continue;
        }

        attempts++;
        if (guess < target) {
            app_puts(api, "Too low.\n");
        } else if (guess > target) {
            app_puts(api, "Too high.\n");
        } else {
            app_puts(api, "Correct in ");
            put_uint(api, (unsigned int)attempts);
            app_puts(api, " attempts.\n");
            return 0;
        }
    }
}
