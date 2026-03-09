#ifndef SCHEDULER_H
#define SCHEDULER_H

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

void scheduler_init_timer(unsigned int hz);
void scheduler_enable_preemption(unsigned int quantum_ticks);
irq_frame_t* scheduler_on_timer_tick(irq_frame_t* frame);
int scheduler_phase5_self_test(void);
unsigned int scheduler_get_ticks(void);

#endif
