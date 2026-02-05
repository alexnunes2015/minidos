#ifndef SHELL_H
#define SHELL_H

// External memory size variable
extern unsigned int g_memory_kb;

void shell_init();
void shell_prompt();
void shell_execute(char* cmd);

#endif
