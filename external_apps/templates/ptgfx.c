#include "minidos_app.h"

#define PTGFX_FRAMES 20U
#define PTGFX_FRAME_MS 80U

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

static void emit_done(const minidos_app_api_t* api, unsigned int ticks, unsigned int frames) {
    char line[96];
    int pos = 0;

    append_text(line, &pos, "PTGFX190 ticks=");
    append_uint(line, &pos, ticks);
    append_text(line, &pos, " frames=");
    append_uint(line, &pos, frames);
    append_text(line, &pos, "\n");
    app_puts(api, line);
}

int app_main(const minidos_app_api_t* api) {
    unsigned int start = app_get_ticks(api);
    unsigned int rendered = 0;
    unsigned int last_mouse_seq = 0;
    int width = 0;
    int height = 0;
    app_mouse_state_t mouse_state;

    if (!app_gfx_size(api, &width, &height) || width <= 0 || height <= 0) {
        app_puts(api, "PTGFX900 size\n");
        return 1;
    }
    if (app_mouse_state(api, &mouse_state) && mouse_state.present) {
        last_mouse_seq = mouse_state.seq;
    }

    app_puts(api, "PTGFX100 start\n");

    for (unsigned int frame = 0; frame < PTGFX_FRAMES; frame++) {
        app_gfx_rect_t bar;
        app_gfx_rect_t pulse;
        app_gfx_text_t title;
        app_gfx_text_t subtitle;
        unsigned int base = 0x00101820u + (frame * 0x00030301u);
        unsigned int accent = 0x00D09020u + (frame * 0x00040102u);
        char label[64];
        int pos = 0;
        int events;

        app_gfx_clear(api, base);

        bar.x = 20;
        bar.y = 24;
        bar.w = width - 40;
        bar.h = 16;
        bar.color = accent;
        (void)app_gfx_rect(api, &bar);

        pulse.x = 24 + (int)(frame * 6U);
        pulse.y = height / 2;
        pulse.w = width / 3;
        pulse.h = 24;
        pulse.color = 0x003060A0u + (frame * 0x00060301u);
        (void)app_gfx_rect(api, &pulse);

        title.x = 24;
        title.y = 56;
        title.text = "MiniDOS PTEST GFX";
        title.fg = 0x00FFFFFFu;
        title.bg = base;
        (void)app_gfx_text(api, &title);

        append_text(label, &pos, "Frame ");
        append_uint(label, &pos, frame + 1U);
        append_text(label, &pos, " of ");
        append_uint(label, &pos, PTGFX_FRAMES);
        subtitle.x = 24;
        subtitle.y = 72;
        subtitle.text = label;
        subtitle.fg = 0x00F6E6C0u;
        subtitle.bg = base;
        (void)app_gfx_text(api, &subtitle);

        (void)app_gfx_present(api);
        rendered++;

        events = app_wait_event_timeout(api, last_mouse_seq, PTGFX_FRAME_MS);
        if (events & APP_EVENT_MOUSE) {
            if (app_mouse_state(api, &mouse_state) && mouse_state.present) {
                last_mouse_seq = mouse_state.seq;
            }
        }
        if (events & APP_EVENT_KEY) {
            char c = 0;
            if (app_get_char_nonblock(api, &c) && (c == 'q' || c == 'Q' || c == 27)) {
                break;
            }
        }
    }

    emit_done(api, (unsigned int)(app_get_ticks(api) - start), rendered);
    return 0;
}
