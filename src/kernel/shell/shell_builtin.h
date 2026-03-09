#ifndef SHELL_BUILTIN_H
#define SHELL_BUILTIN_H

typedef struct {
    void (*out_screen)(const char* s);
    void (*out_both)(const char* s);
} shell_builtin_host_t;

void shell_builtin_show_boot_screen(void);
int shell_builtin_try_execute(const shell_builtin_host_t* host, const char* command, const char* args);

#endif
