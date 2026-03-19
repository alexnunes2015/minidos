#include "minidos_app.h"

#define PTTHRD_MAIN_TICKS 240U
#define PTTHRD_CHILD_TICKS 220U

static volatile unsigned int g_child_loops = 0;
static volatile unsigned int g_child_done = 0;
static volatile unsigned int g_mix = 0x2468ACE1u;

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

static void emit_status(
    const minidos_app_api_t* api,
    const char* marker,
    unsigned int child_pid,
    unsigned int loops,
    unsigned int done
) {
    char line[128];
    int pos = 0;

    append_text(line, &pos, marker);
    append_text(line, &pos, " child=");
    append_uint(line, &pos, child_pid);
    append_text(line, &pos, " loops=");
    append_uint(line, &pos, loops);
    append_text(line, &pos, " done=");
    append_uint(line, &pos, done);
    append_text(line, &pos, " mix=");
    append_uint(line, &pos, g_mix & 0xFFFFU);
    append_text(line, &pos, "\n");
    app_puts(api, line);
}

static void ptthrd_child(const minidos_app_api_t* api, void* arg) {
    unsigned int run_ticks = (unsigned int)arg;
    unsigned int start = app_get_ticks(api);
    unsigned int self = (unsigned int)app_thread_self(api);

    emit_status(api, "PTTHRD110", self, g_child_loops, g_child_done);

    while ((unsigned int)(app_get_ticks(api) - start) < run_ticks) {
        for (unsigned int i = 0; i < 1024U; i++) {
            g_mix = (g_mix * 1103515245U) + 12345U + i + g_child_loops;
            g_mix ^= (g_mix >> 9);
            g_child_loops++;
        }
        app_thread_yield(api);
    }

    g_child_done = 1;
    emit_status(api, "PTTHRD111", self, g_child_loops, g_child_done);
}

int app_main(const minidos_app_api_t* api) {
    app_thread_create_t thread_spec;
    unsigned int start = app_get_ticks(api);
    unsigned int main_loops = 0;
    int child_pid;

    app_puts(api, "PTTHRD100 start\n");

    thread_spec.name = "worker";
    thread_spec.entry = ptthrd_child;
    thread_spec.arg = (void*)PTTHRD_CHILD_TICKS;
    child_pid = app_thread_create(api, &thread_spec);
    if (child_pid < 0) {
        app_puts(api, "PTTHRD900 spawn\n");
        return 1;
    }

    emit_status(api, "PTTHRD101", (unsigned int)child_pid, g_child_loops, g_child_done);

    while ((unsigned int)(app_get_ticks(api) - start) < PTTHRD_MAIN_TICKS) {
        for (unsigned int i = 0; i < 1024U; i++) {
            g_mix = (g_mix * 1664525U) + 1013904223U + main_loops + i;
            g_mix ^= (g_mix >> 11);
            main_loops++;
        }
        if (g_child_done && (unsigned int)(app_get_ticks(api) - start) >= 120U) {
            break;
        }
        app_thread_yield(api);
    }

    emit_status(api, "PTTHRD190", (unsigned int)child_pid, g_child_loops + main_loops, g_child_done);
    return 0;
}
