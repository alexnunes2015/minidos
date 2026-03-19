#ifndef PROCESS_H
#define PROCESS_H

typedef enum {
    PROCESS_UNUSED = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_BLOCKED = 3,
    PROCESS_TERMINATED = 4
} process_state_t;

typedef struct {
    void* irq_frame;
    unsigned int* user_esp;
    unsigned int* kernel_stack_base;
    unsigned int* kernel_stack_top;
    unsigned int* user_stack_base;
    unsigned int* user_stack_top;
    unsigned int* guard_page_base;
    unsigned int* page_directory;
} process_context_t;

typedef struct {
    int pid;
    const char* name;
    const char* origin_name;
    int origin_is_executable;
    process_state_t state;
    process_context_t context;
    unsigned int wake_tick;
    unsigned int runtime_ticks;
    unsigned int switch_count;
    void (*thread_entry)(void* arg);
    void* thread_arg;
} process_t;

const char* process_state_name(process_state_t state);

#endif
