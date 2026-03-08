#include "shell.h"
#include "video.h"
#include "fat16.h"
#include "drive.h"
#include "serial.h"
#include "logger.h"
#include "rtc.h"
#include "keyboard.h"
#include "mouse.h"
#include "scheduler.h"
#include "timer.h"

static unsigned int current_dir_cluster = 0;
static char current_path[64] = "\\";
static int fat16_initialized = 0;
static unsigned int app_clip_src_cluster = 0;
static char app_clip_name[64];
static int app_clip_mode = 0; /* 0 none, 1 copy, 2 move */
static const unsigned int SHELL_BOOT_STEP_MS = 50;
static const unsigned int SHELL_BOOT_FINAL_MS = 100;
static const unsigned int EFLAGS_IF = 0x00000200U;
static int app_input_ready_logged = 0;

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline unsigned int read_eflags(void) {
    unsigned int flags;
    __asm__ volatile ("pushf\npop %0" : "=r"(flags));
    return flags;
}

static void show_boot_screen() {
    cls();
    print_string("\n");
    print_string("========================================\n");
    print_string("            MiniDOS Boot                \n");
    print_string("========================================\n\n");
    print_string("Initializing system components...\n\n");

    print_string("[ ");
    for (int i = 0; i < 20; i++) {
        print_char('#');
        timer_sleep_ms(SHELL_BOOT_STEP_MS);
    }
    print_string(" ] Done\n\n");

    timer_sleep_ms(SHELL_BOOT_FINAL_MS);
    cls();
}

static void shell_out_screen(const char* s) {
    print_string(s);
}

static void shell_out_both(const char* s) {
    print_string(s);
    log_serial_raw(s);
}

static void shell_out_both_char(char c) {
    char s[2];
    s[0] = c;
    s[1] = '\0';
    print_char(c);
    log_serial_raw(s);
}

static int mystrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int uint_to_dec(unsigned int value, char* out) {
    char tmp[16];
    int len = 0;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }

    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = '0' + (value % 10);
        value /= 10;
    }

    for (int i = 0; i < len; i++) {
        out[i] = tmp[len - 1 - i];
    }

    return len;
}

static void format_memory(unsigned int kb, char* out) {
    const char* unit = "KB";
    unsigned int unit_kb = 1;

    if (kb >= 1024U * 1024U) {
        unit = "GB";
        unit_kb = 1024U * 1024U;
    } else if (kb >= 1024U) {
        unit = "MB";
        unit_kb = 1024U;
    }

    int pos = 0;
    if (unit_kb == 1) {
        pos += uint_to_dec(kb, out + pos);
    } else {
        unsigned int tenths = (kb * 10 + unit_kb / 2) / unit_kb;
        unsigned int whole = tenths / 10;
        unsigned int frac = tenths % 10;

        pos += uint_to_dec(whole, out + pos);
        out[pos++] = '.';
        out[pos++] = '0' + (char)frac;
    }

    out[pos++] = ' ';
    out[pos++] = unit[0];
    out[pos++] = unit[1];
    out[pos] = '\0';
}

static void path_reset(char* path) {
    path[0] = '\\';
    path[1] = '\0';
}

static void path_pop(char* path) {
    int len = 0;
    while (path[len] != '\0') len++;

    if (len <= 1) {
        path_reset(path);
        return;
    }

    for (int i = len - 1; i > 0; i--) {
        if (path[i] == '\\') {
            path[i] = '\0';
            return;
        }
    }

    path_reset(path);
}

static int path_push(char* path, int max_len, const char* name) {
    int len = 0;
    while (path[len] != '\0') len++;

    int name_len = 0;
    while (name[name_len] != '\0') name_len++;

    if (len == 1 && path[0] == '\\') {
        int need = 1 + name_len + 1;
        if (need > max_len) return 0;
        for (int i = 0; i < name_len; i++) {
            path[1 + i] = name[i];
        }
        path[1 + name_len] = '\0';
        return 1;
    }

    int need = len + 1 + name_len + 1;
    if (need > max_len) return 0;

    if (path[len - 1] != '\\') {
        path[len++] = '\\';
    }

    for (int i = 0; i < name_len; i++) {
        path[len++] = name[i];
    }
    path[len] = '\0';
    return 1;
}

static void append_two_digits(char* out, int* pos, unsigned char value) {
    out[(*pos)++] = (char)('0' + (value / 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

static void format_time_string(const rtc_time_t* time, char* out) {
    int pos = 0;
    append_two_digits(out, &pos, time->hours);
    out[pos++] = ':';
    append_two_digits(out, &pos, time->minutes);
    out[pos++] = ':';
    append_two_digits(out, &pos, time->seconds);
    out[pos] = '\0';
}

static void append_four_digits(char* out, int* pos, unsigned short value) {
    out[(*pos)++] = (char)('0' + ((value / 1000) % 10));
    out[(*pos)++] = (char)('0' + ((value / 100) % 10));
    out[(*pos)++] = (char)('0' + ((value / 10) % 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

static void format_date_string(const rtc_date_t* date, char* out) {
    int pos = 0;
    append_two_digits(out, &pos, date->day);
    out[pos++] = '/';
    append_two_digits(out, &pos, date->month);
    out[pos++] = '/';
    append_four_digits(out, &pos, date->year);
    out[pos] = '\0';
}

static int parse_time_arg(const char* args, rtc_time_t* time) {
    if (!args || !time) {
        return 0;
    }

    int values[3] = {0, 0, 0};
    int value_count = 0;
    int current_value = 0;
    int digits = 0;
    int i = 0;

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }

    for (; args[i] != '\0'; i++) {
        char c = args[i];
        if (c >= '0' && c <= '9') {
            current_value = current_value * 10 + (c - '0');
            digits++;
            if (digits > 2) {
                return 0;
            }
        } else if (c == ':') {
            if (digits == 0 || value_count >= 2) {
                return 0;
            }
            values[value_count++] = current_value;
            current_value = 0;
            digits = 0;
        } else if (c == ' ' || c == '\t') {
            while (args[i] == ' ' || args[i] == '\t') {
                i++;
            }
            if (args[i] != '\0') {
                return 0;
            }
            break;
        } else {
            return 0;
        }
    }

    if (digits == 0) {
        return 0;
    }
    values[value_count++] = current_value;

    if (value_count != 2 && value_count != 3) {
        return 0;
    }
    if (values[0] > 23 || values[1] > 59) {
        return 0;
    }

    time->hours = (unsigned char)values[0];
    time->minutes = (unsigned char)values[1];
    time->seconds = (unsigned char)((value_count == 3) ? values[2] : 0);
    if (time->seconds > 59) {
        return 0;
    }

    return 1;
}

static int parse_date_arg(const char* args, rtc_date_t* date) {
    if (!args || !date) {
        return 0;
    }

    int values[3] = {0, 0, 0};
    int digit_counts[3] = {0, 0, 0};
    int value_index = 0;
    int i = 0;

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }

    for (; args[i] != '\0'; i++) {
        char c = args[i];
        if (c >= '0' && c <= '9') {
            if (value_index > 2) {
                return 0;
            }
            values[value_index] = values[value_index] * 10 + (c - '0');
            digit_counts[value_index]++;
            if (value_index < 2 && digit_counts[value_index] > 2) {
                return 0;
            }
            if (value_index == 2 && digit_counts[value_index] > 4) {
                return 0;
            }
        } else if (c == '/' || c == '-') {
            if (digit_counts[value_index] == 0 || value_index >= 2) {
                return 0;
            }
            value_index++;
        } else if (c == ' ' || c == '\t') {
            while (args[i] == ' ' || args[i] == '\t') {
                i++;
            }
            if (args[i] != '\0') {
                return 0;
            }
            break;
        } else {
            return 0;
        }
    }

    if (value_index != 2 || digit_counts[2] == 0) {
        return 0;
    }

    unsigned short year;
    if (digit_counts[2] == 2) {
        if (values[2] >= 80) {
            year = (unsigned short)(1900 + values[2]);
        } else {
            year = (unsigned short)(2000 + values[2]);
        }
    } else if (digit_counts[2] == 4) {
        year = (unsigned short)values[2];
    } else {
        return 0;
    }

    if (year < 1980 || year > 2099) {
        return 0;
    }
    if (values[1] < 1 || values[1] > 12) {
        return 0;
    }
    if (values[0] < 1 || values[0] > 31) {
        return 0;
    }

    date->day = (unsigned char)values[0];
    date->month = (unsigned char)values[1];
    date->year = year;
    return 1;
}

// Split command and arguments
static void parse_command(char* cmd, char* command, char* args) {
    int i = 0;
    
    // Extract command
    while (cmd[i] && cmd[i] != ' ') {
        command[i] = cmd[i];
        i++;
    }
    command[i] = '\0';
    
    // Skip spaces
    while (cmd[i] == ' ') i++;
    
    // Extract arguments
    int j = 0;
    while (cmd[i]) {
        args[j++] = cmd[i++];
    }
    args[j] = '\0';
}

static void str_to_upper(char* s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] >= 'a' && s[i] <= 'z') {
            s[i] = s[i] - 'a' + 'A';
        }
    }
}

static void str_copy_upper(const char* src, char* dst, int dst_size) {
    int i = 0;
    if (dst_size <= 0) {
        return;
    }
    while (src[i] && i < dst_size - 1) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        dst[i] = c;
        i++;
    }
    dst[i] = '\0';
}

static void str_to_lower(char* s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            s[i] = (char)(s[i] - 'A' + 'a');
        }
    }
}

static int parse_two_args(const char* args, char* first, int first_size, char* second, int second_size) {
    int i = 0;
    int j = 0;

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }
    if (args[i] == '\0') {
        return 0;
    }

    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        if (j >= first_size - 1) {
            return 0;
        }
        first[j++] = args[i++];
    }
    first[j] = '\0';

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }
    if (args[i] == '\0') {
        return 0;
    }

    j = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        if (j >= second_size - 1) {
            return 0;
        }
        second[j++] = args[i++];
    }
    second[j] = '\0';

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }
    return args[i] == '\0';
}

static int parse_single_arg(const char* args, char* out, int out_size) {
    int i = 0;
    int j = 0;

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }
    if (args[i] == '\0') {
        return 0;
    }

    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        if (j >= out_size - 1) {
            return 0;
        }
        out[j++] = args[i++];
    }
    out[j] = '\0';

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }
    return args[i] == '\0';
}

static int parse_uint_arg(const char* s, unsigned int* out) {
    unsigned int value = 0;
    int i = 0;
    int base = 10;
    int digits = 0;

    if (!s || !out) {
        return 0;
    }

    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }

    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    }

    while (s[i] != '\0') {
        unsigned int d;
        char c = s[i];
        if (c >= '0' && c <= '9') {
            d = (unsigned int)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            d = 10u + (unsigned int)(c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            d = 10u + (unsigned int)(c - 'A');
        } else {
            break;
        }
        value = value * (unsigned int)base + d;
        i++;
        digits++;
    }

    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }

    if (digits == 0 || s[i] != '\0') {
        return 0;
    }

    *out = value;
    return 1;
}

static int parse_n_uints(const char* args, unsigned int* out, int count, const char** rest) {
    int i = 0;

    if (!args || !out || count <= 0) {
        return 0;
    }

    for (int n = 0; n < count; n++) {
        char tok[32];
        int j = 0;

        while (args[i] == ' ' || args[i] == '\t') {
            i++;
        }
        if (args[i] == '\0') {
            return 0;
        }

        while (args[i] != '\0' && args[i] != ' ' && args[i] != '\t') {
            if (j >= (int)sizeof(tok) - 1) {
                return 0;
            }
            tok[j++] = args[i++];
        }
        tok[j] = '\0';

        if (!parse_uint_arg(tok, &out[n])) {
            return 0;
        }
    }

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }
    if (rest) {
        *rest = &args[i];
    }
    return 1;
}

static int has_wildcards(const char* s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '*' || s[i] == '?') {
            return 1;
        }
    }
    return 0;
}

enum {
    APP_SYSCALL_PUTS = 1,
    APP_SYSCALL_GET_CHAR = 2,
    APP_SYSCALL_FILE_SIZE = 3,
    APP_SYSCALL_LIST_ENTRY = 4,
    APP_SYSCALL_COPY_FILE = 5,
    APP_SYSCALL_MOVE_FILE = 6,
    APP_SYSCALL_GFX_CLEAR = 7,
    APP_SYSCALL_GFX_RECT = 8,
    APP_SYSCALL_GFX_TEXT = 9,
    APP_SYSCALL_GFX_SIZE = 10,
    APP_SYSCALL_CHDIR = 11,
    APP_SYSCALL_MKDIR = 12,
    APP_SYSCALL_RMDIR = 13,
    APP_SYSCALL_DELETE_ENTRY = 14,
    APP_SYSCALL_RENAME_ENTRY = 15,
    APP_SYSCALL_COPY_TO_DIR = 16,
    APP_SYSCALL_MOVE_TO_DIR = 17,
    APP_SYSCALL_CLIP_SET = 18,
    APP_SYSCALL_CLIP_PASTE = 19,
    APP_SYSCALL_RANDOM = 20,
    APP_SYSCALL_FILE_READ = 21,
    APP_SYSCALL_FILE_WRITE = 22,
    APP_SYSCALL_GET_CHAR_NONBLOCK = 23,
    APP_SYSCALL_GET_TICKS = 24,
    APP_SYSCALL_GET_MOUSE_STATE = 25,
    APP_SYSCALL_WAIT_EVENT = 26,
};

enum {
    APP_EVENT_KEY = 1,
    APP_EVENT_MOUSE = 2,
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
    unsigned int color;
} app_gfx_rect_t;

typedef struct {
    int x;
    int y;
    const char* text;
    unsigned int fg;
    unsigned int bg;
} app_gfx_text_t;

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    unsigned int buttons;
    unsigned int seq;
    int present;
} app_mouse_state_t;

typedef int (*app_syscall_t)(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2);

typedef struct {
    app_syscall_t syscall;
} minidos_app_api_t;

static void app_api_puts(const char* text) {
    if (!text) {
        return;
    }
    shell_out_both(text);
}

static void app_api_ensure_interrupts_enabled(void) {
    if ((read_eflags() & EFLAGS_IF) == 0U) {
        __asm__ volatile ("sti");
    }
}

static void app_api_begin_input_session(void) {
    app_input_ready_logged = 0;
}

static void app_api_note_input_ready(void) {
    if (!app_input_ready_logged) {
        log_serial_raw("APPIN001\n");
        app_input_ready_logged = 1;
    }
}

static void app_api_note_session_return(void) {
    log_serial_raw("APPRET001\n");
    app_input_ready_logged = 0;
}

static int app_api_try_get_char(char* out) {
    if (!out) {
        return 0;
    }

    app_api_ensure_interrupts_enabled();
    app_api_note_input_ready();

    if (keyboard_try_get_char(out)) {
        return 1;
    }

    if (serial_received()) {
        *out = serial_getchar();
        return 1;
    }

    return 0;
}

static char app_api_get_char(void) {
    char c = 0;

    while (!app_api_try_get_char(&c)) {
        timer_wait_for_interrupt();
    }

    return c;
}

static int app_api_get_char_nonblock(char* out) {
    return app_api_try_get_char(out);
}

static int app_api_get_mouse_state(app_mouse_state_t* out) {
    mouse_state_t state;

    if (!out) {
        return 0;
    }

    app_api_ensure_interrupts_enabled();
    app_api_note_input_ready();

    if (!mouse_get_state(&state)) {
        return 0;
    }

    out->x = state.x;
    out->y = state.y;
    out->dx = state.dx;
    out->dy = state.dy;
    out->buttons = state.buttons;
    out->seq = state.seq;
    out->present = state.present;
    return 1;
}

static int app_api_wait_event(unsigned int last_mouse_seq) {
    mouse_state_t state;

    app_api_ensure_interrupts_enabled();
    app_api_note_input_ready();

    while (1) {
        int flags = 0;

        if (keyboard_has_input() || serial_received()) {
            flags |= APP_EVENT_KEY;
        }

        if (mouse_get_state(&state) && state.present && state.seq != last_mouse_seq) {
            flags |= APP_EVENT_MOUSE;
        }

        if (flags != 0) {
            return flags;
        }

        timer_wait_for_interrupt();
    }
}

static unsigned int app_rng_state = 0xA5F21C3Du;

static unsigned int app_api_random_u32(void) {
    unsigned int ticks = scheduler_get_ticks();
    unsigned int mix = ticks ^ (ticks << 16);

    app_rng_state ^= mix;
    app_rng_state ^= (app_rng_state << 13);
    app_rng_state ^= (app_rng_state >> 17);
    app_rng_state ^= (app_rng_state << 5);
    if (app_rng_state == 0) {
        app_rng_state = 0x6D2B79F5u ^ mix;
    }
    return app_rng_state;
}

static int app_api_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    if (num == APP_SYSCALL_PUTS) {
        app_api_puts((const char*)a0);
        return 0;
    }

    if (num == APP_SYSCALL_GET_CHAR) {
        return (int)(unsigned char)app_api_get_char();
    }

    if (num == APP_SYSCALL_GET_CHAR_NONBLOCK) {
        char c = 0;
        if (app_api_get_char_nonblock(&c)) {
            return (int)(unsigned char)c;
        }
        return -1;
    }

    if (num == APP_SYSCALL_GET_MOUSE_STATE) {
        return app_api_get_mouse_state((app_mouse_state_t*)a0);
    }

    if (num == APP_SYSCALL_WAIT_EVENT) {
        return app_api_wait_event(a0);
    }

    if (num == APP_SYSCALL_GET_TICKS) {
        return (int)scheduler_get_ticks();
    }

    if (num == APP_SYSCALL_RANDOM) {
        unsigned int limit = a0;
        unsigned int value = app_api_random_u32() & 0x7FFFFFFFu;
        if (limit > 0) {
            value %= limit;
        }
        return (int)value;
    }

    if (num == APP_SYSCALL_FILE_SIZE) {
        FAT16_DirectoryEntry entry;
        char file_name[64];
        int i = 0;
        const char* input = (const char*)a0;
        if (!input || input[0] == '\0') {
            return -1;
        }
        while (input[i] != '\0' && i < (int)sizeof(file_name) - 1) {
            file_name[i] = input[i];
            i++;
        }
        file_name[i] = '\0';
        str_to_upper(file_name);
        if (!fat16_find_entry(current_dir_cluster, file_name, &entry, 0, 0)) {
            return -1;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return -1;
        }
        return (int)entry.file_size;
    }

    if (num == APP_SYSCALL_FILE_READ) {
        char file_name[64];
        unsigned char* out_buffer = (unsigned char*)a1;
        int max_size = (int)a2;
        int i = 0;
        const char* input = (const char*)a0;
        if (!input || !out_buffer || max_size <= 0) {
            return -1;
        }
        while (input[i] != '\0' && i < (int)sizeof(file_name) - 1) {
            file_name[i] = input[i];
            i++;
        }
        file_name[i] = '\0';
        str_to_upper(file_name);
        return fat16_read_file_from_dir(current_dir_cluster, file_name, out_buffer, max_size);
    }

    if (num == APP_SYSCALL_FILE_WRITE) {
        char file_name[64];
        const unsigned char* in_buffer = (const unsigned char*)a1;
        int size = (int)a2;
        int i = 0;
        const char* input = (const char*)a0;
        if (!input || (!in_buffer && size > 0) || size < 0) {
            return 0;
        }
        while (input[i] != '\0' && i < (int)sizeof(file_name) - 1) {
            file_name[i] = input[i];
            i++;
        }
        file_name[i] = '\0';
        str_to_upper(file_name);
        return fat16_write_file_from_dir(current_dir_cluster, file_name, in_buffer, size);
    }

    if (num == APP_SYSCALL_LIST_ENTRY) {
        unsigned int index = a0;
        char* out_name = (char*)a1;
        int* out_is_dir = (int*)a2;
        unsigned int file_size = 0;
        if (!out_name) {
            return 0;
        }
        return fat16_get_entry_by_index(current_dir_cluster, index, out_name, 13, out_is_dir, &file_size);
    }

    if (num == APP_SYSCALL_COPY_FILE) {
        const char* src = (const char*)a0;
        const char* dst = (const char*)a1;
        if (!src || !dst || src[0] == '\0' || dst[0] == '\0') {
            return 0;
        }
        return fat16_copy_file(current_dir_cluster, src, dst);
    }

    if (num == APP_SYSCALL_MOVE_FILE) {
        const char* src = (const char*)a0;
        const char* dst = (const char*)a1;
        FAT16_DirectoryEntry entry;
        if (!src || !dst || src[0] == '\0' || dst[0] == '\0') {
            return 0;
        }
        if (!fat16_find_entry(current_dir_cluster, src, &entry, 0, 0)) {
            return 0;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return 0;
        }
        return fat16_update_entry(current_dir_cluster, src, dst, entry.cluster_low, entry.file_size, entry.attributes);
    }

    if (num == APP_SYSCALL_GFX_CLEAR) {
        video_clear_color(a0);
        return 1;
    }

    if (num == APP_SYSCALL_GFX_RECT) {
        const app_gfx_rect_t* rect = (const app_gfx_rect_t*)a0;
        if (!rect) {
            return 0;
        }
        video_fill_rect(rect->x, rect->y, rect->w, rect->h, rect->color);
        return 1;
    }

    if (num == APP_SYSCALL_GFX_TEXT) {
        const app_gfx_text_t* text = (const app_gfx_text_t*)a0;
        if (!text || !text->text) {
            return 0;
        }
        video_draw_text_at(text->x, text->y, text->text, text->fg, text->bg);
        return 1;
    }

    if (num == APP_SYSCALL_GFX_SIZE) {
        int* out_w = (int*)a0;
        int* out_h = (int*)a1;
        if (!out_w || !out_h) {
            return 0;
        }
        *out_w = video_get_width();
        *out_h = video_get_height();
        return 1;
    }

    if (num == APP_SYSCALL_CHDIR) {
        char dir[64];
        const char* input = (const char*)a0;
        int i = 0;
        if (!input || input[0] == '\0') {
            return 0;
        }
        while (input[i] != '\0' && i < (int)sizeof(dir) - 1) {
            dir[i] = input[i];
            i++;
        }
        dir[i] = '\0';

        if (dir[0] == '\\' || dir[0] == '/') {
            current_dir_cluster = 0;
            path_reset(current_path);
            return 1;
        }
        if (dir[0] == '.' && dir[1] == '.' && dir[2] == '\0') {
            unsigned int parent_cluster = 0;
            if (fat16_get_parent_cluster(current_dir_cluster, &parent_cluster)) {
                current_dir_cluster = parent_cluster;
                path_pop(current_path);
                return 1;
            }
            return 0;
        }
        if (dir[0] == '.' && dir[1] == '\0') {
            return 1;
        }

        str_to_upper(dir);
        unsigned int next_cluster = 0;
        if (!fat16_find_dir_cluster(current_dir_cluster, dir, &next_cluster)) {
            return 0;
        }
        if (!path_push(current_path, (int)sizeof(current_path), dir)) {
            return 0;
        }
        current_dir_cluster = next_cluster;
        return 1;
    }

    if (num == APP_SYSCALL_MKDIR) {
        char name[64];
        const char* input = (const char*)a0;
        int i = 0;
        if (!input || input[0] == '\0') {
            return 0;
        }
        while (input[i] != '\0' && i < (int)sizeof(name) - 1) {
            name[i] = input[i];
            i++;
        }
        name[i] = '\0';
        str_to_upper(name);
        return fat16_mkdir(current_dir_cluster, name);
    }

    if (num == APP_SYSCALL_RMDIR) {
        char name[64];
        const char* input = (const char*)a0;
        int i = 0;
        if (!input || input[0] == '\0') {
            return 0;
        }
        while (input[i] != '\0' && i < (int)sizeof(name) - 1) {
            name[i] = input[i];
            i++;
        }
        name[i] = '\0';
        str_to_upper(name);
        return fat16_rmdir(current_dir_cluster, name);
    }

    if (num == APP_SYSCALL_DELETE_ENTRY) {
        char name[64];
        FAT16_DirectoryEntry entry;
        const char* input = (const char*)a0;
        int i = 0;
        if (!input || input[0] == '\0') {
            return 0;
        }
        while (input[i] != '\0' && i < (int)sizeof(name) - 1) {
            name[i] = input[i];
            i++;
        }
        name[i] = '\0';
        str_to_upper(name);
        if (!fat16_find_entry(current_dir_cluster, name, &entry, 0, 0)) {
            return 0;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return fat16_rmdir(current_dir_cluster, name);
        }
        return fat16_delete_entry(current_dir_cluster, name, 1);
    }

    if (num == APP_SYSCALL_RENAME_ENTRY) {
        char old_name[64];
        char new_name[64];
        FAT16_DirectoryEntry entry;
        const char* old_input = (const char*)a0;
        const char* new_input = (const char*)a1;
        int i = 0;
        if (!old_input || !new_input || old_input[0] == '\0' || new_input[0] == '\0') {
            return 0;
        }
        while (old_input[i] != '\0' && i < (int)sizeof(old_name) - 1) {
            old_name[i] = old_input[i];
            i++;
        }
        old_name[i] = '\0';
        i = 0;
        while (new_input[i] != '\0' && i < (int)sizeof(new_name) - 1) {
            new_name[i] = new_input[i];
            i++;
        }
        new_name[i] = '\0';

        str_to_upper(old_name);
        str_to_upper(new_name);
        if (!fat16_find_entry(current_dir_cluster, old_name, &entry, 0, 0)) {
            return 0;
        }
        return fat16_update_entry(current_dir_cluster, old_name, new_name, entry.cluster_low, entry.file_size, entry.attributes);
    }

    if (num == APP_SYSCALL_COPY_TO_DIR || num == APP_SYSCALL_MOVE_TO_DIR) {
        char src_name[64];
        char dst_dir_name[64];
        unsigned int dst_dir_cluster = 0;
        const char* src_input = (const char*)a0;
        const char* dst_dir_input = (const char*)a1;
        int i = 0;
        if (!src_input || !dst_dir_input || src_input[0] == '\0' || dst_dir_input[0] == '\0') {
            return 0;
        }
        while (src_input[i] != '\0' && i < (int)sizeof(src_name) - 1) {
            src_name[i] = src_input[i];
            i++;
        }
        src_name[i] = '\0';
        i = 0;
        while (dst_dir_input[i] != '\0' && i < (int)sizeof(dst_dir_name) - 1) {
            dst_dir_name[i] = dst_dir_input[i];
            i++;
        }
        dst_dir_name[i] = '\0';
        str_to_upper(src_name);
        str_to_upper(dst_dir_name);

        if (!fat16_find_dir_cluster(current_dir_cluster, dst_dir_name, &dst_dir_cluster)) {
            return 0;
        }
        if (!fat16_copy_file_between_dirs(current_dir_cluster, src_name, dst_dir_cluster, src_name)) {
            return 0;
        }
        if (num == APP_SYSCALL_MOVE_TO_DIR) {
            if (!fat16_delete_entry(current_dir_cluster, src_name, 1)) {
                return 0;
            }
        }
        return 1;
    }

    if (num == APP_SYSCALL_CLIP_SET) {
        char src_name[64];
        FAT16_DirectoryEntry entry;
        const char* src_input = (const char*)a0;
        int mode = (int)a1;
        int i = 0;
        if (!src_input || src_input[0] == '\0') {
            return 0;
        }
        while (src_input[i] != '\0' && i < (int)sizeof(src_name) - 1) {
            src_name[i] = src_input[i];
            i++;
        }
        src_name[i] = '\0';
        str_to_upper(src_name);
        if (!fat16_find_entry(current_dir_cluster, src_name, &entry, 0, 0)) {
            return 0;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return 0;
        }
        app_clip_src_cluster = current_dir_cluster;
        for (i = 0; i < (int)sizeof(app_clip_name) - 1 && src_name[i] != '\0'; i++) {
            app_clip_name[i] = src_name[i];
        }
        app_clip_name[i] = '\0';
        app_clip_mode = (mode == 2) ? 2 : 1;
        return 1;
    }

    if (num == APP_SYSCALL_CLIP_PASTE) {
        char dst_dir_name[64];
        unsigned int dst_dir_cluster = current_dir_cluster;
        const char* dst_input = (const char*)a0;
        int i = 0;

        if (app_clip_mode == 0 || app_clip_name[0] == '\0') {
            return 0;
        }

        if (dst_input && dst_input[0] != '\0') {
            while (dst_input[i] != '\0' && i < (int)sizeof(dst_dir_name) - 1) {
                dst_dir_name[i] = dst_input[i];
                i++;
            }
            dst_dir_name[i] = '\0';
            str_to_upper(dst_dir_name);
            if (!fat16_find_dir_cluster(current_dir_cluster, dst_dir_name, &dst_dir_cluster)) {
                return 0;
            }
        }

        if (!fat16_copy_file_between_dirs(app_clip_src_cluster, app_clip_name, dst_dir_cluster, app_clip_name)) {
            return 0;
        }

        if (app_clip_mode == 2) {
            if (!fat16_delete_entry(app_clip_src_cluster, app_clip_name, 1)) {
                return 0;
            }
            app_clip_mode = 0;
            app_clip_name[0] = '\0';
        }
        return 1;
    }

    return -1;
}

static int has_com_extension(const char* name) {
    int len = 0;
    while (name[len] != '\0') {
        len++;
    }

    if (len < 4) {
        return 0;
    }

    char c1 = name[len - 3];
    char c2 = name[len - 2];
    char c3 = name[len - 1];
    if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 - 'A' + 'a');
    if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 - 'A' + 'a');
    if (c3 >= 'A' && c3 <= 'Z') c3 = (char)(c3 - 'A' + 'a');
    return (name[len - 4] == '.'
        && c1 == 'c'
        && c2 == 'o'
        && c3 == 'm');
}

static int has_aut_extension(const char* name) {
    int len = 0;
    while (name[len] != '\0') {
        len++;
    }

    if (len < 4) {
        return 0;
    }

    char c1 = name[len - 3];
    char c2 = name[len - 2];
    char c3 = name[len - 1];
    if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 - 'A' + 'a');
    if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 - 'A' + 'a');
    if (c3 >= 'A' && c3 <= 'Z') c3 = (char)(c3 - 'A' + 'a');
    return (name[len - 4] == '.'
        && c1 == 'a'
        && c2 == 'u'
        && c3 == 't');
}

static int build_com_filename(const char* command, char* out, int out_size) {
    int i = 0;
    if (out_size <= 0) {
        return 0;
    }

    while (command[i] != '\0') {
        if (i >= out_size - 1) {
            return 0;
        }
        out[i] = command[i];
        i++;
    }
    out[i] = '\0';

    if (has_com_extension(out)) {
        return 1;
    }

    if (i + 4 >= out_size) {
        return 0;
    }
    out[i++] = '.';
    out[i++] = 'c';
    out[i++] = 'o';
    out[i++] = 'm';
    out[i] = '\0';
    return 1;
}

static int build_aut_filename(const char* command, char* out, int out_size) {
    int i = 0;
    if (out_size <= 0) {
        return 0;
    }

    while (command[i] != '\0') {
        if (i >= out_size - 1) {
            return 0;
        }
        out[i] = command[i];
        i++;
    }
    out[i] = '\0';

    if (has_aut_extension(out)) {
        return 1;
    }

    if (i + 4 >= out_size) {
        return 0;
    }
    out[i++] = '.';
    out[i++] = 'a';
    out[i++] = 'u';
    out[i++] = 't';
    out[i] = '\0';
    return 1;
}

static int has_elf_extension(const char* name) {
    int len = 0;
    while (name[len] != '\0') {
        len++;
    }

    if (len < 4) {
        return 0;
    }

    char c1 = name[len - 3];
    char c2 = name[len - 2];
    char c3 = name[len - 1];
    if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 - 'A' + 'a');
    if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 - 'A' + 'a');
    if (c3 >= 'A' && c3 <= 'Z') c3 = (char)(c3 - 'A' + 'a');
    return (name[len - 4] == '.'
        && c1 == 'e'
        && c2 == 'l'
        && c3 == 'f');
}

static int build_file_with_extension(const char* command, const char* ext, char* out, int out_size) {
    int i = 0;
    int ext_len = 0;
    if (out_size <= 0) {
        return 0;
    }

    while (command[i] != '\0') {
        if (i >= out_size - 1) {
            return 0;
        }
        out[i] = command[i];
        i++;
    }
    out[i] = '\0';

    while (ext[ext_len] != '\0') {
        ext_len++;
    }

    if (i + ext_len >= out_size) {
        return 0;
    }

    for (int j = 0; j < ext_len; j++) {
        out[i++] = ext[j];
    }
    out[i] = '\0';
    return 1;
}

static void normalize_app_name(const char* input, char* out, int out_size) {
    int j = 0;
    for (int i = 0; input[i] != '\0' && j < out_size - 1; i++) {
        char c = input[i];
        if (c == '.') {
            break;
        }
        if (c == '_' || c == ' ' || c == '\t') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[j++] = c;
        if (j >= 8) {
            break;
        }
    }
    out[j] = '\0';
}

typedef struct {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    unsigned int entry;
    unsigned int phoff;
    unsigned int shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} __attribute__((packed)) elf32_header_t;

typedef struct {
    unsigned int type;
    unsigned int offset;
    unsigned int vaddr;
    unsigned int paddr;
    unsigned int filesz;
    unsigned int memsz;
    unsigned int flags;
    unsigned int align;
} __attribute__((packed)) elf32_program_header_t;

static void mem_copy(unsigned char* dst, const unsigned char* src, unsigned int size) {
    for (unsigned int i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static void mem_zero(unsigned char* dst, unsigned int size) {
    for (unsigned int i = 0; i < size; i++) {
        dst[i] = 0;
    }
}

static int load_elf_and_run(const unsigned char* elf_data, int elf_size, const minidos_app_api_t* api) {
    const elf32_header_t* ehdr;
    typedef int (*app_entry_t)(const minidos_app_api_t* api);
    app_entry_t entry;

    if (elf_size < (int)sizeof(elf32_header_t)) {
        return -1;
    }

    ehdr = (const elf32_header_t*)elf_data;
    if (ehdr->ident[0] != 0x7F || ehdr->ident[1] != 'E' || ehdr->ident[2] != 'L' || ehdr->ident[3] != 'F') {
        return -1;
    }
    if (ehdr->ident[4] != 1 || ehdr->ident[5] != 1 || ehdr->version != 1) {
        return -1;
    }
    if (ehdr->type != 2 || ehdr->machine != 3) {
        return -1;
    }
    if (ehdr->phentsize != sizeof(elf32_program_header_t)) {
        return -1;
    }
    if (ehdr->phnum == 0 || ehdr->phnum > 16) {
        return -1;
    }
    if ((int)ehdr->phoff + (int)(ehdr->phnum * ehdr->phentsize) > elf_size) {
        return -1;
    }

    for (unsigned int i = 0; i < ehdr->phnum; i++) {
        const elf32_program_header_t* phdr = (const elf32_program_header_t*)(elf_data + ehdr->phoff + i * ehdr->phentsize);
        unsigned char* dst;
        const unsigned char* src;
        if (phdr->type != 1) {
            continue;
        }
        if (phdr->memsz == 0) {
            continue;
        }
        if (phdr->memsz < phdr->filesz) {
            return -1;
        }
        if ((int)phdr->offset + (int)phdr->filesz > elf_size) {
            return -1;
        }
        if (phdr->vaddr < 0x200000 || (phdr->vaddr + phdr->memsz) > 0x300000) {
            return -1;
        }

        dst = (unsigned char*)phdr->vaddr;
        src = elf_data + phdr->offset;
        mem_copy(dst, src, phdr->filesz);
        if (phdr->memsz > phdr->filesz) {
            mem_zero(dst + phdr->filesz, phdr->memsz - phdr->filesz);
        }
    }

    if (ehdr->entry < 0x200000 || ehdr->entry >= 0x300000) {
        return -1;
    }

    entry = (app_entry_t)ehdr->entry;
    return entry(api);
}

static int try_execute_com(const char* command, const char* args) {
    char filename[64];
    static unsigned char* const load_addr = (unsigned char*)0x200000;
    static const int max_com_size = 65536;
    minidos_app_api_t api;

    if (command[0] == '\0') {
        return 0;
    }

    if (args[0] != '\0') {
        return 0;
    }

    if (!build_com_filename(command, filename, sizeof(filename))) {
        return 0;
    }

    if (!fat16_initialized) {
        fat16_set_drive(drive_get_current());
        fat16_initialized = fat16_init();
    }
    if (!fat16_initialized) {
        return 0;
    }

    str_to_upper(filename);

    int bytes_read = fat16_read_file_from_dir(current_dir_cluster, filename, load_addr, max_com_size);
    if (bytes_read <= 0) {
        return 0;
    }

    shell_out_both("Executing ");
    shell_out_both(filename);
    shell_out_both("...\n");

    api.syscall = app_api_syscall;
    app_api_begin_input_session();
    app_api_ensure_interrupts_enabled();

    typedef int (*com_entry_t)(const minidos_app_api_t* api);
    com_entry_t entry = (com_entry_t)load_addr;
    int exit_code = entry(&api);

    app_api_note_session_return();
    (void)exit_code;

    return 1;
}

static int try_execute_elf(const char* command, const char* args) {
    char normalized[64];
    char filename[64];
    static unsigned char* const elf_buffer = (unsigned char*)0x300000;
    static const int max_elf_size = 262144;
    minidos_app_api_t api;

    if (command[0] == '\0') {
        return 0;
    }

    if (args[0] != '\0') {
        return 0;
    }

    normalize_app_name(command, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return 0;
    }

    if (has_elf_extension(normalized)) {
        if (!build_file_with_extension(normalized, "", filename, sizeof(filename))) {
            return 0;
        }
    } else {
        if (!build_file_with_extension(normalized, ".elf", filename, sizeof(filename))) {
            return 0;
        }
    }

    if (!fat16_initialized) {
        fat16_set_drive(drive_get_current());
        fat16_initialized = fat16_init();
    }
    if (!fat16_initialized) {
        return 0;
    }

    str_to_upper(filename);
    int bytes_read = fat16_read_file_from_dir(current_dir_cluster, filename, elf_buffer, max_elf_size);
    if (bytes_read <= 0) {
        return 0;
    }

    shell_out_both("Executing ");
    shell_out_both(filename);
    shell_out_both("...\n");

    api.syscall = app_api_syscall;
    app_api_begin_input_session();
    app_api_ensure_interrupts_enabled();
    int exit_code = load_elf_and_run(elf_buffer, bytes_read, &api);
    if (exit_code == -1) {
        shell_out_both("Invalid ELF or load error\n");
        return 1;
    }

    app_api_note_session_return();
    (void)exit_code;
    return 1;
}

static int script_is_space(char c) {
    return c == ' ' || c == '\t';
}

static int script_is_rem_line(const char* line) {
    char a = line[0];
    char b = line[1];
    char c = line[2];
    if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
    if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    if (a == 'R' && b == 'E' && c == 'M') {
        char next = line[3];
        if (next == '\0' || script_is_space(next)) {
            return 1;
        }
    }
    return 0;
}

static int try_execute_aut(const char* command, const char* args) {
    char filename[64];
    static unsigned char script_buffer[2049];
    char line[65];
    int bytes_read;
    int i;

    if (command[0] == '\0') {
        return 0;
    }
    if (args[0] != '\0') {
        return 0;
    }
    if (!build_aut_filename(command, filename, sizeof(filename))) {
        return 0;
    }

    if (!fat16_initialized) {
        fat16_set_drive(drive_get_current());
        fat16_initialized = fat16_init();
    }
    if (!fat16_initialized) {
        return 0;
    }

    str_to_upper(filename);
    bytes_read = fat16_read_file_from_dir(current_dir_cluster, filename, script_buffer, 2048);
    if (bytes_read <= 0) {
        return 0;
    }

    script_buffer[bytes_read] = '\0';
    shell_out_both("Running ");
    shell_out_both(filename);
    shell_out_both("...\n");

    i = 0;
    while (i < bytes_read) {
        int line_len = 0;
        int start;
        int end;

        while (i < bytes_read && script_buffer[i] != '\n') {
            if (script_buffer[i] != '\r' && line_len < (int)sizeof(line) - 1) {
                line[line_len++] = (char)script_buffer[i];
            }
            i++;
        }
        if (i < bytes_read && script_buffer[i] == '\n') {
            i++;
        }
        line[line_len] = '\0';

        start = 0;
        while (line[start] != '\0' && script_is_space(line[start])) {
            start++;
        }
        end = line_len;
        while (end > start && script_is_space(line[end - 1])) {
            end--;
        }
        line[end] = '\0';

        if (line[start] == '\0') {
            continue;
        }
        if (script_is_rem_line(&line[start])) {
            continue;
        }

        shell_execute(&line[start]);
    }

    return 1;
}

void shell_init() {
    show_boot_screen();
    shell_out_both("MiniDOS Shell Ready.\nType 'help' for commands.\n");
    fat16_set_drive(drive_get_current());
}

static int ensure_fat16_ready() {
    if (!fat16_initialized) {
        for (int attempt = 0; attempt < 3 && !fat16_initialized; attempt++) {
            fat16_set_drive(drive_get_current());
            fat16_initialized = fat16_init();
        }
    }
    return fat16_initialized;
}

void shell_prompt() {
    int drive = drive_get_current();
    print_char('A' + drive);
    print_string(":");
    print_string(current_path);
    print_char('>');
}

static void shell_trigger_bsod() {
    video_show_bsod("0E : TEST_BSOD", "Triggered by hidden command BSOD.");

    timer_sleep_ms(1000);

    // Flush stale input (e.g. Enter used to submit the BSOD command)
    while (serial_received()) {
        (void)serial_getchar();
    }
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }

    while (1) {
        if (serial_received()) {
            (void)serial_getchar();
            break;
        }
        if (inb(0x64) & 0x01) {
            (void)inb(0x60);
            break;
        }
        timer_wait_for_interrupt();
    }

    cls();
}

void shell_execute(char* cmd) {
    // Trim whitespace
    int i = 0;
    while (cmd[i] != '\0') i++;
    while (i > 0 && (cmd[i-1] == ' ' || cmd[i-1] == '\t' || cmd[i-1] == '\n')) {
        cmd[--i] = '\0';
    }
    
    // Check if command is drive letter (e.g., "A:" or "a:")
    if ((((cmd[0] >= 'A' && cmd[0] <= 'Z') || (cmd[0] >= 'a' && cmd[0] <= 'z')) &&
         cmd[1] == ':' && cmd[2] == '\0')) {
        char drive_char = cmd[0];
        if (drive_char >= 'a' && drive_char <= 'z') {
            drive_char = (char)(drive_char - 'a' + 'A');
        }
        int drive_letter = drive_char - 'A';
        if (drive_get_info(drive_letter)) {
            drive_set_current(drive_letter);
            fat16_set_drive(drive_letter);
            current_dir_cluster = 0;
            path_reset(current_path);
            fat16_initialized = 0;
            shell_out_both("Switched to drive ");
            shell_out_both_char('A' + drive_letter);
            shell_out_both(":\n");
        } else {
            shell_out_both("Invalid drive\n");
        }
        return;
    }
    
    // Parse command and arguments
    char command[64], args[64];
    parse_command(cmd, command, args);
    str_to_lower(command);
    
    if (mystrcmp(command, "help") == 0) {
        shell_out_both("Available commands:\n");
        shell_out_screen("  dir           - List files\n");
        shell_out_screen("  drives        - List all drives\n");
        shell_out_screen("  type <file>   - View file contents\n");
        shell_out_screen("  echo <text>   - Print text\n");
        shell_out_screen("  cls           - Clear screen\n");
        shell_out_screen("  ver           - Show version\n");
        shell_out_screen("  time [HH:MM[:SS]] - Show/set clock\n");
        shell_out_screen("  date [DD/MM/YYYY] - Show/set date\n");
        shell_out_screen("  mem           - Show system memory\n");
        shell_out_screen("  cd <dir>      - Change directory\n");
        shell_out_screen("  mkdir <dir>   - Create directory\n");
        shell_out_screen("  rmdir <dir>   - Remove empty directory\n");
        shell_out_screen("  del <file>    - Delete file\n");
        shell_out_screen("  copy <a> <b>  - Copy file\n");
        shell_out_screen("  move <a> <b>  - Rename file\n");
        shell_out_screen("  elfls         - List ELF apps in current dir\n");
        shell_out_screen("  run <app>     - Execute ELF app\n");
        shell_out_screen("  dmesg         - Show debug ring buffer\n");
        shell_out_screen("  boot          - Show boot screen\n");
        shell_out_screen("  sleep <ms>    - Pause script execution\n");
        shell_out_screen("  color <rgb>   - Clear screen with color (0xRRGGBB)\n");
        shell_out_screen("  rect <x> <y> <w> <h> <rgb> - Fill rectangle\n");
        shell_out_screen("  box <x> <y> <w> <h> <rgb>  - Draw rectangle border\n");
        shell_out_screen("  text <x> <y> <fg> <bg> <msg> - Draw text\n");
        shell_out_screen("  window <x> <y> <w> <h> <title> - Draw simple window\n");
        shell_out_screen("  help          - This help\n");
    } else if (mystrcmp(command, "echo") == 0) {
        shell_out_both(args);
        shell_out_both("\n");
    } else if (mystrcmp(command, "ver") == 0) {
        shell_out_both("MiniDOS Version 0.1 (MVP) - boot floppy FAT12 + FAT16 runtime\n");
    } else if (mystrcmp(command, "time") == 0) {
        rtc_time_t current_time;
        if (!rtc_read_time(&current_time)) {
            shell_out_both("Unable to read RTC time\n");
            return;
        }

        if (args[0] == '\0') {
            char time_str[9];
            format_time_string(&current_time, time_str);
            shell_out_both("Current time is: ");
            shell_out_both(time_str);
            shell_out_both("\n");
            shell_out_screen("To set a new time, use: time HH:MM[:SS]\n");
            return;
        }

        rtc_time_t new_time;
        if (!parse_time_arg(args, &new_time)) {
            shell_out_both("Invalid time format. Use HH:MM or HH:MM:SS\n");
            return;
        }

        if (!rtc_set_time(&new_time)) {
            shell_out_both("Failed to set RTC time\n");
            return;
        }

        if (!rtc_read_time(&current_time)) {
            shell_out_both("Time updated, but verification read failed\n");
            return;
        }

        char time_str[9];
        format_time_string(&current_time, time_str);
        shell_out_both("Time updated to: ");
        shell_out_both(time_str);
        shell_out_both("\n");
    } else if (mystrcmp(command, "date") == 0) {
        rtc_date_t current_date;
        if (!rtc_read_date(&current_date)) {
            shell_out_both("Unable to read RTC date\n");
            return;
        }

        if (args[0] == '\0') {
            char date_str[11];
            format_date_string(&current_date, date_str);
            shell_out_both("Current date is: ");
            shell_out_both(date_str);
            shell_out_both("\n");
            shell_out_screen("To set a new date, use: date DD/MM/YYYY\n");
            return;
        }

        rtc_date_t new_date;
        if (!parse_date_arg(args, &new_date)) {
            shell_out_both("Invalid date format. Use DD/MM/YYYY\n");
            return;
        }

        if (!rtc_set_date(&new_date)) {
            shell_out_both("Failed to set RTC date\n");
            return;
        }

        if (!rtc_read_date(&current_date)) {
            shell_out_both("Date updated, but verification read failed\n");
            return;
        }

        char date_str[11];
        format_date_string(&current_date, date_str);
        shell_out_both("Date updated to: ");
        shell_out_both(date_str);
        shell_out_both("\n");
    } else if (mystrcmp(command, "mem") == 0) {
        shell_out_both("System Memory: ");
        char mem_str[24];
        unsigned int mem = g_memory_kb;
        format_memory(mem, mem_str);
        shell_out_both(mem_str);
        shell_out_both("\n");
    } else if (mystrcmp(command, "cls") == 0) {
        cls();
    } else if (mystrcmp(command, "boot") == 0) {
        show_boot_screen();
    } else if (mystrcmp(command, "sleep") == 0) {
        unsigned int ms = 0;
        if (!parse_uint_arg(args, &ms)) {
            shell_out_both("Usage: sleep <ms>\n");
            return;
        }
        timer_sleep_ms(ms);
    } else if (mystrcmp(command, "color") == 0) {
        unsigned int rgb = 0;
        if (!parse_uint_arg(args, &rgb)) {
            shell_out_both("Usage: color <rgb>\n");
            return;
        }
        video_clear_color(rgb & 0x00FFFFFFu);
    } else if (mystrcmp(command, "rect") == 0) {
        unsigned int v[5];
        const char* rest = 0;
        if (!parse_n_uints(args, v, 5, &rest) || (rest && rest[0] != '\0')) {
            shell_out_both("Usage: rect <x> <y> <w> <h> <rgb>\n");
            return;
        }
        video_fill_rect((int)v[0], (int)v[1], (int)v[2], (int)v[3], v[4] & 0x00FFFFFFu);
    } else if (mystrcmp(command, "box") == 0) {
        unsigned int v[5];
        const char* rest = 0;
        if (!parse_n_uints(args, v, 5, &rest) || (rest && rest[0] != '\0')) {
            shell_out_both("Usage: box <x> <y> <w> <h> <rgb>\n");
            return;
        }
        if (v[2] == 0 || v[3] == 0) {
            return;
        }
        video_fill_rect((int)v[0], (int)v[1], (int)v[2], 1, v[4] & 0x00FFFFFFu);
        if (v[3] > 1) {
            video_fill_rect((int)v[0], (int)(v[1] + v[3] - 1), (int)v[2], 1, v[4] & 0x00FFFFFFu);
        }
        if (v[3] > 2) {
            video_fill_rect((int)v[0], (int)(v[1] + 1), 1, (int)(v[3] - 2), v[4] & 0x00FFFFFFu);
            if (v[2] > 1) {
                video_fill_rect((int)(v[0] + v[2] - 1), (int)(v[1] + 1), 1, (int)(v[3] - 2), v[4] & 0x00FFFFFFu);
            }
        }
    } else if (mystrcmp(command, "text") == 0) {
        unsigned int v[4];
        const char* rest = 0;
        if (!parse_n_uints(args, v, 4, &rest) || !rest || rest[0] == '\0') {
            shell_out_both("Usage: text <x> <y> <fg> <bg> <message>\n");
            return;
        }
        video_draw_text_at((int)v[0], (int)v[1], rest, v[2] & 0x00FFFFFFu, v[3] & 0x00FFFFFFu);
    } else if (mystrcmp(command, "window") == 0) {
        unsigned int v[4];
        const char* title = 0;
        if (!parse_n_uints(args, v, 4, &title) || !title || title[0] == '\0') {
            shell_out_both("Usage: window <x> <y> <w> <h> <title>\n");
            return;
        }
        if (v[2] < 16 || v[3] < 16) {
            shell_out_both("window: minimum size is 16x16\n");
            return;
        }
        video_fill_rect((int)v[0], (int)v[1], (int)v[2], (int)v[3], 0xC0C0C0u);
        video_fill_rect((int)v[0] + 1, (int)v[1] + 1, (int)v[2] - 2, (int)v[3] - 2, 0x000080u);
        video_fill_rect((int)v[0] + 3, (int)v[1] + 3, (int)v[2] - 6, 14, 0xC0C0C0u);
        video_fill_rect((int)v[0] + 3, (int)v[1] + 19, (int)v[2] - 6, (int)v[3] - 22, 0xFFFFFFu);
        video_draw_text_at((int)v[0] + 8, (int)v[1] + 6, title, 0x000000u, 0xC0C0C0u);
    } else if (mystrcmp(command, "drives") == 0) {
        drive_list_all();
    } else if (mystrcmp(command, "dmesg") == 0) {
        log_dump_buffer(LOG_DEST_BOTH);
    } else if (mystrcmp(command, "dir") == 0) {
        if (!ensure_fat16_ready()) {
            shell_out_both("No disk or FAT16 partition found on current drive\n");
            return;
        }
        shell_out_both("Directory of ");
        shell_out_both_char('A' + drive_get_current());
        shell_out_both(":");
        shell_out_both(current_path);
        if (args[0] != '\0') {
            char filter_upper[64];
            str_copy_upper(args, filter_upper, sizeof(filter_upper));
            shell_out_both(" [");
            shell_out_both(filter_upper);
            shell_out_both("]");
        }
        shell_out_both("\n\n");
        if (current_dir_cluster == 0) {
            if (args[0] == '\0') {
                fat16_list_root();
            } else {
                char filter_upper[64];
                str_copy_upper(args, filter_upper, sizeof(filter_upper));
                fat16_list_root_filtered(filter_upper);
            }
        } else {
            if (args[0] == '\0') {
                fat16_list_dir(current_dir_cluster);
            } else {
                char filter_upper[64];
                str_copy_upper(args, filter_upper, sizeof(filter_upper));
                fat16_list_dir_filtered(current_dir_cluster, filter_upper);
            }
        }
    } else if (mystrcmp(command, "cd") == 0) {
        if (!ensure_fat16_ready()) {
            shell_out_both("No disk or FAT16 partition found on current drive\n");
            return;
        }
        if (args[0] == '\0') {
            print_char('A' + drive_get_current());
            print_string(":");
            print_string(current_path);
            print_string("\n");
        } else {
            if (args[0] == '\\' || args[0] == '/') {
                current_dir_cluster = 0;
                path_reset(current_path);
            } else if (args[0] == '.' && args[1] == '.' && args[2] == '\0') {
                unsigned int parent_cluster = 0;
                if (fat16_get_parent_cluster(current_dir_cluster, &parent_cluster)) {
                    current_dir_cluster = parent_cluster;
                    path_pop(current_path);
                } else {
                    print_string("Path not found\n");
                }
            } else if (args[0] == '.' && args[1] == '\0') {
                // Stay in current directory
            } else {
                char dir_upper[64];
                str_copy_upper(args, dir_upper, sizeof(dir_upper));

                unsigned int next_cluster = 0;
                if (fat16_find_dir_cluster(current_dir_cluster, dir_upper, &next_cluster)) {
                    if (path_push(current_path, (int)sizeof(current_path), dir_upper)) {
                        current_dir_cluster = next_cluster;
                    } else {
                        print_string("Path too long\n");
                    }
                } else {
                    print_string("Path not found\n");
                }
            }
        }
    } else if (mystrcmp(command, "type") == 0) {
        if (args[0] == '\0') {
            print_string("Usage: type <filename>\n");
        } else {
            if (!ensure_fat16_ready()) {
                shell_out_both("No disk or FAT16 partition found on current drive\n");
                return;
            }
            char file_upper[64];
            str_copy_upper(args, file_upper, sizeof(file_upper));
            
            static unsigned char file_buffer[8192];
            int bytes_read = fat16_read_file_from_dir(current_dir_cluster, file_upper, file_buffer, sizeof(file_buffer));
            
            if (bytes_read > 0) {
                // Print file contents
                for (int j = 0; j < bytes_read; j++) {
                    unsigned char c = file_buffer[j];
                    if (c == '\n') {
                        print_char('\n');
                    } else if (c >= 32 && c < 127) {
                        print_char(c);
                    } else if (c == '\r') {
                        // Skip CR, will be handled by LF
                    } else if (c == '\t') {
                        print_char(' ');
                        print_char(' ');
                    } else {
                        print_char('.');
                    }
                }
                print_char('\n');
            } else {
                print_string("File not found: ");
                print_string(file_upper);
                print_char('\n');
            }
        }
    } else if (mystrcmp(command, "mkdir") == 0) {
        if (args[0] == '\0') {
            shell_out_both("Usage: mkdir <dirname>\n");
        } else {
            if (!ensure_fat16_ready()) {
                shell_out_both("No disk or FAT16 partition found on current drive\n");
                return;
            }

            char name_upper[64];
            str_copy_upper(args, name_upper, sizeof(name_upper));

            if (fat16_mkdir(current_dir_cluster, name_upper)) {
                shell_out_both("Directory created\n");
            } else {
                shell_out_both("Failed to create directory\n");
            }
        }
    } else if (mystrcmp(command, "rmdir") == 0) {
        if (args[0] == '\0') {
            shell_out_both("Usage: rmdir <dirname>\n");
        } else {
            if (!ensure_fat16_ready()) {
                shell_out_both("No disk or FAT16 partition found on current drive\n");
                return;
            }

            char name_upper[64];
            str_copy_upper(args, name_upper, sizeof(name_upper));

            if (fat16_rmdir(current_dir_cluster, name_upper)) {
                shell_out_both("Directory removed\n");
            } else {
                shell_out_both("Failed to remove directory\n");
            }
        }
    } else if (mystrcmp(command, "del") == 0 || mystrcmp(command, "rm") == 0) {
        if (args[0] == '\0') {
            shell_out_both("Usage: del <filename>\n");
        } else {
            if (!ensure_fat16_ready()) {
                shell_out_both("No disk or FAT16 partition found on current drive\n");
                return;
            }

            char pattern_upper[64];
            str_copy_upper(args, pattern_upper, sizeof(pattern_upper));

            if (has_wildcards(pattern_upper)) {
                int deleted_count = 0;
                if (fat16_delete_matching(current_dir_cluster, pattern_upper, 1, &deleted_count)) {
                    if (deleted_count > 0) {
                        shell_out_both("Deleted ");
                        if (deleted_count < 10) {
                            char n[2];
                            n[0] = (char)('0' + deleted_count);
                            n[1] = '\0';
                            shell_out_both(n);
                        } else {
                            char n[12];
                            int len = uint_to_dec((unsigned int)deleted_count, n);
                            n[len] = '\0';
                            shell_out_both(n);
                        }
                        shell_out_both(" file(s)\n");
                    } else {
                        shell_out_both("No files matched\n");
                    }
                } else {
                    shell_out_both("Failed to delete files\n");
                }
            } else if (fat16_delete_entry(current_dir_cluster, pattern_upper, 1)) {
                shell_out_both("File deleted\n");
            } else {
                shell_out_both("Failed to delete file\n");
            }
        }
    } else if (mystrcmp(command, "copy") == 0 || mystrcmp(command, "cp") == 0) {
        char src_name[64];
        char dst_name[64];

        if (!parse_two_args(args, src_name, sizeof(src_name), dst_name, sizeof(dst_name))) {
            shell_out_both("Usage: copy <source> <destination>\n");
        } else {
            if (!ensure_fat16_ready()) {
                shell_out_both("No disk or FAT16 partition found on current drive\n");
                return;
            }

            str_to_upper(src_name);
            str_to_upper(dst_name);

            if (fat16_copy_file(current_dir_cluster, src_name, dst_name)) {
                shell_out_both("File copied\n");
            } else {
                shell_out_both("Failed to copy file\n");
            }
        }
    } else if (mystrcmp(command, "move") == 0 || mystrcmp(command, "mv") == 0 || mystrcmp(command, "ren") == 0) {
        char src_name[64];
        char dst_name[64];
        FAT16_DirectoryEntry src_entry;

        if (!parse_two_args(args, src_name, sizeof(src_name), dst_name, sizeof(dst_name))) {
            shell_out_both("Usage: move <source> <destination>\n");
        } else {
            if (!ensure_fat16_ready()) {
                shell_out_both("No disk or FAT16 partition found on current drive\n");
                return;
            }

            str_to_upper(src_name);
            str_to_upper(dst_name);

            if (!fat16_find_entry(current_dir_cluster, src_name, &src_entry, 0, 0)) {
                shell_out_both("File not found\n");
            } else if (src_entry.attributes & FAT16_ATTR_DIRECTORY) {
                shell_out_both("Cannot move directory with this command\n");
            } else if (fat16_update_entry(current_dir_cluster, src_name, dst_name, src_entry.cluster_low, src_entry.file_size, src_entry.attributes)) {
                shell_out_both("File moved\n");
            } else {
                shell_out_both("Failed to move file\n");
            }
        }
    } else if (mystrcmp(command, "elfls") == 0) {
        if (!ensure_fat16_ready()) {
            shell_out_both("No disk or FAT16 partition found on current drive\n");
            return;
        }
        shell_out_both("ELF apps in ");
        shell_out_both_char('A' + drive_get_current());
        shell_out_both(":");
        shell_out_both(current_path);
        shell_out_both("\n\n");
        if (current_dir_cluster == 0) {
            fat16_list_root_filtered("*.ELF");
        } else {
            fat16_list_dir_filtered(current_dir_cluster, "*.ELF");
        }
    } else if (mystrcmp(command, "run") == 0) {
        char app_name[64];
        if (!parse_single_arg(args, app_name, sizeof(app_name))) {
            shell_out_both("Usage: run <app>\n");
            return;
        }
        str_to_upper(app_name);
        if (!try_execute_elf(app_name, "")) {
            shell_out_both("ELF app not found or failed to load\n");
        }
    } else if (mystrcmp(command, "bsod") == 0) {
        shell_trigger_bsod();
    } else if (cmd[0] != '\0') {
        if (try_execute_elf(command, args) || try_execute_com(command, args) || try_execute_aut(command, args)) {
            return;
        }
        print_string("Bad command or file name\n");
    }
}
