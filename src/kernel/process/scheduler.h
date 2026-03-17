#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

typedef struct {
    unsigned int gs;
    unsigned int fs;
    unsigned int es;
    unsigned int ds;
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int esp;
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int vector;
    unsigned int error_code;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
} irq_frame_t;

typedef struct {
    int pid;
    const char* name;
    const char* origin_name;
    int origin_is_executable;
    process_state_t state;
    int is_current;
    unsigned int wake_tick;
    unsigned int runtime_ticks;
    unsigned int switch_count;
    unsigned int kernel_stack_base;
    unsigned int kernel_stack_top;
    unsigned int guard_page_base;
} scheduler_process_snapshot_t;

void scheduler_init_timer(unsigned int hz);
int scheduler_runtime_init(void);
void scheduler_enable_preemption(unsigned int quantum_ticks);
irq_frame_t* scheduler_on_timer_tick(irq_frame_t* frame);
irq_frame_t* scheduler_on_yield(irq_frame_t* frame);
void scheduler_yield(void);
void scheduler_sleep_ticks(unsigned int ticks);
int scheduler_phase5_self_test(void);
unsigned int scheduler_get_ticks(void);
unsigned int scheduler_get_quantum_ticks(void);
unsigned int scheduler_get_kernel_stack_bytes(void);
unsigned int scheduler_get_guard_page_bytes(void);
int scheduler_snapshot_processes(scheduler_process_snapshot_t* out, int max_out, int include_terminated);
int scheduler_describe_guard_fault(unsigned int fault_addr, int* pid_out, const char** name_out);
void scheduler_set_current_name(const char* name);
void scheduler_set_current_origin(const char* origin_name, int is_executable);

#endif
