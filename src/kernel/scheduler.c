#include "scheduler.h"
#include "process.h"
#include "serial.h"
#include "logger.h"

#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL0_PORT 0x40
#define PIT_BASE_HZ 1193182U
#define SCHED_MAX_PROCS 3
#define SCHED_STACK_WORDS 512
#define SCHED_TEST_ROUNDS 3

static inline void outb(unsigned short port, unsigned char value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

typedef struct {
    process_t pcb;
    unsigned int kernel_stack[SCHED_STACK_WORDS];
} sched_proc_slot_t;

static sched_proc_slot_t g_slots[SCHED_MAX_PROCS];
static int g_current = -1;
static unsigned int g_ticks = 0;
static int g_test_completed = 0;
static int g_runtime_ready = 0;
static int g_preemption_enabled = 0;
static unsigned int g_quantum_ticks = 0;
static unsigned int g_slice_ticks_left = 0;

__attribute__((naked)) static void sched_context_switch(unsigned int** old_esp, unsigned int* new_esp) {
    (void)old_esp;
    (void)new_esp;
    __asm__ volatile (
        "mov 4(%esp), %eax\n"
        "mov %esp, (%eax)\n"
        "mov 8(%esp), %esp\n"
        "ret\n"
    );
}

static int sched_find_next_ready(void) {
    if (g_current < 0) {
        return -1;
    }

    for (int step = 1; step < SCHED_MAX_PROCS; step++) {
        int idx = (g_current + step) % SCHED_MAX_PROCS;
        if (g_slots[idx].pcb.state == PROCESS_READY) {
            return idx;
        }
    }

    return -1;
}

static void sched_runtime_reset(void) {
    for (int i = 0; i < SCHED_MAX_PROCS; i++) {
        g_slots[i].pcb.pid = i;
        g_slots[i].pcb.name = "idle";
        g_slots[i].pcb.state = PROCESS_UNUSED;
        g_slots[i].pcb.context.esp = 0;
        g_slots[i].pcb.context.irq_esp = 0;
    }

    g_slots[0].pcb.name = "kernel";
    g_slots[0].pcb.state = PROCESS_RUNNING;
    g_current = 0;
    g_runtime_ready = 1;
}

static void sched_yield(void) {
    int next = sched_find_next_ready();
    if (next < 0) {
        return;
    }

    sched_proc_slot_t* cur = &g_slots[g_current];
    sched_proc_slot_t* nxt = &g_slots[next];

    if (cur->pcb.state == PROCESS_RUNNING) {
        cur->pcb.state = PROCESS_READY;
    }
    nxt->pcb.state = PROCESS_RUNNING;
    g_current = next;

    sched_context_switch(&cur->pcb.context.esp, nxt->pcb.context.esp);
}

static void sched_task_exit(void) {
    sched_proc_slot_t* cur = &g_slots[g_current];
    cur->pcb.state = PROCESS_TERMINATED;
    sched_yield();

    log_serial_raw("[sched] unexpected return after task_exit\n");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static unsigned int* sched_prepare_stack(sched_proc_slot_t* slot, void (*entry)(void)) {
    unsigned int* sp = &slot->kernel_stack[SCHED_STACK_WORDS];

    *--sp = (unsigned int)sched_task_exit;
    *--sp = (unsigned int)entry;

    return sp;
}

static void sched_task_a(void) {
    for (int i = 0; i < SCHED_TEST_ROUNDS; i++) {
        log_serial_raw("[sched] task A step\n");
        sched_yield();
    }
}

static void sched_task_b(void) {
    for (int i = 0; i < SCHED_TEST_ROUNDS; i++) {
        log_serial_raw("[sched] task B step\n");
        sched_yield();
    }
}

void scheduler_init_timer(unsigned int hz) {
    if (hz == 0) {
        hz = 100;
    }

    unsigned int divisor = PIT_BASE_HZ / hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFFU) {
        divisor = 0xFFFFU;
    }

    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, (unsigned char)(divisor & 0xFF));
    outb(PIT_CHANNEL0_PORT, (unsigned char)((divisor >> 8) & 0xFF));

    log_serial_raw("[sched] PIT timer configured\n");
}

void scheduler_enable_preemption(unsigned int quantum_ticks) {
    if (quantum_ticks == 0) {
        quantum_ticks = 5;
    }

    g_quantum_ticks = quantum_ticks;
    g_slice_ticks_left = quantum_ticks;
    g_preemption_enabled = 1;
    log_serial_raw("[sched] preemption enabled\n");
}

irq_frame_t* scheduler_on_timer_tick(irq_frame_t* frame) {
    g_ticks++;

    if (!frame || !g_runtime_ready || g_current < 0 || g_current >= SCHED_MAX_PROCS) {
        return 0;
    }

    g_slots[g_current].pcb.context.irq_esp = (unsigned int*)frame;

    if (!g_preemption_enabled || g_quantum_ticks == 0) {
        return 0;
    }

    if (g_slice_ticks_left > 0) {
        g_slice_ticks_left--;
    }
    if (g_slice_ticks_left > 0) {
        return 0;
    }
    g_slice_ticks_left = g_quantum_ticks;

    int next = sched_find_next_ready();
    if (next < 0) {
        return 0;
    }

    sched_proc_slot_t* cur = &g_slots[g_current];
    sched_proc_slot_t* nxt = &g_slots[next];
    if (!nxt->pcb.context.irq_esp) {
        return 0;
    }

    if (cur->pcb.state == PROCESS_RUNNING) {
        cur->pcb.state = PROCESS_READY;
    }
    nxt->pcb.state = PROCESS_RUNNING;
    g_current = next;

    return (irq_frame_t*)nxt->pcb.context.irq_esp;
}

int scheduler_phase5_self_test(void) {
    g_test_completed = 0;

    for (int i = 0; i < SCHED_MAX_PROCS; i++) {
        g_slots[i].pcb.pid = i;
        g_slots[i].pcb.name = "idle";
        g_slots[i].pcb.state = PROCESS_UNUSED;
        g_slots[i].pcb.context.esp = 0;
        g_slots[i].pcb.context.irq_esp = 0;
    }

    g_slots[0].pcb.name = "kernel";
    g_slots[0].pcb.state = PROCESS_RUNNING;
    g_slots[1].pcb.name = "task_a";
    g_slots[1].pcb.state = PROCESS_READY;
    g_slots[2].pcb.name = "task_b";
    g_slots[2].pcb.state = PROCESS_READY;

    g_slots[1].pcb.context.esp = sched_prepare_stack(&g_slots[1], sched_task_a);
    g_slots[2].pcb.context.esp = sched_prepare_stack(&g_slots[2], sched_task_b);

    g_current = 0;

    log_serial_raw("[sched] phase5 context-switch self-test start\n");

    for (int i = 0; i < (SCHED_TEST_ROUNDS * 4); i++) {
        sched_yield();
        if (g_slots[1].pcb.state == PROCESS_TERMINATED && g_slots[2].pcb.state == PROCESS_TERMINATED) {
            g_test_completed = 1;
            break;
        }
    }

    if (!g_test_completed) {
        log_serial_raw("[sched] phase5 self-test failed\n");
        return -1;
    }

    log_serial_raw("[sched] phase5 context-switch self-test OK\n");
    sched_runtime_reset();
    return 0;
}

unsigned int scheduler_get_ticks(void) {
    return g_ticks;
}
