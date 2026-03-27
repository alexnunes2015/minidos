#define BOOT_AUTOEXEC_DEGRADED "BOOT300"
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
        log_serial_raw(BOOT_AUTOEXEC_DEGRADED "\n");
        log_write(
            LOG_LEVEL_WARN,
            "autoexec",
            "degraded: no AUTOEXEC storage\n",
            LOG_DEST_SERIAL
        );
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
