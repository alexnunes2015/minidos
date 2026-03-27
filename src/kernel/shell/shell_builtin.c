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

/* Keep the shell split by responsibility without changing the final link unit. */
#include "shell_builtin_clock.inc"
#include "shell_builtin_process.inc"

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
    host->out_screen("  run <app>     - Execute ELF or COM app\n");
    host->out_screen("  runbg <app>   - Start ELF or COM app in background\n");
    host->out_screen("  kill <pid>    - Stop background app group (main + threads)\n");
    host->out_screen("  dmesg         - Show debug ring buffer\n");
    host->out_screen("  boot          - Show boot screen\n");
    host->out_screen("  sleep <ms>    - Pause script execution\n");
    host->out_screen("  color <rgb>   - Clear screen with color (0xRRGGBB)\n");
    host->out_screen("  rect <x> <y> <w> <h> <rgb> - Fill rectangle\n");
    host->out_screen("  box <x> <y> <w> <h> <rgb>  - Draw rectangle border\n");
    host->out_screen("  text <x> <y> <fg> <bg> <msg> - Draw text\n");
    host->out_screen("  window <x> <y> <w> <h> <title> - Draw simple window\n");
    host->out_screen("  videostress [workers] [iterations] - Run multi-thread video stress test\n");
    host->out_screen("  help          - This help\n");
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

static int shell_builtin_cmd_videostress(const shell_builtin_host_t* host, const char* args) {
    if (!shell_builtin_host_valid(host)) {
        return 0;
    }

    unsigned int params[2] = {0, 0};
    const char* rest = 0;
    unsigned int workers = 0;
    unsigned int iterations = 0;

    if (args && args[0] != '\0') {
        if (!shell_builtin_parse_n_uints(args, params, 2, &rest) || (rest && rest[0] != '\0')) {
            host->out_both("Usage: videostress [workers] [iterations]\n");
            return 1;
        }
        workers = params[0];
        iterations = params[1];
    }

    if (workers == 0) {
        workers = 2;
    }
    if (iterations == 0) {
        iterations = 120;
    }

    char buf[16];
    int len;

    host->out_both("PTVIDEO100 start workers=");
    len = shell_builtin_uint_to_dec(workers, buf);
    buf[len] = '\0';
    host->out_both(buf);

    host->out_both(" iterations=");
    len = shell_builtin_uint_to_dec(iterations, buf);
    buf[len] = '\0';
    host->out_both(buf);

    int started = video_start_stress_workers((int)workers, (int)iterations);
    host->out_both(" started=");
    len = shell_builtin_uint_to_dec((unsigned int)started, buf);
    buf[len] = '\0';
    host->out_both(buf);
    host->out_both("\n");

    if (started < (int)workers) {
        host->out_both("PTVIDEO110 limited by scheduler slots\n");
    }

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
        host->out_both("MiniDOS Version 0.1 (MVP) - BIOS boot vol + ATA/FAT16 runtime\n");
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
    if (shell_builtin_mystrcmp(command, "videostress") == 0) {
        return shell_builtin_cmd_videostress(host, args);
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
