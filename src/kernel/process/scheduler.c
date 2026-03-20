#include "scheduler.h"
#include "process.h"
#include "paging.h"
#include "serial.h"
#include "logger.h"
#include "timer.h"

#define SCHED_BOOT_PID 0
#define SCHED_IDLE_PID 1
#define SCHED_STACK_PAGE_SIZE 4096U
#define SCHED_STACK_PAGES 16U
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
#define USER_CODE_SELECTOR   0x2BU
#define USER_DATA_SELECTOR   0x33U
#define SCHED_EXTERNAL_PID_MIN 3

typedef struct {
    process_t pcb;
    char name_storage[24];
    char origin_storage[24];
} sched_proc_slot_t;

static sched_proc_slot_t g_slots[SCHEDULER_MAX_PROCESSES];
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

static void sched_mark_terminated(process_t* pcb) {
    if (!pcb) {
        return;
    }

    pcb->state = PROCESS_TERMINATED;
    pcb->wake_tick = 0;
    pcb->thread_entry = 0;
    pcb->thread_arg = 0;
    pcb->context.user_esp = 0;
    pcb->context.page_directory = paging_get_kernel_directory();
}

static void sched_reset_slot(int idx, const char* name, process_state_t state) {
    process_t* pcb = &g_slots[idx].pcb;

    pcb->pid = idx;
    pcb->state = state;
    pcb->context.irq_frame = 0;
    pcb->context.user_esp = 0;
    pcb->context.user_stack_base = 0;
    pcb->context.user_stack_top = 0;
    pcb->context.page_directory = paging_get_kernel_directory();
    pcb->wake_tick = 0;
    pcb->runtime_ticks = 0;
    pcb->switch_count = 0;
    pcb->thread_entry = 0;
    pcb->thread_arg = 0;
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
    for (int idx = SCHED_IDLE_PID; idx < SCHEDULER_MAX_PROCESSES; idx++) {
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

static __attribute__((noreturn)) void sched_thread_bootstrap(void) {
    process_t* pcb;
    void (*entry)(void* arg);
    void* arg;

    if (g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
        sched_exit_current();
    }

    pcb = &g_slots[g_current].pcb;
    entry = pcb->thread_entry;
    arg = pcb->thread_arg;

    if (!entry) {
        sched_exit_current();
    }

    entry(arg);
    sched_exit_current();
}

static void sched_prepare_kernel_thread(sched_proc_slot_t* slot, void (*entry)(void* arg), void* arg) {
    unsigned char* stack_top = (unsigned char*)slot->pcb.context.kernel_stack_top;
    unsigned int* return_addr;
    irq_frame_t* frame;

    sched_mem_zero((unsigned char*)slot->pcb.context.kernel_stack_base, SCHED_STACK_BYTES);
    slot->pcb.thread_entry = entry;
    slot->pcb.thread_arg = arg;

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
    frame->eip = (unsigned int)sched_thread_bootstrap;
    frame->cs = KERNEL_CODE_SELECTOR;
    frame->eflags = EFLAGS_IF | 0x2U;

    slot->pcb.context.irq_frame = frame;
}

static int sched_find_free_slot_from(int first_pid) {
    if (first_pid < (SCHED_IDLE_PID + 1)) {
        first_pid = SCHED_IDLE_PID + 1;
    }

    for (int idx = first_pid; idx < SCHEDULER_MAX_PROCESSES; idx++) {
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

    for (int step = 1; step < SCHEDULER_MAX_PROCESSES; step++) {
        int idx = (g_current + step) % SCHEDULER_MAX_PROCESSES;
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

static irq_frame_t* sched_activate_slot(int next) {
    process_t* nxt = &g_slots[next].pcb;

    nxt->state = PROCESS_RUNNING;
    nxt->switch_count++;
    g_current = next;
    g_slice_ticks_left = g_quantum_ticks;
    paging_activate_directory(nxt->context.page_directory);
    paging_set_kernel_stack_top((unsigned int)nxt->context.kernel_stack_top);
    return (irq_frame_t*)nxt->context.irq_frame;
}

static irq_frame_t* sched_switch_from_frame(irq_frame_t* frame) {
    int next;
    process_t* cur;

    if (g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
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

    return sched_activate_slot(next);
}

static void sched_wake_sleepers(unsigned int now) {
    for (int idx = SCHED_BOOT_PID; idx < SCHEDULER_MAX_PROCESSES; idx++) {
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

static void sched_idle_thread(void* arg) {
    (void)arg;
    while (1) {
        timer_wait_for_interrupt();
    }
}

static int sched_spawn_kernel_thread_internal(
    const char* name,
    const char* origin_name,
    int origin_is_executable,
    void (*entry)(void* arg),
    void* arg,
    unsigned int* page_directory,
    int first_pid
) {
    unsigned int flags = sched_read_eflags();
    int idx;

    __asm__ volatile ("cli");
    idx = sched_find_free_slot_from(first_pid);

    if (idx < 0 || !name || !entry) {
        if ((flags & EFLAGS_IF) != 0U) {
            __asm__ volatile ("sti");
        }
        return -1;
    }

    /* Do not expose the slot as runnable until its IRQ-return frame is ready. */
    sched_reset_slot(idx, name, PROCESS_BLOCKED);
    if (page_directory) {
        g_slots[idx].pcb.context.page_directory = page_directory;
    }
    sched_set_slot_origin(idx, origin_name, origin_is_executable);
    sched_prepare_kernel_thread(&g_slots[idx], entry, arg);
    g_slots[idx].pcb.state = PROCESS_READY;
    if ((flags & EFLAGS_IF) != 0U) {
        __asm__ volatile ("sti");
    }
    return idx;
}

#ifdef SCHED_TEST_GUARD
static void sched_self_test_guard_task(void* arg) {
    volatile unsigned int* guard;
    (void)arg;

    sched_log_marker("SCHED150", " guard-trip-armed");
    guard = (volatile unsigned int*)g_slots[g_current].pcb.context.guard_page_base;
    *guard = 0x53434844U;

    while (1) {
        __asm__ volatile ("hlt");
    }
}
#else
static void sched_self_test_task_a(void* arg) {
    (void)arg;
    for (unsigned int i = 0; i < SCHED_TEST_ROUNDS; i++) {
        g_self_test_a_steps++;
        log_serial_raw("[sched] task A step\n");
        scheduler_sleep_ticks(1);
    }
}

static void sched_self_test_task_b(void* arg) {
    (void)arg;
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

    for (int idx = 0; idx < SCHEDULER_MAX_PROCESSES; idx++) {
        sched_reset_slot(idx, "unused", PROCESS_UNUSED);
    }

    sched_reset_slot(SCHED_BOOT_PID, "kernel", PROCESS_RUNNING);
    sched_reset_slot(SCHED_IDLE_PID, "idle", PROCESS_READY);

    if (sched_guard_pages_init() != 0) {
        return -1;
    }

    sched_prepare_kernel_thread(&g_slots[SCHED_IDLE_PID], sched_idle_thread, 0);

    g_current = SCHED_BOOT_PID;
    g_runtime_ready = 1;
    g_quantum_ticks = SCHED_DEFAULT_QUANTUM;
    g_slice_ticks_left = SCHED_DEFAULT_QUANTUM;
    paging_activate_directory(g_slots[g_current].pcb.context.page_directory);
    paging_set_kernel_stack_top((unsigned int)g_slots[g_current].pcb.context.kernel_stack_top);

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

void scheduler_disable_preemption(void) {
    g_preemption_enabled = 0;
    g_slice_ticks_left = g_quantum_ticks;
    log_serial_raw("[sched] preemption disabled\n");
}

irq_frame_t* scheduler_on_timer_tick(irq_frame_t* frame) {
    int forced_next = -1;

    timer_on_tick();

    if (!frame || !g_runtime_ready || g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
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
    if (!frame || !g_runtime_ready || g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
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

    if (!g_runtime_ready || g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
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
    if (g_current >= 0 && g_current < SCHEDULER_MAX_PROCESSES) {
        sched_mark_terminated(&g_slots[g_current].pcb);
    }

    scheduler_yield();

    log_serial_raw("[sched] unexpected return after task exit\n");
    while (1) {
        __asm__ volatile ("cli\nhlt");
    }
}

void scheduler_enter_current_user_mode(unsigned int entry_virtual, unsigned int user_esp) {
    unsigned int eflags;
    process_t* pcb;

    if (!g_runtime_ready
        || g_current < 0
        || g_current >= SCHEDULER_MAX_PROCESSES
        || entry_virtual == 0
        || user_esp == 0) {
        log_serial_raw("[sched] invalid user-mode handoff\n");
        while (1) {
            __asm__ volatile ("cli\nhlt");
        }
    }

    pcb = &g_slots[g_current].pcb;
    pcb->context.user_esp = (unsigned int*)user_esp;
    eflags = sched_read_eflags() | EFLAGS_IF | 0x2U;
    __asm__ volatile ("cli");
    paging_set_kernel_stack_top((unsigned int)pcb->context.kernel_stack_top);
    __asm__ volatile (
        "mov %w0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushl %0\n"
        "pushl %1\n"
        "pushl %2\n"
        "pushl %3\n"
        "pushl %4\n"
        "iret\n"
        :
        : "r"((unsigned int)USER_DATA_SELECTOR),
          "r"(user_esp),
          "r"(eflags),
          "r"((unsigned int)USER_CODE_SELECTOR),
          "r"(entry_virtual)
        : "memory", "eax");

    __builtin_unreachable();
}

irq_frame_t* scheduler_exit_current_from_interrupt(irq_frame_t* frame) {
    int next;
    process_t* cur;

    if (!frame || !g_runtime_ready || g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
        return 0;
    }

    cur = &g_slots[g_current].pcb;
    cur->context.irq_frame = frame;
    sched_mark_terminated(cur);

    next = sched_pick_next();
    if (next < 0) {
        return 0;
    }

    return sched_activate_slot(next);
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
    if (sched_spawn_kernel_thread_internal("guard_trip", "guard_trip", 0, sched_self_test_guard_task, 0, 0, SCHED_IDLE_PID + 1) < 0) {
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
    int task_a_pid = sched_spawn_kernel_thread_internal("task_a", "task_a", 0, sched_self_test_task_a, 0, 0, SCHED_IDLE_PID + 1);
    int task_b_pid = sched_spawn_kernel_thread_internal("task_b", "task_b", 0, sched_self_test_task_b, 0, 0, SCHED_IDLE_PID + 1);

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

    for (int idx = 0; idx < SCHEDULER_MAX_PROCESSES && count < max_out; idx++) {
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

    for (int idx = SCHED_IDLE_PID; idx < SCHEDULER_MAX_PROCESSES; idx++) {
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
    if (!g_runtime_ready || g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
        return;
    }

    sched_set_slot_name(g_current, name);
}

void scheduler_set_current_origin(const char* origin_name, int is_executable) {
    if (!g_runtime_ready || g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
        return;
    }

    sched_set_slot_origin(g_current, origin_name, is_executable);
}

int scheduler_get_current_pid(void) {
    if (!g_runtime_ready || g_current < 0 || g_current >= SCHEDULER_MAX_PROCESSES) {
        return -1;
    }

    return g_current;
}

process_state_t scheduler_get_process_state(int pid) {
    if (!g_runtime_ready || pid < 0 || pid >= SCHEDULER_MAX_PROCESSES) {
        return PROCESS_UNUSED;
    }

    return g_slots[pid].pcb.state;
}

int scheduler_spawn_kernel_task(
    const char* name,
    const char* origin_name,
    int origin_is_executable,
    void (*entry)(void* arg),
    void* arg,
    unsigned int* page_directory
) {
    if (!g_runtime_ready) {
        return -1;
    }

    return sched_spawn_kernel_thread_internal(
        name,
        origin_name,
        origin_is_executable,
        entry,
        arg,
        page_directory,
        SCHED_EXTERNAL_PID_MIN
    );
}

int scheduler_terminate_process(int pid) {
    if (!g_runtime_ready || pid <= SCHED_IDLE_PID || pid >= SCHEDULER_MAX_PROCESSES) {
        return 0;
    }
    if (g_slots[pid].pcb.state == PROCESS_UNUSED || g_slots[pid].pcb.state == PROCESS_TERMINATED) {
        return 0;
    }

    sched_mark_terminated(&g_slots[pid].pcb);
    return 1;
}

void scheduler_exit_current_task(void) {
    sched_exit_current();
}
