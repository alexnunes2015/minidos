#include "shell.h"
#include "video.h"
#include "fat16.h"
#include "drive.h"

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
}

void shell_prompt() {
    // Print current drive letter
    int drive = drive_get_current();
    print_char('A' + drive);
    print_string(":> ");
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
    
    // Check if command is drive letter (e.g., "C:")
    if (cmd[0] >= 'A' && cmd[0] <= 'Z' && cmd[1] == ':' && cmd[2] == '\0') {
        int drive_letter = cmd[0] - 'A';
        if (drive_get_info(drive_letter)) {
            drive_set_current(drive_letter);
            fat16_set_drive(drive_letter);
            print_string("Switched to drive ");
            print_char('A' + drive_letter);
            print_string(":\n");
        } else {
            print_string("Invalid drive\n");
        }
        return;
    }
    
    // Parse command and arguments
    char command[64], args[64];
    parse_command(cmd, command, args);
    
    if (mystrcmp(command, "help") == 0) {
        print_string("Available commands:\n");
        print_string("  dir           - List files\n");
        print_string("  drives        - List all drives\n");
        print_string("  type <file>   - View file contents\n");
        print_string("  cls           - Clear screen\n");
        print_string("  ver           - Show version\n");
        print_string("  mem           - Show system memory\n");
        print_string("  boot          - Show boot screen\n");
        print_string("  help          - This help\n");
    } else if (mystrcmp(command, "ver") == 0) {
        print_string("MiniDOS Version 0.1 (MVP) - FAT16\n");
    } else if (mystrcmp(command, "mem") == 0) {
        print_string("System Memory: ");
        // Convert to string manually
        char mem_str[16];
        unsigned int mem = g_memory_kb;
        int i = 0;
        if (mem == 0) {
            mem_str[i++] = '0';
        } else {
            // Convert number to string
            int temp = mem;
            int digits = 0;
            while (temp > 0) {
                temp /= 10;
                digits++;
            }
            i = digits;
            temp = mem;
            while (temp > 0) {
                mem_str[--digits] = '0' + (temp % 10);
                temp /= 10;
            }
        }
        mem_str[i] = '\0';
        print_string(mem_str);
        print_string(" KB\n");
    } else if (mystrcmp(command, "cls") == 0) {
        cls();
    } else if (mystrcmp(command, "boot") == 0) {
        show_boot_screen();
    } else if (mystrcmp(command, "drives") == 0) {
        drive_list_all();
    } else if (mystrcmp(command, "dir") == 0) {
        static int fat16_initialized = 0;
        if (!fat16_initialized) {
            fat16_init();
            fat16_initialized = 1;
        }
        fat16_list_root();
    } else if (mystrcmp(command, "type") == 0) {
        if (args[0] == '\0') {
            print_string("Usage: type <filename>\n");
        } else {
            // Convert to uppercase for FAT16 (which is case-insensitive)
            for (int j = 0; args[j]; j++) {
                if (args[j] >= 'a' && args[j] <= 'z') {
                    args[j] = args[j] - 'a' + 'A';
                }
            }
            
            static unsigned char file_buffer[8192];
            int bytes_read = fat16_read_file(args, file_buffer, sizeof(file_buffer));
            
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
    } else if (cmd[0] != '\0') {
        print_string("Bad command or file name\n");
    }
}
