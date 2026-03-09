#include "logger.h"

#define LOG_BUFFER_SIZE 8192

static char log_buffer[LOG_BUFFER_SIZE];
static unsigned int log_head = 0;
static unsigned int log_count = 0;
static unsigned int log_seq = 0;

static const char* level_name(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_WARN:
            return "WARN";
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_DEBUG:
            return "DEBUG";
        case LOG_LEVEL_TRACE:
            return "TRACE";
        default:
            return "UNK";
    }
}

static void append_char(char c) {
    log_buffer[log_head] = c;
    log_head = (log_head + 1) % LOG_BUFFER_SIZE;
    if (log_count < LOG_BUFFER_SIZE) {
        log_count++;
    }
}

static void append_string(const char* str) {
    while (*str) {
        append_char(*str++);
    }
}

static int uint_to_dec(unsigned int value, char* out) {
    char tmp[16];
    int len = 0;

    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }

    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    for (int i = 0; i < len; i++) {
        out[i] = tmp[len - 1 - i];
    }
    out[len] = '\0';
    return len;
}

static void write_char_to_dest(char c, LogDest dest) {
    if (dest & LOG_DEST_SCREEN) {
        print_char(c);
    }
    if (dest & LOG_DEST_SERIAL) {
        serial_putchar(c);
    }
}

static void write_string_to_dest(const char* str, LogDest dest) {
    while (*str) {
        write_char_to_dest(*str++, dest);
    }
}

void log_init() {
    log_head = 0;
    log_count = 0;
    log_seq = 0;
}

void log_write(LogLevel level, const char* module, const char* msg, LogDest dest) {
    if ((int)level > (int)MIN_LOG_LEVEL) {
        return;
    }

    if (dest & LOG_DEST_SERIAL) {
        char seq[16];

        log_seq++;
        uint_to_dec(log_seq, seq);

        serial_putchar('[');
        serial_print(seq);
        serial_print("][");
        serial_print(level_name(level));
        serial_print("][");
        serial_print(module ? module : "core");
        serial_print("] ");

        append_char('[');
        append_string(seq);
        append_string("][");
        append_string(level_name(level));
        append_string("][");
        append_string(module ? module : "core");
        append_string("] ");
        append_string(msg);
    }

    if (dest & LOG_DEST_SCREEN) {
        print_string(msg);
    }

    if (dest & LOG_DEST_SERIAL) {
        serial_print(msg);
    }
}

void log_screen(const char* msg) {
    write_string_to_dest(msg, LOG_DEST_SCREEN);
}

void log_serial_raw(const char* msg) {
    write_string_to_dest(msg, LOG_DEST_SERIAL);
    append_string(msg);
}

void log_both(const char* msg) {
    write_string_to_dest(msg, LOG_DEST_BOTH);
    append_string(msg);
}

void log_dump_buffer(LogDest dest) {
    if (log_count == 0) {
        write_string_to_dest("[dmesg] log buffer is empty\n", dest);
        return;
    }

    write_string_to_dest("--- dmesg (serial log ring) ---\n", dest);

    unsigned int start = (log_head + LOG_BUFFER_SIZE - log_count) % LOG_BUFFER_SIZE;
    for (unsigned int i = 0; i < log_count; i++) {
        unsigned int idx = (start + i) % LOG_BUFFER_SIZE;
        write_char_to_dest(log_buffer[idx], dest);
    }

    if (log_buffer[(log_head + LOG_BUFFER_SIZE - 1) % LOG_BUFFER_SIZE] != '\n') {
        write_char_to_dest('\n', dest);
    }

    write_string_to_dest("--- end dmesg ---\n", dest);
}
