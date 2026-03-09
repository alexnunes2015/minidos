#ifndef SHELL_FS_H
#define SHELL_FS_H

typedef struct {
    unsigned int* current_dir_cluster;
    char* current_path;
    int current_path_size;
    int (*ensure_fat16_ready)(void);
    void (*out_screen)(const char* s);
    void (*out_screen_char)(char c);
    void (*out_both)(const char* s);
    void (*out_both_char)(char c);
    void (*str_to_upper)(char* s);
    void (*str_copy_upper)(const char* src, char* dst, int dst_size);
    void (*path_reset)(char* path);
    void (*path_pop)(char* path);
    int (*path_push)(char* path, int max_len, const char* name);
    int (*parse_two_args)(const char* args, char* first, int first_size, char* second, int second_size);
    int (*uint_to_dec)(unsigned int value, char* out);
    int (*has_wildcards)(const char* s);
} shell_fs_host_t;

int shell_fs_try_execute(const shell_fs_host_t* host, const char* command, const char* args);

#endif
