#include "minidos_app.h"

#define PTIO_FILE_NAME "LOAD.BIN"
#define PTIO_RENAMED_NAME "SAVE.BIN"
#define PTIO_TEMP_DIR "PTTMP"
#define PTIO_BUFFER_SIZE 384

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

static unsigned int checksum_buffer(const unsigned char* buffer, int size) {
    unsigned int sum = 2166136261u;

    for (int i = 0; i < size; i++) {
        sum ^= (unsigned int)buffer[i];
        sum *= 16777619u;
        sum += (unsigned int)i;
    }

    return sum;
}

static unsigned int count_entries(const minidos_app_api_t* api) {
    unsigned int count = 0;
    char name[16];
    int is_dir = 0;

    while (count < 128U) {
        if (!app_list_entry(api, count, name, &is_dir)) {
            break;
        }
        count++;
    }

    return count;
}

static int fail(const minidos_app_api_t* api, const char* stage) {
    char line[96];
    int pos = 0;

    append_text(line, &pos, "PTIO900 ");
    append_text(line, &pos, stage);
    append_text(line, &pos, "\n");
    app_puts(api, line);
    return 1;
}

static void emit_done(
    const minidos_app_api_t* api,
    unsigned int ticks,
    unsigned int before_count,
    unsigned int after_count,
    unsigned int checksum
) {
    char line[128];
    int pos = 0;

    append_text(line, &pos, "PTIO190 ticks=");
    append_uint(line, &pos, ticks);
    append_text(line, &pos, " entries=");
    append_uint(line, &pos, before_count);
    append_text(line, &pos, "->");
    append_uint(line, &pos, after_count);
    append_text(line, &pos, " sum=");
    append_uint(line, &pos, checksum);
    append_text(line, &pos, "\n");
    app_puts(api, line);
}

int app_main(const minidos_app_api_t* api) {
    unsigned char write_buffer[PTIO_BUFFER_SIZE];
    unsigned char read_buffer[PTIO_BUFFER_SIZE];
    unsigned int start = app_get_ticks(api);
    unsigned int before_count = count_entries(api);
    unsigned int after_count = 0;
    unsigned int checksum = 0;
    int file_size;
    int bytes_read;

    app_puts(api, "PTIO100 start\n");

    for (int i = 0; i < PTIO_BUFFER_SIZE; i++) {
        write_buffer[i] = (unsigned char)((i * 17) ^ 0x5Au);
        read_buffer[i] = 0;
    }

    (void)app_delete_entry(api, PTIO_TEMP_DIR);
    if (!app_mkdir(api, PTIO_TEMP_DIR)) {
        return fail(api, "mkdir");
    }
    if (!app_chdir(api, PTIO_TEMP_DIR)) {
        (void)app_rmdir(api, PTIO_TEMP_DIR);
        return fail(api, "chdir-in");
    }
    if (!app_file_write(api, PTIO_FILE_NAME, write_buffer, PTIO_BUFFER_SIZE)) {
        (void)app_chdir(api, "..");
        (void)app_rmdir(api, PTIO_TEMP_DIR);
        return fail(api, "write");
    }

    file_size = app_file_size(api, PTIO_FILE_NAME);
    if (file_size != PTIO_BUFFER_SIZE) {
        (void)app_delete_entry(api, PTIO_FILE_NAME);
        (void)app_chdir(api, "..");
        (void)app_rmdir(api, PTIO_TEMP_DIR);
        return fail(api, "size");
    }

    bytes_read = app_file_read(api, PTIO_FILE_NAME, read_buffer, PTIO_BUFFER_SIZE);
    if (bytes_read != PTIO_BUFFER_SIZE) {
        (void)app_delete_entry(api, PTIO_FILE_NAME);
        (void)app_chdir(api, "..");
        (void)app_rmdir(api, PTIO_TEMP_DIR);
        return fail(api, "read");
    }

    checksum = checksum_buffer(read_buffer, bytes_read);
    if (!app_rename_entry(api, PTIO_FILE_NAME, PTIO_RENAMED_NAME)) {
        (void)app_delete_entry(api, PTIO_FILE_NAME);
        (void)app_chdir(api, "..");
        (void)app_rmdir(api, PTIO_TEMP_DIR);
        return fail(api, "rename");
    }
    if (!app_delete_entry(api, PTIO_RENAMED_NAME)) {
        (void)app_chdir(api, "..");
        (void)app_rmdir(api, PTIO_TEMP_DIR);
        return fail(api, "delete");
    }
    if (!app_chdir(api, "..")) {
        return fail(api, "chdir-out");
    }
    if (!app_rmdir(api, PTIO_TEMP_DIR)) {
        return fail(api, "rmdir");
    }

    after_count = count_entries(api);
    emit_done(api, (unsigned int)(app_get_ticks(api) - start), before_count, after_count, checksum);
    return 0;
}
