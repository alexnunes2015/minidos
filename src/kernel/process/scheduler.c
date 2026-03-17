#include "scheduler.h"
#include "process.h"
#include "paging.h"
#include "serial.h"
#include "logger.h"
#include "timer.h"

#define SCHED_MAX_PROCS 8
#define SCHED_BOOT_PID 0
#define SCHED_IDLE_PID 1
#define SCHED_STACK_PAGE_SIZE 4096U
#define SCHED_STACK_PAGES 2U
#define SCHED_STACK_BYTES (SCHED_STACK_PAGE_SIZE * SCHED_STACK_PAGES)
#define SCHED_GUARD_BYTES SCHED_STACK_PAGE_SIZE
#define SCHED_STACK_REGION_BYTES (SCHED_GUARD_BYTES + SCHED_STACK_BYTES)
#define SCHED_STACK_ARENA_BASE 0x00600000U
#define SCHED_DEFAULT_QUANTUM 5U
#define SCHED_SELF_TEST_QUANTUM 1U
#define SCHED_TEST_ROUNDS 3U
#define SCHED_SELF_TEST_TIMEOUT_TICKS 500U
#define EFLAGS_IF 0x00000200U
#define KERNEL_CODE_SELECTOR 0x08U
#define KERNEL_DATA_SELECTOR 0x10U

typedef struct {
    process_t pcb;
    char name_storage[24];
    char origin_storage[24];
} sched_proc_slot_t;

static sched_proc_slot_t g_slots[SCHED_MAX_PROCS];
static int g_current = -1;
static int g_runtime_ready = 0;
static int g_preemption_enabled = 0;
static unsigned int g_quantum_ticks = 0;
static unsigned int g_slice_ticks_left = 0;
static volatile unsigned int g_self_test_a_steps = 0;
static volatile unsigned int g_self_test_b_steps = 0;

static void sched_copy_text(const char* src, char* dst, unsigned int size) {
    unsigned int i = 0;

    if (!dst || size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1U < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void sched_set_slot_name(int idx, const char* name) {
    sched_proc_slot_t* slot = &g_slots[idx];

    sched_copy_text((name && name[0] != '\0') ? name : "unknown", slot->name_storage, sizeof(slot->name_storage));
    slot->pcb.name = slot->name_storage;
}

static void sched_set_slot_origin(int idx, const char* origin_name, int is_executable) {
    sched_proc_slot_t* slot = &g_slots[idx];

    if (!origin_name || origin_name[0] == '\0') {
        slot->origin_storage[0] = '\0';
        slot->pcb.origin_name = 0;
        slot->pcb.origin_is_executable = 0;
        return;
    }

    sched_copy_text(origin_name, slot->origin_storage, sizeof(slot->origin_storage));
    slot->pcb.origin_name = slot->origin_storage;
    slot->pcb.origin_is_executable = is_executable ? 1 : 0;
}

static void sched_mem_zero(unsigned char* dst, unsigned int size) {
    for (unsigned int i = 0; i < size; i++) {
        dst[i] = 0;
    }
}

static unsigned int sched_slot_guard_base(int idx) {
    return SCHED_STACK_ARENA_BASE + (unsigned int)(idx - 1) * SCHED_STACK_REGION_BYTES;
}

static unsigned int sched_read_eflags(void) {
    unsigned int flags;
    __asm__ volatile ("pushf\npop %0" : "=r"(flags));
    return flags;
}

static void sched_log_marker(const char* marker, const char* detail) {
    log_serial_raw(marker);
    if (detail) {
        log_serial_raw(detail);
    }
    log_serial_raw("\n");
}

static void sched_reset_slot(int idx, const char* name, process_state_t state) {
    process_t* pcb = &g_slots[idx].pcb;

    pcb->pid = idx;
    pcb->state = state;
    pcb->context.irq_frame = 0;
    pcb->context.user_esp = 0;
    pcb->context.user_stack_base = 0;
    pcb->context.user_stack_top = 0;
    pcb->wake_tick = 0;
    pcb->runtime_ticks = 0;
    pcb->switch_count = 0;
    sched_set_slot_name(idx, name);
    sched_set_slot_origin(idx, 0, 0);

    if (idx == SCHED_BOOT_PID) {
        pcb->context.kernel_stack_base = 0;
        pcb->context.kernel_stack_top = 0;
        pcb->context.guard_page_base = 0;
        return;
    }

    pcb->context.guard_page_base = (unsigned int*)sched_slot_guard_base(idx);
    pcb->context.kernel_stack_base = (unsigned int*)(sched_slot_guard_base(idx) + SCHED_GUARD_BYTES);
    pcb->context.kernel_stack_top = (unsigned int*)(sched_slot_guard_base(idx) + SCHED_STACK_REGION_BYTES);
}

static int sched_guard_pages_init(void) {
    for (int idx = SCHED_IDLE_PID; idx < SCHED_MAX_PROCS; idx++) {
        process_t* pcb = &g_slots[idx].pcb;
        unsigned int guard_addr = (unsigned int)pcb->context.guard_page_base;
        unsigned int stack_addr = (unsigned int)pcb->context.kernel_stack_base;

        if (!guard_addr || !stack_addr) {
            return -1;
        }
        if (!paging_unmap_page(guard_addr)) {
            return -1;
        }
        if (paging_page_present(guard_addr)) {
            return -1;
        }
        if (!paging_page_present(stack_addr) || !paging_page_present(stack_addr + SCHED_STACK_PAGE_SIZE)) {
            return -1;
        }
    }

    return 0;
}

static __attribute__((noreturn)) void sched_exit_current(void);

static __attribute__((noreturn)) void sched_thread_exit_trampoline(void) {
    sched_exit_current();
}

static void sched_prepare_kernel_thread(sched_proc_slot_t* slot, void (*entry)(void)) {
    unsigned char* stack_top = (unsigned char*)slot->pcb.context.kernel_stack_top;
    unsigned int* return_addr;
    irq_frame_t* frame;

    sched_mem_zero((unsigned char*)slot->pcb.context.kernel_stack_base, SCHED_STACK_BYTES);

    return_addr = (unsigned int*)(stack_top - sizeof(unsigned int));
    *return_addr = (unsigned int)sched_thread_exit_trampoline;

    frame = (irq_frame_t*)(stack_top - sizeof(unsigned int) - sizeof(irq_frame_t));
    frame->gs = KERNEL_DATA_SELECTOR;
    frame->fs = KERNEL_DATA_SELECTOR;
    frame->es = KERNEL_DATA_SELECTOR;
    frame->ds = KERNEL_DATA_SELECTOR;
    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->esp = 0;
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->vector = 0;
    frame->error_code = 0;
    frame->eip = (unsigned int)entry;
    frame->cs = KERNEL_CODE_SELECTOR;
    frame->eflags = EFLAGS_IF | 0x2U;

    slot->pcb.context.irq_frame = frame;
}

static int sched_find_free_slot(void) {
    for (int idx = SCHED_IDLE_PID + 1; idx < SCHED_MAX_PROCS; idx++) {
        process_state_t state = g_slots[idx].pcb.state;
        if (state == PROCESS_UNUSED || state == PROCESS_TERMINATED) {
            return idx;
        }
    }

    return -1;
}

static int sched_find_ready_non_idle(void) {
    if (g_current < 0) {
        return -1;
    }

    for (int step = 1; step < SCHED_MAX_PROCS; step++) {
        int idx = (g_current + step) % SCHED_MAX_PROCS;
        if (idx == SCHED_IDLE_PID) {
            continue;
        }
        if (g_slots[idx].pcb.state == PROCESS_READY) {
            return idx;
        }
    }

    return -1;
}

static int sched_pick_next(void) {
    int next = sched_find_ready_non_idle();

    if (next >= 0) {
        return next;
    }

    if (g_current >= 0 && g_slots[g_current].pcb.state == PROCESS_RUNNING) {
        return g_current;
    }

    if (g_slots[SCHED_IDLE_PID].pcb.state == PROCESS_READY ||
        g_slots[SCHED_IDLE_PID].pcb.state == PROCESS_RUNNING) {
        return SCHED_IDLE_PID;
    }

    return -1;
}

static irq_frame_t* sched_switch_from_frame(irq_frame_t* frame) {
    int next;
    process_t* cur;
    process_t* nxt;

    if (g_current < 0 || g_current >= SCHED_MAX_PROCS) {
        return 0;
    }

    cur = &g_slots[g_current].pcb;
    cur->context.irq_frame = frame;

    next = sched_pick_next();
    if (next < 0 || next == g_current) {
        if (cur->state == PROCESS_READY) {
            cur->state = PROCESS_RUNNING;
        }
        return 0;
    }

    if (cur->state == PROCESS_RUNNING) {
        cur->state = PROCESS_READY;
    }

    nxt = &g_slots[next].pcb;
    nxt->state = PROCESS_RUNNING;
    nxt->switch_count++;
    g_current = next;
    g_slice_ticks_left = g_quantum_ticks;
    return (irq_frame_t*)nxt->context.irq_frame;
}

static void sched_wake_sleepers(unsigned int now) {
    for (int idx = SCHED_BOOT_PID; idx < SCHED_MAX_PROCS; idx++) {
        process_t* pcb = &g_slots[idx].pcb;

        if (pcb->state != PROCESS_BLOCKED) {
            continue;
        }
        if ((unsigned int)(now - pcb->wake_tick) >= 0x80000000U) {
            continue;
        }

        pcb->wake_tick = 0;
        pcb->state = PROCESS_READY;
    }
}

static __attribute__((noreturn)) void sched_idle_thread(void) {
    while (1) {
        timer_wait_for_interrupt();
    }
}

static int sched_spawn_kernel_thread(const char* name, void (*entry)(void)) {
    int idx = sched_find_free_slot();

    if (idx < 0 || !name || !entry) {
        return -1;
    }

    sched_reset_slot(idx, name, PROCESS_READY);
    sched_prepare_kernel_thread(&g_slots[idx], entry);
    return idx;
}

#ifdef SCHED_TEST_GUARD
static void sched_self_test_guard_task(void) {
    volatile unsigned int* guard;

    sched_log_marker("SCHED150", " guard-trip-armed");
    guard = (volatile unsigned int*)g_slots[g_current].pcb.context.guard_page_base;
    *guard = 0x53434844U;

    while (1) {
        __asm__ volatile ("hlt");
    }
}
#else
static void sched_self_test_task_a(void) {
    for (unsigned int i = 0; i < SCHED_TEST_ROUNDS; i++) {
        g_self_test_a_steps++;
        log_serial_raw("[sched] task A step\n");
        scheduler_sleep_ticks(1);
    }
}

static void sched_self_test_task_b(void) {
    for (unsigned int i = 0; i < SCHED_TEST_ROUNDS; i++) {
        g_self_test_b_steps++;
        log_serial_raw("[sched] task B step\n");
        scheduler_sleep_ticks(1);
    }
}
#endif

void scheduler_init_timer(unsigned int hz) {
    timer_init(hz);
}

int scheduler_runtime_init(void) {
    if (g_runtime_ready) {
        return 0;
    }

    for (int idx = 0; idx < SCHED_MAX_PROCS; idx++) {
        sched_reset_slot(idx, "unused", PROCESS_UNUSED);
    }

    sched_reset_slot(SCHED_BOOT_PID, "kernel", PROCESS_RUNNING);
    sched_reset_slot(SCHED_IDLE_PID, "idle", PROCESS_READY);

    if (sched_guard_pages_init() != 0) {
        return -1;
    }

    sched_prepare_kernel_thread(&g_slots[SCHED_IDLE_PID], sched_idle_thread);

    g_current = SCHED_BOOT_PID;
    g_runtime_ready = 1;
    g_quantum_ticks = SCHED_DEFAULT_QUANTUM;
    g_slice_ticks_left = SCHED_DEFAULT_QUANTUM;

    sched_log_marker("SCHED100", " runtime-init");
    sched_log_marker("SCHED110", " guarded-kstacks-armed");
    log_serial_raw("[sched] runtime initialized with guarded kernel stacks\n");
    return 0;
}

void scheduler_enable_preemption(unsigned int quantum_ticks) {
    if (quantum_ticks == 0) {
        quantum_ticks = SCHED_DEFAULT_QUANTUM;
    }

    g_quantum_ticks = quantum_ticks;
    g_slice_ticks_left = quantum_ticks;
    g_preemption_enabled = 1;
    log_serial_raw("[sched] preemption enabled\n");
}

irq_frame_t* scheduler_on_timer_tick(irq_frame_t* frame) {
    int forced_next = -1;

    timer_on_tick();

    if (!frame || !g_runtime_ready || g_current < 0 || g_current >= SCHED_MAX_PROCS) {
        return 0;
    }

    g_slots[g_current].pcb.runtime_ticks++;
    sched_wake_sleepers(timer_get_ticks());

    forced_next = (g_current == SCHED_IDLE_PID) ? sched_find_ready_non_idle() : -1;
    if (!g_preemption_enabled || g_quantum_ticks == 0) {
        if (forced_next >= 0) {
            return sched_switch_from_frame(frame);
        }
        return 0;
    }

    if (forced_next < 0) {
        if (g_slice_ticks_left > 0) {
            g_slice_ticks_left--;
        }
        if (g_slice_ticks_left > 0) {
            return 0;
        }
    }

    g_slice_ticks_left = g_quantum_ticks;
    return sched_switch_from_frame(frame);
}

irq_frame_t* scheduler_on_yield(irq_frame_t* frame) {
    if (!frame || !g_runtime_ready || g_current < 0 || g_current >= SCHED_MAX_PROCS) {
        return 0;
    }

    return sched_switch_from_frame(frame);
}

void scheduler_yield(void) {
    if (!g_runtime_ready) {
        return;
    }

    __asm__ volatile ("int $0x81" : : : "memory");
}

void scheduler_sleep_ticks(unsigned int ticks) {
    process_t* pcb;

    if (!g_runtime_ready || g_current < 0 || g_current >= SCHED_MAX_PROCS) {
        return;
    }

    if (ticks == 0) {
        scheduler_yield();
        return;
    }

    pcb = &g_slots[g_current].pcb;
    pcb->wake_tick = timer_get_ticks() + ticks;
    pcb->state = PROCESS_BLOCKED;
    scheduler_yield();
}

static __attribute__((noreturn)) void sched_exit_current(void) {
    if (g_current >= 0 && g_current < SCHED_MAX_PROCS) {
        g_slots[g_current].pcb.state = PROCESS_TERMINATED;
    }

    scheduler_yield();

    log_serial_raw("[sched] unexpected return after task exit\n");
    while (1) {
        __asm__ volatile ("cli\nhlt");
    }
}

int scheduler_phase5_self_test(void) {
    unsigned int start_ticks;

    if (scheduler_runtime_init() != 0) {
        return -1;
    }

    g_self_test_a_steps = 0;
    g_self_test_b_steps = 0;
    sched_log_marker("SCHED120", " phase5-self-test-start");
    log_serial_raw("[sched] phase5 context-switch self-test start\n");
    scheduler_enable_preemption(SCHED_SELF_TEST_QUANTUM);

#ifdef SCHED_TEST_GUARD
    if (sched_spawn_kernel_thread("guard_trip", sched_self_test_guard_task) < 0) {
        return -1;
    }

    start_ticks = scheduler_get_ticks();
    while ((unsigned int)(scheduler_get_ticks() - start_ticks) < SCHED_SELF_TEST_TIMEOUT_TICKS) {
        if ((sched_read_eflags() & EFLAGS_IF) != 0U) {
            timer_wait_for_interrupt();
        }
    }
    return -1;
#else
    int task_a_pid = sched_spawn_kernel_thread("task_a", sched_self_test_task_a);
    int task_b_pid = sched_spawn_kernel_thread("task_b", sched_self_test_task_b);

    if (task_a_pid < 0 || task_b_pid < 0) {
        return -1;
    }

    start_ticks = scheduler_get_ticks();
    while ((unsigned int)(scheduler_get_ticks() - start_ticks) < SCHED_SELF_TEST_TIMEOUT_TICKS) {
        if (g_self_test_a_steps >= SCHED_TEST_ROUNDS &&
            g_self_test_b_steps >= SCHED_TEST_ROUNDS &&
            g_slots[task_a_pid].pcb.state == PROCESS_TERMINATED &&
            g_slots[task_b_pid].pcb.state == PROCESS_TERMINATED) {
            sched_log_marker("SCHED190", " phase5-self-test-ok");
            log_serial_raw("[sched] phase5 context-switch self-test OK\n");
            return 0;
        }
        if ((sched_read_eflags() & EFLAGS_IF) != 0U) {
            timer_wait_for_interrupt();
        }
    }

    log_serial_raw("[sched] phase5 self-test failed\n");
    return -1;
#endif
}

unsigned int scheduler_get_ticks(void) {
    return timer_get_ticks();
}

unsigned int scheduler_get_quantum_ticks(void) {
    return g_quantum_ticks;
}

unsigned int scheduler_get_kernel_stack_bytes(void) {
    return SCHED_STACK_BYTES;
}

unsigned int scheduler_get_guard_page_bytes(void) {
    return SCHED_GUARD_BYTES;
}

int scheduler_snapshot_processes(scheduler_process_snapshot_t* out, int max_out, int include_terminated) {
    int count = 0;

    if (!out || max_out <= 0 || !g_runtime_ready) {
        return 0;
    }

    for (int idx = 0; idx < SCHED_MAX_PROCS && count < max_out; idx++) {
        process_t* pcb = &g_slots[idx].pcb;
        scheduler_process_snapshot_t* snap;

        if (pcb->state == PROCESS_UNUSED) {
            continue;
        }
        if (!include_terminated && pcb->state == PROCESS_TERMINATED) {
            continue;
        }

        snap = &out[count++];
        snap->pid = pcb->pid;
        snap->name = pcb->name;
        snap->origin_name = pcb->origin_name;
        snap->origin_is_executable = pcb->origin_is_executable;
        snap->state = pcb->state;
        snap->is_current = (idx == g_current);
        snap->wake_tick = pcb->wake_tick;
        snap->runtime_ticks = pcb->runtime_ticks;
        snap->switch_count = pcb->switch_count;
        snap->kernel_stack_base = (unsigned int)pcb->context.kernel_stack_base;
        snap->kernel_stack_top = (unsigned int)pcb->context.kernel_stack_top;
        snap->guard_page_base = (unsigned int)pcb->context.guard_page_base;
    }

    return count;
}

int scheduler_describe_guard_fault(unsigned int fault_addr, int* pid_out, const char** name_out) {
    unsigned int page = fault_addr & ~(SCHED_STACK_PAGE_SIZE - 1U);

    for (int idx = SCHED_IDLE_PID; idx < SCHED_MAX_PROCS; idx++) {
        process_t* pcb = &g_slots[idx].pcb;
        if ((unsigned int)pcb->context.guard_page_base != page) {
            continue;
        }

        if (pid_out) {
            *pid_out = pcb->pid;
        }
        if (name_out) {
            *name_out = pcb->name;
        }
        return 1;
    }

    return 0;
}

void scheduler_set_current_name(const char* name) {
    if (!g_runtime_ready || g_current < 0 || g_current >= SCHED_MAX_PROCS) {
        return;
    }

    sched_set_slot_name(g_current, name);
}

void scheduler_set_current_origin(const char* origin_name, int is_executable) {
    if (!g_runtime_ready || g_current < 0 || g_current >= SCHED_MAX_PROCS) {
        return;
    }

    sched_set_slot_origin(g_current, origin_name, is_executable);
}
