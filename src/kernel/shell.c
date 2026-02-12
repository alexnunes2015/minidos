#include "shell.h"
#include "video.h"
#include "fat16.h"
#include "drive.h"
#include "serial.h"
#include "logger.h"
#include "rtc.h"

static void delay(unsigned int count) {
    for (volatile unsigned int i = 0; i < count; i++) {
        for (volatile unsigned int j = 0; j < 20000; j++) {
            __asm__ volatile ("nop");
        }
    }
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
        delay(2);
    }
    print_string(" ] Done\n\n");

    delay(10);
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

static int has_wildcards(const char* s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '*' || s[i] == '?') {
            return 1;
        }
    }
    return 0;
}

void shell_init() {
    show_boot_screen();
    shell_out_both("MiniDOS Shell Ready.\nType 'help' for commands.\n");
    fat16_set_drive(drive_get_current());
}

static unsigned int current_dir_cluster = 0;
static char current_path[64] = "\\";
static int fat16_initialized = 0;

void shell_prompt() {
    int drive = drive_get_current();
    print_char('A' + drive);
    print_string(":");
    print_string(current_path);
    print_char('>');
}

void shell_execute(char* cmd) {
    // Trim whitespace
    int i = 0;
    while (cmd[i] != '\0') i++;
    while (i > 0 && (cmd[i-1] == ' ' || cmd[i-1] == '\t' || cmd[i-1] == '\n')) {
        cmd[--i] = '\0';
    }
    
    // Convert to lowercase for command comparison (but preserve drive letters)
    int is_drive_cmd = (cmd[0] >= 'A' && cmd[0] <= 'Z' && cmd[1] == ':' && cmd[2] == '\0');
    if (!is_drive_cmd) {
        for (int j = 0; cmd[j] && cmd[j] != ' '; j++) {
            if (cmd[j] >= 'A' && cmd[j] <= 'Z') {
                cmd[j] = cmd[j] - 'A' + 'a';
            }
        }
    }
    
    // Check if command is drive letter (e.g., "A:")
    if (cmd[0] >= 'A' && cmd[0] <= 'Z' && cmd[1] == ':' && cmd[2] == '\0') {
        int drive_letter = cmd[0] - 'A';
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
    
    if (mystrcmp(command, "help") == 0) {
        shell_out_both("Available commands:\n");
        shell_out_screen("  dir           - List files\n");
        shell_out_screen("  drives        - List all drives\n");
        shell_out_screen("  type <file>   - View file contents\n");
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
        shell_out_screen("  dmesg         - Show debug ring buffer\n");
        shell_out_screen("  boot          - Show boot screen\n");
        shell_out_screen("  help          - This help\n");
    } else if (mystrcmp(command, "ver") == 0) {
        shell_out_both("MiniDOS Version 0.1 (MVP) - FAT16\n");
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
    } else if (mystrcmp(command, "drives") == 0) {
        drive_list_all();
    } else if (mystrcmp(command, "dmesg") == 0) {
        log_dump_buffer(LOG_DEST_BOTH);
    } else if (mystrcmp(command, "dir") == 0) {
        if (!fat16_initialized) {
            fat16_set_drive(drive_get_current());
            fat16_init();
            fat16_initialized = 1;
        }
        shell_out_both("Directory of ");
        shell_out_both_char('A' + drive_get_current());
        shell_out_both(":");
        shell_out_both(current_path);
        if (args[0] != '\0') {
            shell_out_both(" [");
            str_to_upper(args);
            shell_out_both(args);
            shell_out_both("]");
        }
        shell_out_both("\n\n");
        if (current_dir_cluster == 0) {
            if (args[0] == '\0') {
                fat16_list_root();
            } else {
                fat16_list_root_filtered(args);
            }
        } else {
            if (args[0] == '\0') {
                fat16_list_dir(current_dir_cluster);
            } else {
                fat16_list_dir_filtered(current_dir_cluster, args);
            }
        }
    } else if (mystrcmp(command, "cd") == 0) {
        if (!fat16_initialized) {
            fat16_set_drive(drive_get_current());
            fat16_init();
            fat16_initialized = 1;
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
                for (int j = 0; args[j]; j++) {
                    if (args[j] >= 'a' && args[j] <= 'z') {
                        args[j] = args[j] - 'a' + 'A';
                    }
                }

                unsigned int next_cluster = 0;
                if (fat16_find_dir_cluster(current_dir_cluster, args, &next_cluster)) {
                    if (path_push(current_path, (int)sizeof(current_path), args)) {
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
            if (!fat16_initialized) {
                fat16_set_drive(drive_get_current());
                fat16_init();
                fat16_initialized = 1;
            }
            // Convert to uppercase for FAT16 (which is case-insensitive)
            str_to_upper(args);
            
            static unsigned char file_buffer[8192];
            int bytes_read = fat16_read_file_from_dir(current_dir_cluster, args, file_buffer, sizeof(file_buffer));
            
            if (bytes_read > 0) {
                print_string("--- ");
                print_string(args);
                print_string(" ---\n");
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
                print_string("\n--- End of file ---\n");
            } else {
                print_string("File not found: ");
                print_string(args);
                print_char('\n');
            }
        }
    } else if (mystrcmp(command, "mkdir") == 0) {
        if (args[0] == '\0') {
            shell_out_both("Usage: mkdir <dirname>\n");
        } else {
            if (!fat16_initialized) {
                fat16_set_drive(drive_get_current());
                fat16_init();
                fat16_initialized = 1;
            }

            str_to_upper(args);

            if (fat16_mkdir(current_dir_cluster, args)) {
                shell_out_both("Directory created\n");
            } else {
                shell_out_both("Failed to create directory\n");
            }
        }
    } else if (mystrcmp(command, "rmdir") == 0) {
        if (args[0] == '\0') {
            shell_out_both("Usage: rmdir <dirname>\n");
        } else {
            if (!fat16_initialized) {
                fat16_set_drive(drive_get_current());
                fat16_init();
                fat16_initialized = 1;
            }

            str_to_upper(args);

            if (fat16_rmdir(current_dir_cluster, args)) {
                shell_out_both("Directory removed\n");
            } else {
                shell_out_both("Failed to remove directory\n");
            }
        }
    } else if (mystrcmp(command, "del") == 0 || mystrcmp(command, "rm") == 0) {
        if (args[0] == '\0') {
            shell_out_both("Usage: del <filename>\n");
        } else {
            if (!fat16_initialized) {
                fat16_set_drive(drive_get_current());
                fat16_init();
                fat16_initialized = 1;
            }

            str_to_upper(args);

            if (has_wildcards(args)) {
                int deleted_count = 0;
                if (fat16_delete_matching(current_dir_cluster, args, 1, &deleted_count)) {
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
            } else if (fat16_delete_entry(current_dir_cluster, args, 1)) {
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
            if (!fat16_initialized) {
                fat16_set_drive(drive_get_current());
                fat16_init();
                fat16_initialized = 1;
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
            if (!fat16_initialized) {
                fat16_set_drive(drive_get_current());
                fat16_init();
                fat16_initialized = 1;
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
    } else if (cmd[0] != '\0') {
        print_string("Bad command or file name\n");
    }
}
