#include "shell.h"
#include "shell_apps.h"
#include "shell_builtin.h"
#include "shell_fs.h"
#include "video.h"
#include "fat16.h"
#include "drive.h"
#include "logger.h"

static unsigned int current_dir_cluster = 0;
static char current_path[64] = "\\";
static int fat16_initialized = 0;
static unsigned int app_clip_src_cluster = 0;
static char app_clip_name[64];
static int app_clip_mode = 0; /* 0 none, 1 copy, 2 move */

static int ensure_fat16_ready(void);

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

static void shell_out_screen_char(char c) {
    print_char(c);
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

static int has_wildcards(const char* s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '*' || s[i] == '?') {
            return 1;
        }
    }
    return 0;
}

static void shell_build_app_host(shell_app_host_t* host) {
    host->current_dir_cluster = &current_dir_cluster;
    host->current_path = current_path;
    host->current_path_size = (int)sizeof(current_path);
    host->fat16_initialized = &fat16_initialized;
    host->app_clip_src_cluster = &app_clip_src_cluster;
    host->app_clip_name = app_clip_name;
    host->app_clip_name_size = (int)sizeof(app_clip_name);
    host->app_clip_mode = &app_clip_mode;
    host->out_both = shell_out_both;
    host->str_to_upper = str_to_upper;
    host->path_reset = path_reset;
    host->path_pop = path_pop;
    host->path_push = path_push;
}

static void shell_build_builtin_host(shell_builtin_host_t* host) {
    host->out_screen = shell_out_screen;
    host->out_both = shell_out_both;
}

static void shell_build_fs_host(shell_fs_host_t* host) {
    host->current_dir_cluster = &current_dir_cluster;
    host->current_path = current_path;
    host->current_path_size = (int)sizeof(current_path);
    host->ensure_fat16_ready = ensure_fat16_ready;
    host->out_screen = shell_out_screen;
    host->out_screen_char = shell_out_screen_char;
    host->out_both = shell_out_both;
    host->out_both_char = shell_out_both_char;
    host->str_to_upper = str_to_upper;
    host->str_copy_upper = str_copy_upper;
    host->path_reset = path_reset;
    host->path_pop = path_pop;
    host->path_push = path_push;
    host->parse_two_args = parse_two_args;
    host->uint_to_dec = uint_to_dec;
    host->has_wildcards = has_wildcards;
}

static int shell_try_execute_builtin_command(const char* command, const char* args) {
    shell_builtin_host_t host;
    shell_build_builtin_host(&host);
    return shell_builtin_try_execute(&host, command, args);
}

static int shell_try_execute_fs_command(const char* command, const char* args) {
    shell_fs_host_t host;
    shell_build_fs_host(&host);
    return shell_fs_try_execute(&host, command, args);
}

void shell_init() {
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

    if (shell_try_execute_builtin_command(command, args)) {
        return;
    } else if (mystrcmp(command, "drives") == 0) {
        drive_list_all();
    } else if (shell_try_execute_fs_command(command, args)) {
        return;
    } else if (mystrcmp(command, "run") == 0) {
        char app_name[64];
        shell_app_host_t host;
        if (!parse_single_arg(args, app_name, sizeof(app_name))) {
            shell_out_both("Usage: run <app>\n");
            return;
        }
        str_to_upper(app_name);
        shell_build_app_host(&host);
        if (!shell_apps_try_execute_elf(&host, app_name, "")) {
            shell_out_both("ELF app not found or failed to load\n");
        }
    } else if (cmd[0] != '\0') {
        shell_app_host_t host;
        shell_build_app_host(&host);
        if (shell_apps_try_execute(&host, command, args)) {
            return;
        }
        print_string("Bad command or file name\n");
    }
}
