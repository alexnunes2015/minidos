#include "shell_builtin.h"
#include "shell.h"
#include "logger.h"
#include "rtc.h"
#include "scheduler.h"
#include "serial.h"
#include "timer.h"
#include "video.h"

static const unsigned int SHELL_BOOT_STEP_MS = 50;
static const unsigned int SHELL_BOOT_FINAL_MS = 100;
static const unsigned int TOP_DEFAULT_INTERVAL_MS = 200U;
static const unsigned int TOP_DEFAULT_SAMPLES = 1U;

#define PROC_COL_PID 4
#define PROC_COL_NAME 12
#define PROC_COL_STATE 10
#define PROC_COL_MEM 8
#define PROC_COL_CPU 6
#define PROC_COL_EXE 14

static int shell_builtin_parse_uint_arg(const char* s, unsigned int* out);

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

static void shell_builtin_copy_string(const char* src, char* dst, int dst_size) {
    int i = 0;

    if (!dst || dst_size <= 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i < dst_size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int shell_builtin_string_length(const char* s) {
    int len = 0;

    if (!s) {
        return 0;
    }
    while (s[len] != '\0') {
        len++;
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

static void shell_builtin_format_percent_tenths(unsigned int value_tenths, char* out) {
    unsigned int whole = value_tenths / 10U;
    unsigned int frac = value_tenths % 10U;
    int pos = 0;

    pos += shell_builtin_uint_to_dec(whole, out + pos);
    out[pos++] = '.';
    out[pos++] = (char)('0' + frac);
    out[pos++] = '%';
    out[pos] = '\0';
}

static void shell_builtin_format_stack_reserve(const scheduler_process_snapshot_t* snap, char* out, int out_size) {
    if (!snap || !out || out_size <= 0) {
        return;
    }

    if (snap->kernel_stack_base == 0 || snap->kernel_stack_top == 0) {
        shell_builtin_copy_string("-", out, out_size);
        return;
    }

    shell_builtin_format_memory(scheduler_get_kernel_stack_bytes() / 1024U, out);
}

static void shell_builtin_format_origin(const scheduler_process_snapshot_t* snap, char* out, int out_size) {
    if (!snap || !out || out_size <= 0) {
        return;
    }

    if (!snap->origin_is_executable || !snap->origin_name || snap->origin_name[0] == '\0') {
        shell_builtin_copy_string("-", out, out_size);
        return;
    }

    shell_builtin_copy_string(snap->origin_name, out, out_size);
}

static void shell_builtin_append_cell(char* line, int* pos, int line_size, const char* text, int width, int right_align) {
    int len;
    int visible;

    if (!line || !pos || line_size <= 0 || width <= 0) {
        return;
    }

    if (!text) {
        text = "-";
    }

    len = shell_builtin_string_length(text);
    visible = (len < width) ? len : width;

    if (right_align) {
        while (visible < width && *pos < line_size - 1) {
            line[(*pos)++] = ' ';
            visible++;
        }
        visible = (len < width) ? len : width;
    }

    for (int i = 0; i < visible && *pos < line_size - 1; i++) {
        line[(*pos)++] = text[i];
    }

    if (!right_align) {
        for (int i = visible; i < width && *pos < line_size - 1; i++) {
            line[(*pos)++] = ' ';
        }
    }

    if (*pos < line_size - 1) {
        line[(*pos)++] = ' ';
    }
}

static void shell_builtin_print_process_table_header(const shell_builtin_host_t* host) {
    char line[96];
    int pos = 0;

    if (!host) {
        return;
    }

    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "PID", PROC_COL_PID, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "NAME", PROC_COL_NAME, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "STATE", PROC_COL_STATE, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "MEM", PROC_COL_MEM, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "CPU", PROC_COL_CPU, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "EXE", PROC_COL_EXE, 0);
    if (pos > 0) {
        pos--;
    }
    line[pos++] = '\n';
    line[pos] = '\0';
    host->out_both(line);
}

static void shell_builtin_print_process_row(const shell_builtin_host_t* host, const scheduler_process_snapshot_t* snap, unsigned int cpu_tenths) {
    char line[128];
    char pid_str[16];
    char cpu_str[16];
    char stack_str[24];
    char origin_str[24];
    int pos = 0;

    if (!host || !snap) {
        return;
    }

    {
        int len = shell_builtin_uint_to_dec((unsigned int)snap->pid, pid_str);
        pid_str[len] = '\0';
    }
    shell_builtin_format_percent_tenths(cpu_tenths, cpu_str);
    shell_builtin_format_stack_reserve(snap, stack_str, (int)sizeof(stack_str));
    shell_builtin_format_origin(snap, origin_str, (int)sizeof(origin_str));

    shell_builtin_append_cell(line, &pos, (int)sizeof(line), pid_str, PROC_COL_PID, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), snap->name ? snap->name : "unknown", PROC_COL_NAME, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), process_state_name(snap->state), PROC_COL_STATE, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), stack_str, PROC_COL_MEM, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), cpu_str, PROC_COL_CPU, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), origin_str, PROC_COL_EXE, 0);
    if (pos > 0) {
        pos--;
    }
    line[pos++] = '\n';
    line[pos] = '\0';
    host->out_both(line);
}

static int shell_builtin_parse_top_args(const char* args, unsigned int* interval_ms, unsigned int* samples) {
    int i = 0;
    int field = 0;
    char token[32];

    if (!args || !interval_ms || !samples) {
        return 0;
    }

    *interval_ms = TOP_DEFAULT_INTERVAL_MS;
    *samples = TOP_DEFAULT_SAMPLES;

    while (args[i] != '\0') {
        int j = 0;
        unsigned int value = 0;

        while (args[i] == ' ' || args[i] == '\t') {
            i++;
        }
        if (args[i] == '\0') {
            break;
        }
        if (field >= 2) {
            return 0;
        }

        while (args[i] != '\0' && args[i] != ' ' && args[i] != '\t') {
            if (j >= (int)sizeof(token) - 1) {
                return 0;
            }
            token[j++] = args[i++];
        }
        token[j] = '\0';

        if (!shell_builtin_parse_uint_arg(token, &value)) {
            return 0;
        }

        if (field == 0) {
            *interval_ms = value;
        } else {
            *samples = value;
        }
        field++;
    }

    if (*interval_ms == 0) {
        *interval_ms = 1U;
    }
    if (*samples == 0) {
        *samples = 1U;
    }

    return 1;
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
    host->out_screen("  ps            - Show task table (PID/name/state/mem/cpu/exe)\n");
    host->out_screen("  top [ms] [n]  - Sample task table with CPU usage\n");
    host->out_screen("  cd <dir>      - Change directory\n");
    host->out_screen("  mkdir <dir>   - Create directory\n");
    host->out_screen("  rmdir <dir>   - Remove empty directory\n");
    host->out_screen("  del <file>    - Delete file\n");
    host->out_screen("  copy <a> <b>  - Copy file\n");
    host->out_screen("  move <a> <b>  - Rename file\n");
    host->out_screen("  elfls         - List ELF apps in current dir\n");
    host->out_screen("  run <app>     - Execute ELF app\n");
    host->out_screen("  runbg <app>   - Start ELF app as background process\n");
    host->out_screen("  kill <pid>    - Stop background app group (main + threads)\n");
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

static int shell_builtin_cmd_ps(const shell_builtin_host_t* host, const char* args) {
    scheduler_process_snapshot_t snaps[16];
    int count;
    unsigned int total_ticks = 0;

    if (args[0] != '\0') {
        host->out_both("Usage: ps\n");
        return 1;
    }

    count = scheduler_snapshot_processes(snaps, (int)(sizeof(snaps) / sizeof(snaps[0])), 0);

    if (count <= 0) {
        host->out_both("no scheduler-visible tasks\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        total_ticks += snaps[i].runtime_ticks;
    }

    shell_builtin_print_process_table_header(host);
    for (int i = 0; i < count; i++) {
        unsigned int cpu_tenths = 0;

        if (total_ticks > 0) {
            cpu_tenths = (snaps[i].runtime_ticks * 1000U + (total_ticks / 2U)) / total_ticks;
        }
        shell_builtin_print_process_row(host, &snaps[i], cpu_tenths);
    }

    return 1;
}

static int shell_builtin_cmd_top(const shell_builtin_host_t* host, const char* args) {
    scheduler_process_snapshot_t before[16];
    scheduler_process_snapshot_t after[16];
    unsigned int before_ticks[16];
    unsigned int interval_ms;
    unsigned int samples;
    int count;

    if (!shell_builtin_parse_top_args(args, &interval_ms, &samples)) {
        host->out_both("Usage: top [interval_ms] [samples]\n");
        return 1;
    }

    for (unsigned int sample = 0; sample < samples; sample++) {
        unsigned int sleep_ticks;
        unsigned int total_delta = 0;

        count = scheduler_snapshot_processes(before, (int)(sizeof(before) / sizeof(before[0])), 0);
        for (int i = 0; i < count; i++) {
            before_ticks[i] = before[i].runtime_ticks;
        }

        sleep_ticks = timer_ms_to_ticks_ceil(interval_ms);
        if (sleep_ticks == 0) {
            sleep_ticks = 1;
        }
        scheduler_sleep_ticks(sleep_ticks);

        count = scheduler_snapshot_processes(after, (int)(sizeof(after) / sizeof(after[0])), 0);
        for (int i = 0; i < count; i++) {
            unsigned int delta = after[i].runtime_ticks - before_ticks[i];
            total_delta += delta;
        }

        if (sample > 0U) {
            host->out_both("\n");
        }

        shell_builtin_print_process_table_header(host);
        for (int i = 0; i < count; i++) {
            unsigned int delta = after[i].runtime_ticks - before_ticks[i];
            unsigned int cpu_tenths = 0;

            if (total_delta > 0) {
                cpu_tenths = (delta * 1000U + (total_delta / 2U)) / total_delta;
            }
            shell_builtin_print_process_row(host, &after[i], cpu_tenths);
        }
    }

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
    if (shell_builtin_mystrcmp(command, "ps") == 0) {
        return shell_builtin_cmd_ps(host, args);
    }
    if (shell_builtin_mystrcmp(command, "top") == 0) {
        return shell_builtin_cmd_top(host, args);
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
