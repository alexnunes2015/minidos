#ifndef PROCESS_H
#define PROCESS_H

typedef enum {
    PROCESS_UNUSED = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_TERMINATED = 3
} process_state_t;

typedef struct {
    unsigned int* esp;
    unsigned int* irq_esp;
} process_context_t;

typedef struct {
    int pid;
    const char* name;
    process_state_t state;
    process_context_t context;
} process_t;

const char* process_state_name(process_state_t state);

#endif
