#include "minidos_app.h"

#define PTWAIT_STEPS 6U
#define PTWAIT_STEP_MS 350U

static void append_text(char* out, int* pos, const char* text) {
    int i = 0;

    while (out && pos && text && text[i] != '\0') {
        out[(*pos)++] = text[i++];
    }
    if (out && pos) {
        out[*pos] = '\0';
    }
}

static void append_uint(char* out, int* pos, unsigned int value) {
    char tmp[16];
    int len = 0;

    if (!out || !pos) {
        return;
    }
    if (value == 0U) {
        out[(*pos)++] = '0';
        out[*pos] = '\0';
        return;
    }

    while (value > 0U && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (len > 0) {
        out[(*pos)++] = tmp[--len];
    }
    out[*pos] = '\0';
}

static void emit_done(
    const minidos_app_api_t* api,
    unsigned int ticks,
    unsigned int timers,
    unsigned int keys,
    unsigned int mice,
    unsigned int early_exit
) {
    char line[128];
    int pos = 0;

    append_text(line, &pos, "PTWAIT190 ticks=");
    append_uint(line, &pos, ticks);
    append_text(line, &pos, " timer=");
    append_uint(line, &pos, timers);
    append_text(line, &pos, " key=");
    append_uint(line, &pos, keys);
    append_text(line, &pos, " mouse=");
    append_uint(line, &pos, mice);
    append_text(line, &pos, " early=");
    append_uint(line, &pos, early_exit);
    append_text(line, &pos, "\n");
    app_puts(api, line);
}

int app_main(const minidos_app_api_t* api) {
    unsigned int start = app_get_ticks(api);
    unsigned int timers = 0;
    unsigned int keys = 0;
    unsigned int mice = 0;
    unsigned int early_exit = 0;
    unsigned int last_mouse_seq = 0;
    app_mouse_state_t mouse_state = {0};

    if (app_mouse_state(api, &mouse_state) && mouse_state.present) {
        last_mouse_seq = mouse_state.seq;
    }

    app_puts(api, "PTWAIT100 start\n");

    for (unsigned int step = 0; step < PTWAIT_STEPS; step++) {
        int events = app_wait_event_timeout(api, last_mouse_seq, PTWAIT_STEP_MS);

        if (events & APP_EVENT_TIMER) {
            timers++;
        }
        if (events & APP_EVENT_MOUSE) {
            mice++;
            if (app_mouse_state(api, &mouse_state) && mouse_state.present) {
                last_mouse_seq = mouse_state.seq;
            }
        }
        if (events & APP_EVENT_KEY) {
            char c = 0;
            keys++;
            if (app_get_char_nonblock(api, &c)) {
                if (c == 'q' || c == 'Q' || c == 27) {
                    early_exit = 1;
                    break;
                }
            }
        }
    }

    emit_done(api, (unsigned int)(app_get_ticks(api) - start), timers, keys, mice, early_exit);
    return 0;
}
