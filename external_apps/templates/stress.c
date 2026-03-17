#include "minidos_app.h"

#define STRESS_ROUNDS 4
#define STRESS_FILES 3
#define STRESS_READ_BYTES 1024
#define STRESS_FRAMES 8

static const char* g_monitored_files[STRESS_FILES] = {
    "HELLOELF.ELF",
    "STATELF.ELF",
    "STRESS.ELF",
};

static int str_equal(const char* a, const char* b) {
    int i = 0;

    if (!a || !b) {
        return 0;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

static void append_text(char* dst, int* pos, const char* text) {
    int i = 0;

    if (!dst || !pos || !text) {
        return;
    }

    while (text[i] != '\0') {
        dst[*pos] = text[i];
        (*pos)++;
        i++;
    }
    dst[*pos] = '\0';
}

static void append_uint(char* dst, int* pos, unsigned int value) {
    char tmp[16];
    int len = 0;

    if (!dst || !pos) {
        return;
    }

    if (value == 0) {
        dst[*pos] = '0';
        (*pos)++;
        dst[*pos] = '\0';
        return;
    }

    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (len > 0) {
        len--;
        dst[*pos] = tmp[len];
        (*pos)++;
    }
    dst[*pos] = '\0';
}

static void append_newline(char* dst, int* pos) {
    if (!dst || !pos) {
        return;
    }

    dst[*pos] = '\n';
    (*pos)++;
    dst[*pos] = '\0';
}

static void emit_line(const minidos_app_api_t* api, const char* text) {
    app_puts(api, text);
}

static void emit_stage(const minidos_app_api_t* api, unsigned int code, unsigned int round) {
    char line[64];
    int pos = 0;

    append_text(line, &pos, "STRS");
    append_uint(line, &pos, code);
    append_text(line, &pos, " r=");
    append_uint(line, &pos, round);
    append_newline(line, &pos);
    emit_line(api, line);
}

static void emit_round_ok(const minidos_app_api_t* api, int round) {
    char line[64];
    int pos = 0;

    append_text(line, &pos, "stress: round ");
    append_uint(line, &pos, (unsigned int)(round + 1));
    append_text(line, &pos, " ok");
    append_newline(line, &pos);
    emit_line(api, line);
}

static void emit_fail(const minidos_app_api_t* api, const char* stage, unsigned int detail) {
    char line[96];
    int pos = 0;

    append_text(line, &pos, "STRS900 ");
    append_text(line, &pos, stage);
    append_text(line, &pos, " ");
    append_uint(line, &pos, detail);
    append_newline(line, &pos);
    emit_line(api, line);
}

static void emit_pass(const minidos_app_api_t* api, unsigned int ticks) {
    char line[96];
    int pos = 0;

    append_text(line, &pos, "STRS190 ticks=");
    append_uint(line, &pos, ticks);
    append_newline(line, &pos);
    emit_line(api, line);
}

static unsigned int checksum_buffer(const unsigned char* buffer, int size) {
    unsigned int value = 2166136261u;
    int i;

    for (i = 0; i < size; i++) {
        value ^= (unsigned int)buffer[i];
        value *= 16777619u;
        value += (unsigned int)i;
    }

    return value;
}

static int dir_contains(const minidos_app_api_t* api, const char* name) {
    char entry_name[16];
    int is_dir = 0;
    unsigned int index = 0;

    while (index < 128) {
        if (!app_list_entry(api, index, entry_name, &is_dir)) {
            return 0;
        }
        if (!is_dir && str_equal(entry_name, name)) {
            return 1;
        }
        index++;
    }

    return 0;
}

static int sample_file(
    const minidos_app_api_t* api,
    const char* name,
    unsigned char* buffer,
    int buffer_size,
    unsigned int* out_size,
    unsigned int* out_read_len,
    unsigned int* out_checksum
) {
    int file_size;
    int to_read;
    int bytes_read;

    if (!out_size || !out_read_len || !out_checksum) {
        return 0;
    }

    file_size = app_file_size(api, name);
    if (file_size <= 0 || !dir_contains(api, name)) {
        return 0;
    }

    to_read = file_size < buffer_size ? file_size : buffer_size;
    bytes_read = app_file_read(api, name, buffer, to_read);
    if (bytes_read != to_read) {
        return 0;
    }

    *out_size = (unsigned int)file_size;
    *out_read_len = (unsigned int)bytes_read;
    *out_checksum = checksum_buffer(buffer, bytes_read);
    return 1;
}

static void build_frame_label(char* out, int round, int frame) {
    int pos = 0;

    append_text(out, &pos, "Stress round ");
    append_uint(out, &pos, (unsigned int)(round + 1));
    append_text(out, &pos, " frame ");
    append_uint(out, &pos, (unsigned int)(frame + 1));
    out[pos] = '\0';
}

static void render_frames(const minidos_app_api_t* api, int round) {
    int width = 0;
    int height = 0;
    int frame;

    if (!app_gfx_size(api, &width, &height) || width <= 0 || height <= 0) {
        return;
    }

    for (frame = 0; frame < STRESS_FRAMES; frame++) {
        app_gfx_rect_t bar;
        app_gfx_rect_t pulse;
        app_gfx_text_t title;
        app_gfx_text_t subtitle;
        char label[64];
        unsigned int base = 0x00101820u + (unsigned int)(round * 0x00040400u);
        unsigned int accent = 0x00D0A020u + (unsigned int)(frame * 0x00030703u);

        app_gfx_clear(api, base);

        bar.x = 18;
        bar.y = 20 + frame * 6;
        bar.w = width - 36;
        bar.h = 14;
        bar.color = accent;
        (void)app_gfx_rect(api, &bar);

        pulse.x = 24 + frame * 10;
        pulse.y = height / 2;
        pulse.w = width / 3;
        pulse.h = 20;
        pulse.color = 0x003060A0u + (unsigned int)(round * 0x000A0500u);
        (void)app_gfx_rect(api, &pulse);

        title.x = 24;
        title.y = 48;
        title.text = "MiniDOS STRESS";
        title.fg = 0x00FFFFFFu;
        title.bg = base;
        (void)app_gfx_text(api, &title);

        build_frame_label(label, round, frame);
        subtitle.x = 24;
        subtitle.y = 64;
        subtitle.text = label;
        subtitle.fg = 0x00F6E6C0u;
        subtitle.bg = base;
        (void)app_gfx_text(api, &subtitle);

        (void)app_gfx_present(api);
    }
}

int app_main(const minidos_app_api_t* api) {
    static unsigned char read_buffer[STRESS_READ_BYTES];
    unsigned int baseline_sizes[STRESS_FILES];
    unsigned int baseline_reads[STRESS_FILES];
    unsigned int baseline_checksums[STRESS_FILES];
    unsigned int start_ticks;
    unsigned int end_ticks;
    int file_index;
    int round;

    emit_line(api, "STRS100\n");
    emit_stage(api, 110, 0);

    for (file_index = 0; file_index < STRESS_FILES; file_index++) {
        if (!sample_file(
                api,
                g_monitored_files[file_index],
                read_buffer,
                STRESS_READ_BYTES,
                &baseline_sizes[file_index],
                &baseline_reads[file_index],
                &baseline_checksums[file_index])) {
            emit_fail(api, "baseline", (unsigned int)file_index);
            return 1;
        }
    }

    start_ticks = app_get_ticks(api);
    for (round = 0; round < STRESS_ROUNDS; round++) {
        emit_stage(api, 200, (unsigned int)round);
        for (file_index = 0; file_index < STRESS_FILES; file_index++) {
            unsigned int size = 0;
            unsigned int read_len = 0;
            unsigned int checksum = 0;

            if (!sample_file(
                    api,
                    g_monitored_files[file_index],
                    read_buffer,
                    STRESS_READ_BYTES,
                    &size,
                    &read_len,
                    &checksum)) {
                emit_fail(api, "read", (unsigned int)(round * 10 + file_index));
                return 1;
            }

            if (size != baseline_sizes[file_index]
                || read_len != baseline_reads[file_index]
                || checksum != baseline_checksums[file_index]) {
                emit_fail(api, "drift", (unsigned int)(round * 10 + file_index));
                return 1;
            }
        }

        emit_stage(api, 220, (unsigned int)round);
        render_frames(api, round);
        emit_round_ok(api, round);
    }

    end_ticks = app_get_ticks(api);
    emit_pass(api, end_ticks - start_ticks);
    return 0;
}
