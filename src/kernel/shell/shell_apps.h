#ifndef SHELL_APPS_H
#define SHELL_APPS_H

typedef struct {
    unsigned int* current_dir_cluster;
    char* current_path;
    int current_path_size;
    int* fat16_initialized;
    unsigned int* app_clip_src_cluster;
    char* app_clip_name;
    int app_clip_name_size;
    int* app_clip_mode;
    void (*out_both)(const char* s);
    void (*str_to_upper)(char* s);
    void (*path_reset)(char* path);
    void (*path_pop)(char* path);
    int (*path_push)(char* path, int max_len, const char* name);
} shell_app_host_t;

int shell_apps_try_execute(const shell_app_host_t* host, const char* command, const char* args);
int shell_apps_try_execute_program(const shell_app_host_t* host, const char* command, const char* args);
int shell_apps_try_execute_elf(const shell_app_host_t* host, const char* command, const char* args);
int shell_apps_run_background(const shell_app_host_t* host, const char* command, int* pid_out);
int shell_apps_stop_background(int pid);
int shell_apps_handle_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2);
void shell_apps_on_current_task_exit(void);
void shell_apps_on_current_task_fault(unsigned int vector, unsigned int error_code, unsigned int eip);

#endif
