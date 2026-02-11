#include "shell.h"
#include "video.h"
#include "fat16.h"
#include "drive.h"
#include "serial.h"

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

void shell_init() {
    show_boot_screen();
    print_string("MiniDOS Shell Ready.\nType 'help' for commands.\n");
    serial_print("MiniDOS Shell Ready.\nType 'help' for commands.\n");
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
            serial_print("Switched to drive ");
            serial_putchar('A' + drive_letter);
            serial_print(":\n");
            print_string("Switched to drive ");
            print_char('A' + drive_letter);
            print_string(":\n");
        } else {
            serial_print("Invalid drive\n");
            print_string("Invalid drive\n");
        }
        return;
    }
    
    // Parse command and arguments
    char command[64], args[64];
    parse_command(cmd, command, args);
    
    if (mystrcmp(command, "help") == 0) {
        serial_print("Available commands:\n");
        print_string("Available commands:\n");
        print_string("  dir           - List files\n");
        print_string("  drives        - List all drives\n");
        print_string("  type <file>   - View file contents\n");
        print_string("  cls           - Clear screen\n");
        print_string("  ver           - Show version\n");
        print_string("  mem           - Show system memory\n");
        print_string("  cd <dir>      - Change directory\n");
        print_string("  mkdir <dir>   - Create directory\n");
        print_string("  boot          - Show boot screen\n");
        print_string("  help          - This help\n");
    } else if (mystrcmp(command, "ver") == 0) {
        serial_print("MiniDOS Version 0.1 (MVP) - FAT16\n");
        print_string("MiniDOS Version 0.1 (MVP) - FAT16\n");
    } else if (mystrcmp(command, "mem") == 0) {
        serial_print("System Memory: ");
        print_string("System Memory: ");
        char mem_str[24];
        unsigned int mem = g_memory_kb;
        format_memory(mem, mem_str);
        serial_print(mem_str);
        serial_print("\n");
        print_string(mem_str);
        print_string("\n");
    } else if (mystrcmp(command, "cls") == 0) {
        cls();
    } else if (mystrcmp(command, "boot") == 0) {
        show_boot_screen();
    } else if (mystrcmp(command, "drives") == 0) {
        drive_list_all();
    } else if (mystrcmp(command, "dir") == 0) {
        if (!fat16_initialized) {
            fat16_set_drive(drive_get_current());
            fat16_init();
            fat16_initialized = 1;
        }
        serial_print("Directory of ");
        print_string("Directory of ");
        print_char('A' + drive_get_current());
        print_string(":");
        print_string(current_path);
        print_string("\n\n");
        serial_putchar('A' + drive_get_current());
        serial_print(":");
        serial_print(current_path);
        serial_print("\n\n");
        if (current_dir_cluster == 0) {
            fat16_list_root();
        } else {
            fat16_list_dir(current_dir_cluster);
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
            for (int j = 0; args[j]; j++) {
                if (args[j] >= 'a' && args[j] <= 'z') {
                    args[j] = args[j] - 'a' + 'A';
                }
            }
            
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
            print_string("Usage: mkdir <dirname>\n");
        } else {
            if (!fat16_initialized) {
                fat16_set_drive(drive_get_current());
                fat16_init();
                fat16_initialized = 1;
            }

            for (int j = 0; args[j]; j++) {
                if (args[j] >= 'a' && args[j] <= 'z') {
                    args[j] = args[j] - 'a' + 'A';
                }
            }

            if (fat16_mkdir(current_dir_cluster, args)) {
                print_string("Directory created\n");
            } else {
                print_string("Failed to create directory\n");
            }
        }
    } else if (cmd[0] != '\0') {
        print_string("Bad command or file name\n");
    }
}
