#include "boot_splash.h"
#include "video.h"
#include "keyboard.h"
#include "shell/shell.h"
#include "disk.h"
#include "drive.h"
#include "fat16.h"
#include "serial.h"
#include "logger.h"
#include "paging.h"
#include "scheduler.h"
#include "timer.h"

// Memory size from BIOS (in KB)
unsigned int g_memory_kb = 0;

#define STOP_MEMORY_INVALID "STOP 0x00000001"
#define STOP_PAGING_INIT    "STOP 0x00000002"
#define STOP_DISK_READ      "STOP 0x00000003"
#define STOP_DRIVE_DETECT   "STOP 0x00000004"
#define STOP_SCHED_INIT     "STOP 0x00000006"
#define STOP_SCHED_SELFTEST "STOP 0x00000007"
#define AUTO_SCRIPT_NAME    "AUTOEXEC.AUT"
#define AUTO_SCRIPT_MAX     2048
#define AUTO_LINE_MAX       64

#ifndef SHELL_COMMAND_TRACE
#define SHELL_COMMAND_TRACE 0
#endif

typedef enum {
    CMD_INPUT_NONE = 0,
    CMD_INPUT_SERIAL = 1,
    CMD_INPUT_KEYBOARD = 2
} command_input_t;

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned short read_phys_u16(unsigned int addr) {
    unsigned short val;
    __asm__ volatile ("movw (%1), %0" : "=r"(val) : "r"(addr) : "memory");
    return val;
}

static void bsod_wait_key_then_reboot() {
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

    outb(0x64, 0xFE);
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void boot_panic_bsod(const char* stop_code, const char* detail) {
    log_serial_raw("[boot] critical failure: ");
    if (detail) {
        log_serial_raw(detail);
    } else {
        log_serial_raw("unknown error");
    }
    log_serial_raw("\n");
    video_show_bsod(stop_code, detail);
    bsod_wait_key_then_reboot();
}

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int is_command_char(char c) {
    if (c == '\r' || c == '\n' || c == '\b' || c == '\t') {
        return 1;
    }

    return c >= 32 && c <= 126;
}

static char to_upper_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static int is_rem_line(const char* line) {
    char a = to_upper_char(line[0]);
    char b = to_upper_char(line[1]);
    char c = to_upper_char(line[2]);
    if (a == 'R' && b == 'E' && c == 'M') {
        char next = line[3];
        if (next == '\0' || is_space(next)) {
            return 1;
        }
    }
    return 0;
}

static command_input_t read_command_line(char* buffer, int max_len) {
    int i = 0;
    command_input_t source = CMD_INPUT_NONE;

    if (!buffer || max_len <= 0) {
        return CMD_INPUT_NONE;
    }

    while (i < max_len - 1) {
        char c = 0;

        if (source != CMD_INPUT_SERIAL && keyboard_try_get_char(&c)) {
            source = CMD_INPUT_KEYBOARD;
            video_set_deferred_present(1);
            if (c == '\n') {
                print_char('\n');
                video_cursor_reset_blink();
                video_set_deferred_present(0);
                break;
            }
            if (c == '\b') {
                if (i > 0) {
                    i--;
                    print_char('\b');
                    video_cursor_reset_blink();
                }
                video_set_deferred_present(0);
                continue;
            }

            buffer[i++] = c;
            print_char(c);
            video_cursor_reset_blink();
            video_set_deferred_present(0);
            continue;
        }

        if (source != CMD_INPUT_KEYBOARD && serial_received()) {
            c = serial_getchar();
            if (source == CMD_INPUT_NONE && !is_command_char(c)) {
                continue;
            }
            if (!is_command_char(c)) {
                continue;
            }

            source = CMD_INPUT_SERIAL;
            if (c == '\r' || c == '\n') {
                break;
            }
            if (c == '\b') {
                if (i > 0) {
                    i--;
                    serial_putchar('\b');
                }
                continue;
            }

            buffer[i++] = c;
            serial_putchar(c);
            continue;
        }

        video_cursor_blink_step();
        timer_wait_for_interrupt();
    }

    buffer[i] = '\0';
    return source;
}

static void run_auto_script() {
    static unsigned char script[AUTO_SCRIPT_MAX + 1];
    char line[AUTO_LINE_MAX];
    int bytes_read;
    int i;

    fat16_set_drive(drive_get_current());
    if (!fat16_init()) {
        log_write(LOG_LEVEL_DEBUG, "autoexec", "FAT16 not ready, skipping AUTOEXEC.AUT\n", LOG_DEST_SERIAL);
        return;
    }

    bytes_read = fat16_read_file(AUTO_SCRIPT_NAME, script, AUTO_SCRIPT_MAX);
    if (bytes_read <= 0) {
        log_write(LOG_LEVEL_DEBUG, "autoexec", "AUTOEXEC.AUT not found\n", LOG_DEST_SERIAL);
        return;
    }

    script[bytes_read] = '\0';
    log_write(LOG_LEVEL_INFO, "autoexec", "Running AUTOEXEC.AUT\n", LOG_DEST_SERIAL);

    i = 0;
    while (i < bytes_read) {
        int line_len = 0;
        int start;
        int end;

        while (i < bytes_read && script[i] != '\n') {
            if (script[i] != '\r' && line_len < (AUTO_LINE_MAX - 1)) {
                line[line_len++] = (char)script[i];
            }
            i++;
        }
        if (i < bytes_read && script[i] == '\n') {
            i++;
        }

        line[line_len] = '\0';

        start = 0;
        while (line[start] != '\0' && is_space(line[start])) {
            start++;
        }

        end = line_len;
        while (end > start && is_space(line[end - 1])) {
            end--;
        }
        line[end] = '\0';

        if (line[start] == '\0') {
            continue;
        }

        if (is_rem_line(&line[start])) {
            continue;
        }

        log_write(LOG_LEVEL_INFO, "autoexec", "Command: ", LOG_DEST_SERIAL);
        log_serial_raw(&line[start]);
        log_serial_raw("\n");
        shell_execute(&line[start]);
    }
}

static void kernel_runtime_thread(void* arg) {
    (void)arg;

    if (scheduler_phase5_self_test() != 0) {
        boot_panic_bsod(STOP_SCHED_SELFTEST, "PHASE5 RUNTIME SELF-TEST FAILED.");
    }
    scheduler_disable_preemption();

    log_write(LOG_LEVEL_INFO, "kernel", "Starting shell...\n", LOG_DEST_SERIAL);
    boot_splash_finish();
    shell_init();
    scheduler_set_current_name("shell");
    run_auto_script();

    keyboard_flush();
    scheduler_enable_preemption(5);
    log_write(LOG_LEVEL_INFO, "kernel", "Entering main loop\n", LOG_DEST_SERIAL);
    while (1) {
        char command[64];
        command_input_t input;

        shell_prompt();
        input = read_command_line(command, 64);

        if (input == CMD_INPUT_SERIAL) {
            log_write(LOG_LEVEL_DEBUG, "input", "reading command from serial\n", LOG_DEST_SERIAL);
            log_serial_raw("\n");
        } else {
            log_write(LOG_LEVEL_DEBUG, "input", "reading command from keyboard\n", LOG_DEST_SERIAL);
        }

        if (input == CMD_INPUT_SERIAL) {
            log_serial_raw("Command: ");
            log_serial_raw(command);
            log_serial_raw("\n");
        } else if (SHELL_COMMAND_TRACE) {
            log_write(LOG_LEVEL_DEBUG, "shell", "Command: ", LOG_DEST_SERIAL);
            log_serial_raw(command);
            log_serial_raw("\n");
        }

        shell_execute(command);
    }
}

void kernel_main() {
    unsigned int base_mem;
    unsigned int ext_mem;
    char mem_str[16];
    unsigned int mem;
    int idx = 0;

    // Initialize serial for debugging FIRST
    serial_init();
    log_init();
    log_serial_raw("\n\n=== MiniDOS Kernel Starting ===\n");

    // Read memory size from BIOS data (stored by bootloader)
    base_mem = read_phys_u16(0x500); // Base memory (up to 640KB)
    ext_mem = read_phys_u16(0x502);  // Extended memory (above 1MB)

    // Total memory = base + extended (extended is above 1MB, so add 1024KB)
    if (ext_mem > 0) {
        g_memory_kb = 1024 + ext_mem;  // Extended memory starts at 1MB
    } else {
        g_memory_kb = base_mem;         // Only base memory available
    }

    log_serial_raw("System Memory: ");
    mem = g_memory_kb;
    if (mem == 0) {
        mem_str[idx++] = '0';
    } else {
        int temp = (int)mem;
        int digits = 0;
        while (temp > 0) {
            temp /= 10;
            digits++;
        }
        idx = digits;
        temp = (int)mem;
        while (temp > 0) {
            mem_str[--digits] = '0' + (temp % 10);
            temp /= 10;
        }
    }
    mem_str[idx] = '\0';
    log_serial_raw(mem_str);
    log_serial_raw(" KB\n");

    if (g_memory_kb == 0) {
        boot_panic_bsod(STOP_MEMORY_INVALID, "INVALID MEMORY SIZE REPORTED BY BIOS.");
    }

    if (paging_init() != 0) {
        boot_panic_bsod(STOP_PAGING_INIT, "FAILED TO INITIALIZE PAGING.");
    }

    log_write(LOG_LEVEL_INFO, "kernel", "MiniDOS v0.1 Kernel Started\n", LOG_DEST_SERIAL);
    boot_splash_begin();
    boot_splash_pump();

    log_write(LOG_LEVEL_INFO, "kernel", "Initializing disk driver...\n", LOG_DEST_SERIAL);
    disk_init();
    {
        unsigned char probe[512];
        if (disk_read_lba(0, probe) != 0) {
            boot_panic_bsod(STOP_DISK_READ, "FAILED TO READ BOOT DISK (LBA 0).");
        }
    }
    log_write(LOG_LEVEL_INFO, "kernel", "Disk driver initialized\n", LOG_DEST_SERIAL);
    boot_splash_pump();

    drive_init_boot_media();
    boot_splash_try_load_logo();
    boot_splash_pump();

    log_write(LOG_LEVEL_INFO, "kernel", "Detecting drives and partitions...\n", LOG_DEST_SERIAL);
    drive_probe_additional();
    boot_splash_try_load_logo();
    boot_splash_pump();
    if (drive_get_count() <= 0) {
        boot_panic_bsod(STOP_DRIVE_DETECT, "NO VALID DRIVE DETECTED.");
    }
    log_write(LOG_LEVEL_INFO, "kernel", "Drive detection complete\n", LOG_DEST_SERIAL);

    interrupts_init();
    if (scheduler_runtime_init() != 0) {
        boot_panic_bsod(STOP_SCHED_INIT, "FAILED TO INITIALIZE SCHEDULER RUNTIME.");
    }

    kernel_runtime_thread(0);
}
