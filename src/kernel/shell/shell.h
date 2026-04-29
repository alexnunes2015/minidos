#ifndef SHELL_H
#define SHELL_H

// External memory size variable
extern unsigned int g_memory_kb;

void shell_init();
void shell_prompt();
void shell_execute(char* cmd);
int shell_run_app_from_drive_root(const char* app_name, int drive_letter);
void shell_autocomplete_reset(void);
int shell_autocomplete_apply(char* buffer, int* len, int max_len);

#endif
