#include "shell_builtin.h"
#include "shell.h"
#include "logger.h"
#include "rtc.h"
#include "serial.h"
#include "timer.h"
#include "video.h"

static const unsigned int SHELL_BOOT_STEP_MS = 50;
static const unsigned int SHELL_BOOT_FINAL_MS = 100;

static int shell_builtin_host_valid(const shell_builtin_host_t* host) {
    return host && host->out_screen && host->out_both;
}

static int shell_builtin_mystrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static int shell_builtin_uint_to_dec(unsigned int value, char* out) {
    char tmp[16];
    int len = 0;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }

    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    for (int i = 0; i < len; i++) {
        out[i] = tmp[len - 1 - i];
    }

    return len;
}

static void shell_builtin_format_memory(unsigned int kb, char* out) {
    const char* unit = "KB";
    unsigned int unit_kb = 1;
    int pos = 0;

    if (kb >= 1024U * 1024U) {
        unit = "GB";
        unit_kb = 1024U * 1024U;
    } else if (kb >= 1024U) {
        unit = "MB";
        unit_kb = 1024U;
    }

    if (unit_kb == 1) {
        pos += shell_builtin_uint_to_dec(kb, out + pos);
    } else {
        unsigned int tenths = (kb * 10 + unit_kb / 2) / unit_kb;
        unsigned int whole = tenths / 10;
        unsigned int frac = tenths % 10;

        pos += shell_builtin_uint_to_dec(whole, out + pos);
        out[pos++] = '.';
        out[pos++] = (char)('0' + frac);
    }

    out[pos++] = ' ';
    out[pos++] = unit[0];
    out[pos++] = unit[1];
    out[pos] = '\0';
}

static void shell_builtin_append_two_digits(char* out, int* pos, unsigned char value) {
    out[(*pos)++] = (char)('0' + (value / 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

static void shell_builtin_append_four_digits(char* out, int* pos, unsigned short value) {
    out[(*pos)++] = (char)('0' + ((value / 1000) % 10));
    out[(*pos)++] = (char)('0' + ((value / 100) % 10));
    out[(*pos)++] = (char)('0' + ((value / 10) % 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

static void shell_builtin_format_time_string(const rtc_time_t* time, char* out) {
    int pos = 0;
    shell_builtin_append_two_digits(out, &pos, time->hours);
    out[pos++] = ':';
    shell_builtin_append_two_digits(out, &pos, time->minutes);
    out[pos++] = ':';
    shell_builtin_append_two_digits(out, &pos, time->seconds);
    out[pos] = '\0';
}

static void shell_builtin_format_date_string(const rtc_date_t* date, char* out) {
    int pos = 0;
    shell_builtin_append_two_digits(out, &pos, date->day);
    out[pos++] = '/';
    shell_builtin_append_two_digits(out, &pos, date->month);
    out[pos++] = '/';
    shell_builtin_append_four_digits(out, &pos, date->year);
    out[pos] = '\0';
}

static int shell_builtin_parse_time_arg(const char* args, rtc_time_t* time) {
    int values[3] = {0, 0, 0};
    int value_count = 0;
    int current_value = 0;
    int digits = 0;
    int i = 0;

    if (!args || !time) {
        return 0;
    }

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
    return time->seconds <= 59;
}

static int shell_builtin_parse_date_arg(const char* args, rtc_date_t* date) {
    int values[3] = {0, 0, 0};
    int digit_counts[3] = {0, 0, 0};
    int value_index = 0;
    int i = 0;
    unsigned short year;

    if (!args || !date) {
        return 0;
    }

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

static int shell_builtin_parse_uint_arg(const char* s, unsigned int* out) {
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

static int shell_builtin_parse_n_uints(const char* args, unsigned int* out, int count, const char** rest) {
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

        if (!shell_builtin_parse_uint_arg(tok, &out[n])) {
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

static void shell_builtin_trigger_bsod(void) {
    video_show_bsod("0E : TEST_BSOD", "Triggered by hidden command BSOD.");

    timer_sleep_ms(1000);

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

void shell_builtin_show_boot_screen(void) {
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

static int shell_builtin_cmd_help(const shell_builtin_host_t* host) {
    host->out_both("Available commands:\n");
    host->out_screen("  dir           - List files\n");
    host->out_screen("  drives        - List all drives\n");
    host->out_screen("  type <file>   - View file contents\n");
    host->out_screen("  echo <text>   - Print text\n");
    host->out_screen("  cls           - Clear screen\n");
    host->out_screen("  ver           - Show version\n");
    host->out_screen("  time [HH:MM[:SS]] - Show/set clock\n");
    host->out_screen("  date [DD/MM/YYYY] - Show/set date\n");
    host->out_screen("  mem           - Show system memory\n");
    host->out_screen("  cd <dir>      - Change directory\n");
    host->out_screen("  mkdir <dir>   - Create directory\n");
    host->out_screen("  rmdir <dir>   - Remove empty directory\n");
    host->out_screen("  del <file>    - Delete file\n");
    host->out_screen("  copy <a> <b>  - Copy file\n");
    host->out_screen("  move <a> <b>  - Rename file\n");
    host->out_screen("  elfls         - List ELF apps in current dir\n");
    host->out_screen("  run <app>     - Execute ELF app\n");
    host->out_screen("  dmesg         - Show debug ring buffer\n");
    host->out_screen("  boot          - Show boot screen\n");
    host->out_screen("  sleep <ms>    - Pause script execution\n");
    host->out_screen("  color <rgb>   - Clear screen with color (0xRRGGBB)\n");
    host->out_screen("  rect <x> <y> <w> <h> <rgb> - Fill rectangle\n");
    host->out_screen("  box <x> <y> <w> <h> <rgb>  - Draw rectangle border\n");
    host->out_screen("  text <x> <y> <fg> <bg> <msg> - Draw text\n");
    host->out_screen("  window <x> <y> <w> <h> <title> - Draw simple window\n");
    host->out_screen("  help          - This help\n");
    return 1;
}

static int shell_builtin_cmd_time(const shell_builtin_host_t* host, const char* args) {
    rtc_time_t current_time;

    if (!rtc_read_time(&current_time)) {
        host->out_both("Unable to read RTC time\n");
        return 1;
    }

    if (args[0] == '\0') {
        char time_str[9];
        shell_builtin_format_time_string(&current_time, time_str);
        host->out_both("Current time is: ");
        host->out_both(time_str);
        host->out_both("\n");
        host->out_screen("To set a new time, use: time HH:MM[:SS]\n");
        return 1;
    }

    {
        rtc_time_t new_time;
        char time_str[9];

        if (!shell_builtin_parse_time_arg(args, &new_time)) {
            host->out_both("Invalid time format. Use HH:MM or HH:MM:SS\n");
            return 1;
        }

        if (!rtc_set_time(&new_time)) {
            host->out_both("Failed to set RTC time\n");
            return 1;
        }

        if (!rtc_read_time(&current_time)) {
            host->out_both("Time updated, but verification read failed\n");
            return 1;
        }

        shell_builtin_format_time_string(&current_time, time_str);
        host->out_both("Time updated to: ");
        host->out_both(time_str);
        host->out_both("\n");
    }

    return 1;
}

static int shell_builtin_cmd_date(const shell_builtin_host_t* host, const char* args) {
    rtc_date_t current_date;

    if (!rtc_read_date(&current_date)) {
        host->out_both("Unable to read RTC date\n");
        return 1;
    }

    if (args[0] == '\0') {
        char date_str[11];
        shell_builtin_format_date_string(&current_date, date_str);
        host->out_both("Current date is: ");
        host->out_both(date_str);
        host->out_both("\n");
        host->out_screen("To set a new date, use: date DD/MM/YYYY\n");
        return 1;
    }

    {
        rtc_date_t new_date;
        char date_str[11];

        if (!shell_builtin_parse_date_arg(args, &new_date)) {
            host->out_both("Invalid date format. Use DD/MM/YYYY\n");
            return 1;
        }

        if (!rtc_set_date(&new_date)) {
            host->out_both("Failed to set RTC date\n");
            return 1;
        }

        if (!rtc_read_date(&current_date)) {
            host->out_both("Date updated, but verification read failed\n");
            return 1;
        }

        shell_builtin_format_date_string(&current_date, date_str);
        host->out_both("Date updated to: ");
        host->out_both(date_str);
        host->out_both("\n");
    }

    return 1;
}

static int shell_builtin_cmd_mem(const shell_builtin_host_t* host) {
    char mem_str[24];

    shell_builtin_format_memory(g_memory_kb, mem_str);
    host->out_both("System Memory: ");
    host->out_both(mem_str);
    host->out_both("\n");
    return 1;
}

static int shell_builtin_cmd_sleep(const shell_builtin_host_t* host, const char* args) {
    unsigned int ms = 0;

    if (!shell_builtin_parse_uint_arg(args, &ms)) {
        host->out_both("Usage: sleep <ms>\n");
        return 1;
    }

    timer_sleep_ms(ms);
    return 1;
}

static int shell_builtin_cmd_color(const shell_builtin_host_t* host, const char* args) {
    unsigned int rgb = 0;

    if (!shell_builtin_parse_uint_arg(args, &rgb)) {
        host->out_both("Usage: color <rgb>\n");
        return 1;
    }

    video_clear_color(rgb & 0x00FFFFFFu);
    return 1;
}

static int shell_builtin_cmd_rect(const shell_builtin_host_t* host, const char* args) {
    unsigned int v[5];
    const char* rest = 0;

    if (!shell_builtin_parse_n_uints(args, v, 5, &rest) || (rest && rest[0] != '\0')) {
        host->out_both("Usage: rect <x> <y> <w> <h> <rgb>\n");
        return 1;
    }

    video_fill_rect((int)v[0], (int)v[1], (int)v[2], (int)v[3], v[4] & 0x00FFFFFFu);
    return 1;
}

static int shell_builtin_cmd_box(const shell_builtin_host_t* host, const char* args) {
    unsigned int v[5];
    const char* rest = 0;

    if (!shell_builtin_parse_n_uints(args, v, 5, &rest) || (rest && rest[0] != '\0')) {
        host->out_both("Usage: box <x> <y> <w> <h> <rgb>\n");
        return 1;
    }

    if (v[2] == 0 || v[3] == 0) {
        return 1;
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
    return 1;
}

static int shell_builtin_cmd_text(const shell_builtin_host_t* host, const char* args) {
    unsigned int v[4];
    const char* rest = 0;

    if (!shell_builtin_parse_n_uints(args, v, 4, &rest) || !rest || rest[0] == '\0') {
        host->out_both("Usage: text <x> <y> <fg> <bg> <message>\n");
        return 1;
    }

    video_draw_text_at((int)v[0], (int)v[1], rest, v[2] & 0x00FFFFFFu, v[3] & 0x00FFFFFFu);
    return 1;
}

static int shell_builtin_cmd_window(const shell_builtin_host_t* host, const char* args) {
    unsigned int v[4];
    const char* title = 0;

    if (!shell_builtin_parse_n_uints(args, v, 4, &title) || !title || title[0] == '\0') {
        host->out_both("Usage: window <x> <y> <w> <h> <title>\n");
        return 1;
    }

    if (v[2] < 16 || v[3] < 16) {
        host->out_both("window: minimum size is 16x16\n");
        return 1;
    }

    video_fill_rect((int)v[0], (int)v[1], (int)v[2], (int)v[3], 0xC0C0C0u);
    video_fill_rect((int)v[0] + 1, (int)v[1] + 1, (int)v[2] - 2, (int)v[3] - 2, 0x000080u);
    video_fill_rect((int)v[0] + 3, (int)v[1] + 3, (int)v[2] - 6, 14, 0xC0C0C0u);
    video_fill_rect((int)v[0] + 3, (int)v[1] + 19, (int)v[2] - 6, (int)v[3] - 22, 0xFFFFFFu);
    video_draw_text_at((int)v[0] + 8, (int)v[1] + 6, title, 0x000000u, 0xC0C0C0u);
    return 1;
}

int shell_builtin_try_execute(const shell_builtin_host_t* host, const char* command, const char* args) {
    if (!shell_builtin_host_valid(host) || !command || !args) {
        return 0;
    }

    if (shell_builtin_mystrcmp(command, "help") == 0) {
        return shell_builtin_cmd_help(host);
    }
    if (shell_builtin_mystrcmp(command, "echo") == 0) {
        host->out_both(args);
        host->out_both("\n");
        return 1;
    }
    if (shell_builtin_mystrcmp(command, "ver") == 0) {
        host->out_both("MiniDOS Version 0.1 (MVP) - boot floppy FAT12 + FAT16 runtime\n");
        return 1;
    }
    if (shell_builtin_mystrcmp(command, "time") == 0) {
        return shell_builtin_cmd_time(host, args);
    }
    if (shell_builtin_mystrcmp(command, "date") == 0) {
        return shell_builtin_cmd_date(host, args);
    }
    if (shell_builtin_mystrcmp(command, "mem") == 0) {
        return shell_builtin_cmd_mem(host);
    }
    if (shell_builtin_mystrcmp(command, "cls") == 0) {
        cls();
        return 1;
    }
    if (shell_builtin_mystrcmp(command, "boot") == 0) {
        shell_builtin_show_boot_screen();
        return 1;
    }
    if (shell_builtin_mystrcmp(command, "sleep") == 0) {
        return shell_builtin_cmd_sleep(host, args);
    }
    if (shell_builtin_mystrcmp(command, "color") == 0) {
        return shell_builtin_cmd_color(host, args);
    }
    if (shell_builtin_mystrcmp(command, "rect") == 0) {
        return shell_builtin_cmd_rect(host, args);
    }
    if (shell_builtin_mystrcmp(command, "box") == 0) {
        return shell_builtin_cmd_box(host, args);
    }
    if (shell_builtin_mystrcmp(command, "text") == 0) {
        return shell_builtin_cmd_text(host, args);
    }
    if (shell_builtin_mystrcmp(command, "window") == 0) {
        return shell_builtin_cmd_window(host, args);
    }
    if (shell_builtin_mystrcmp(command, "dmesg") == 0) {
        log_dump_buffer(LOG_DEST_BOTH);
        return 1;
    }
    if (shell_builtin_mystrcmp(command, "bsod") == 0) {
        shell_builtin_trigger_bsod();
        return 1;
    }

    return 0;
}
