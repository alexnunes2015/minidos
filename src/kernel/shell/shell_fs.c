#include "shell_fs.h"
#include "drive.h"
#include "fat16.h"

static int shell_fs_host_valid(const shell_fs_host_t* host) {
    return host
        && host->current_dir_cluster
        && host->current_path
        && host->current_path_size > 0
        && host->ensure_fat16_ready
        && host->out_screen
        && host->out_screen_char
        && host->out_both
        && host->out_both_char
        && host->str_to_upper
        && host->str_copy_upper
        && host->path_reset
        && host->path_pop
        && host->path_push
        && host->parse_two_args
        && host->uint_to_dec
        && host->has_wildcards;
}

static int shell_fs_mystrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void shell_fs_out_uint(const shell_fs_host_t* host, unsigned int value) {
    char n[12];
    int len = host->uint_to_dec(value, n);
    n[len] = '\0';
    host->out_both(n);
}

static int shell_fs_cmd_dir(const shell_fs_host_t* host, const char* args) {
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->out_both("Directory of ");
    host->out_both_char('A' + drive_get_current());
    host->out_both(":");
    host->out_both(host->current_path);
    if (args[0] != '\0') {
        char filter_upper[64];
        host->str_copy_upper(args, filter_upper, (int)sizeof(filter_upper));
        host->out_both(" [");
        host->out_both(filter_upper);
        host->out_both("]");
    }
    host->out_both("\n\n");

    if (*host->current_dir_cluster == 0) {
        if (args[0] == '\0') {
            fat16_list_root();
        } else {
            char filter_upper[64];
            host->str_copy_upper(args, filter_upper, (int)sizeof(filter_upper));
            fat16_list_root_filtered(filter_upper);
        }
    } else {
        if (args[0] == '\0') {
            fat16_list_dir(*host->current_dir_cluster);
        } else {
            char filter_upper[64];
            host->str_copy_upper(args, filter_upper, (int)sizeof(filter_upper));
            fat16_list_dir_filtered(*host->current_dir_cluster, filter_upper);
        }
    }
    return 1;
}

static int shell_fs_cmd_cd(const shell_fs_host_t* host, const char* args) {
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    if (args[0] == '\0') {
        host->out_screen_char('A' + drive_get_current());
        host->out_screen(":");
        host->out_screen(host->current_path);
        host->out_screen("\n");
        return 1;
    }

    if (args[0] == '\\' || args[0] == '/') {
        *host->current_dir_cluster = 0;
        host->path_reset(host->current_path);
        return 1;
    }
    if (args[0] == '.' && args[1] == '.' && args[2] == '\0') {
        unsigned int parent_cluster = 0;
        if (fat16_get_parent_cluster(*host->current_dir_cluster, &parent_cluster)) {
            *host->current_dir_cluster = parent_cluster;
            host->path_pop(host->current_path);
        } else {
            host->out_screen("Path not found\n");
        }
        return 1;
    }
    if (args[0] == '.' && args[1] == '\0') {
        return 1;
    }

    {
        char dir_upper[64];
        unsigned int next_cluster = 0;

        host->str_copy_upper(args, dir_upper, (int)sizeof(dir_upper));
        if (fat16_find_dir_cluster(*host->current_dir_cluster, dir_upper, &next_cluster)) {
            if (host->path_push(host->current_path, host->current_path_size, dir_upper)) {
                *host->current_dir_cluster = next_cluster;
            } else {
                host->out_screen("Path too long\n");
            }
        } else {
            host->out_screen("Path not found\n");
        }
    }
    return 1;
}

static int shell_fs_cmd_type(const shell_fs_host_t* host, const char* args) {
    static unsigned char file_buffer[8192];
    char file_upper[64];
    int bytes_read;

    if (args[0] == '\0') {
        host->out_screen("Usage: type <filename>\n");
        return 1;
    }
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->str_copy_upper(args, file_upper, (int)sizeof(file_upper));
    bytes_read = fat16_read_file_from_dir(*host->current_dir_cluster, file_upper, file_buffer, (int)sizeof(file_buffer));
    if (bytes_read > 0) {
        for (int j = 0; j < bytes_read; j++) {
            unsigned char c = file_buffer[j];
            if (c == '\n') {
                host->out_screen_char('\n');
            } else if (c >= 32 && c < 127) {
                host->out_screen_char((char)c);
            } else if (c == '\t') {
                host->out_screen_char(' ');
                host->out_screen_char(' ');
            } else if (c != '\r') {
                host->out_screen_char('.');
            }
        }
        host->out_screen_char('\n');
    } else {
        host->out_screen("File not found: ");
        host->out_screen(file_upper);
        host->out_screen_char('\n');
    }
    return 1;
}

static int shell_fs_cmd_mkdir(const shell_fs_host_t* host, const char* args) {
    char name_upper[64];

    if (args[0] == '\0') {
        host->out_both("Usage: mkdir <dirname>\n");
        return 1;
    }
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->str_copy_upper(args, name_upper, (int)sizeof(name_upper));
    if (fat16_mkdir(*host->current_dir_cluster, name_upper)) {
        host->out_both("Directory created\n");
    } else {
        host->out_both("Failed to create directory\n");
    }
    return 1;
}

static int shell_fs_cmd_rmdir(const shell_fs_host_t* host, const char* args) {
    char name_upper[64];

    if (args[0] == '\0') {
        host->out_both("Usage: rmdir <dirname>\n");
        return 1;
    }
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->str_copy_upper(args, name_upper, (int)sizeof(name_upper));
    if (fat16_rmdir(*host->current_dir_cluster, name_upper)) {
        host->out_both("Directory removed\n");
    } else {
        host->out_both("Failed to remove directory\n");
    }
    return 1;
}

static int shell_fs_cmd_del(const shell_fs_host_t* host, const char* args) {
    char pattern_upper[64];

    if (args[0] == '\0') {
        host->out_both("Usage: del <filename>\n");
        return 1;
    }
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->str_copy_upper(args, pattern_upper, (int)sizeof(pattern_upper));
    if (host->has_wildcards(pattern_upper)) {
        int deleted_count = 0;
        if (fat16_delete_matching(*host->current_dir_cluster, pattern_upper, 1, &deleted_count)) {
            if (deleted_count > 0) {
                host->out_both("Deleted ");
                shell_fs_out_uint(host, (unsigned int)deleted_count);
                host->out_both(" file(s)\n");
            } else {
                host->out_both("No files matched\n");
            }
        } else {
            host->out_both("Failed to delete files\n");
        }
    } else if (fat16_delete_entry(*host->current_dir_cluster, pattern_upper, 1)) {
        host->out_both("File deleted\n");
    } else {
        host->out_both("Failed to delete file\n");
    }
    return 1;
}

static int shell_fs_cmd_copy(const shell_fs_host_t* host, const char* args) {
    char src_name[64];
    char dst_name[64];

    if (!host->parse_two_args(args, src_name, (int)sizeof(src_name), dst_name, (int)sizeof(dst_name))) {
        host->out_both("Usage: copy <source> <destination>\n");
        return 1;
    }
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->str_to_upper(src_name);
    host->str_to_upper(dst_name);
    if (fat16_copy_file(*host->current_dir_cluster, src_name, dst_name)) {
        host->out_both("File copied\n");
    } else {
        host->out_both("Failed to copy file\n");
    }
    return 1;
}

static int shell_fs_cmd_move(const shell_fs_host_t* host, const char* args) {
    char src_name[64];
    char dst_name[64];
    FAT16_DirectoryEntry src_entry;

    if (!host->parse_two_args(args, src_name, (int)sizeof(src_name), dst_name, (int)sizeof(dst_name))) {
        host->out_both("Usage: move <source> <destination>\n");
        return 1;
    }
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->str_to_upper(src_name);
    host->str_to_upper(dst_name);
    if (!fat16_find_entry(*host->current_dir_cluster, src_name, &src_entry, 0, 0)) {
        host->out_both("File not found\n");
    } else if (src_entry.attributes & FAT16_ATTR_DIRECTORY) {
        host->out_both("Cannot move directory with this command\n");
    } else if (fat16_update_entry(*host->current_dir_cluster, src_name, dst_name, src_entry.cluster_low, src_entry.file_size, src_entry.attributes)) {
        host->out_both("File moved\n");
    } else {
        host->out_both("Failed to move file\n");
    }
    return 1;
}

static int shell_fs_cmd_elfls(const shell_fs_host_t* host) {
    if (!host->ensure_fat16_ready()) {
        host->out_both("No disk or FAT16 partition found on current drive\n");
        return 1;
    }

    host->out_both("ELF apps in ");
    host->out_both_char('A' + drive_get_current());
    host->out_both(":");
    host->out_both(host->current_path);
    host->out_both("\n\n");
    if (*host->current_dir_cluster == 0) {
        fat16_list_root_filtered("*.ELF");
    } else {
        fat16_list_dir_filtered(*host->current_dir_cluster, "*.ELF");
    }
    return 1;
}

int shell_fs_try_execute(const shell_fs_host_t* host, const char* command, const char* args) {
    if (!shell_fs_host_valid(host) || !command || !args) {
        return 0;
    }

    if (shell_fs_mystrcmp(command, "dir") == 0) {
        return shell_fs_cmd_dir(host, args);
    }
    if (shell_fs_mystrcmp(command, "cd") == 0) {
        return shell_fs_cmd_cd(host, args);
    }
    if (shell_fs_mystrcmp(command, "type") == 0) {
        return shell_fs_cmd_type(host, args);
    }
    if (shell_fs_mystrcmp(command, "mkdir") == 0) {
        return shell_fs_cmd_mkdir(host, args);
    }
    if (shell_fs_mystrcmp(command, "rmdir") == 0) {
        return shell_fs_cmd_rmdir(host, args);
    }
    if (shell_fs_mystrcmp(command, "del") == 0 || shell_fs_mystrcmp(command, "rm") == 0) {
        return shell_fs_cmd_del(host, args);
    }
    if (shell_fs_mystrcmp(command, "copy") == 0 || shell_fs_mystrcmp(command, "cp") == 0) {
        return shell_fs_cmd_copy(host, args);
    }
    if (shell_fs_mystrcmp(command, "move") == 0 || shell_fs_mystrcmp(command, "mv") == 0 || shell_fs_mystrcmp(command, "ren") == 0) {
        return shell_fs_cmd_move(host, args);
    }
    if (shell_fs_mystrcmp(command, "elfls") == 0) {
        return shell_fs_cmd_elfls(host);
    }

    return 0;
}
