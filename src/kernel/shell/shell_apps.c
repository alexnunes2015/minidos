#include "shell_apps.h"
#include "shell.h"
#include "drive.h"
#include "fat16.h"
#include "keyboard.h"
#include "logger.h"
#include "mouse.h"
#include "paging.h"
#include "scheduler.h"
#include "serial.h"
#include "rtc.h"
#include "timer.h"
#include "video.h"
#include "../../../external_apps/runtime/minidos_app.h"

typedef struct {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    unsigned int entry;
    unsigned int phoff;
    unsigned int shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} __attribute__((packed)) elf32_header_t;

typedef struct {
    unsigned int type;
    unsigned int offset;
    unsigned int vaddr;
    unsigned int paddr;
    unsigned int filesz;
    unsigned int memsz;
    unsigned int flags;
    unsigned int align;
} __attribute__((packed)) elf32_program_header_t;

#define APP_LOAD_VIRT_BASE 0x01000000U
#define APP_SLOT_FIRST_PID 3
#define APP_SLOT_SIZE 0x00100000U
#define APP_LOAD_VIRT_LIMIT (APP_LOAD_VIRT_BASE + APP_SLOT_SIZE)
#define APP_BACKGROUND_SLOT_COUNT 4
#define APP_THREAD_SLOT_COUNT (SCHEDULER_MAX_PROCESSES - APP_SLOT_FIRST_PID)
#define APP_THREAD_STACK_BYTES 0x00004000U
#define APP_RUNTIME_BLOCK_VIRT (APP_LOAD_VIRT_LIMIT - 0x00001000U)
#define APP_USER_IMAGE_LIMIT (APP_RUNTIME_BLOCK_VIRT - (APP_THREAD_STACK_BYTES * APP_THREAD_SLOT_COUNT))
#define APP_SYSCALL_STUB_VIRT (APP_RUNTIME_BLOCK_VIRT + 0x00000020U)
#define APP_EXIT_STUB_VIRT    (APP_RUNTIME_BLOCK_VIRT + 0x00000040U)
#define APP_THREAD_STUB_VIRT  (APP_RUNTIME_BLOCK_VIRT + 0x00000060U)
#define APP_MAX_ELF_SIZE 262144
#define APP_MAX_COM_SIZE 65536
#define APP_PATH_MAX 64
#define APP_CLIP_NAME_MAX 64
#define APP_USER_TEXT_MAX 256
#define APP_ELF_BUFFER_PHYS 0x00300000U
#define APP_TASK_TABLE_PHYS 0x00340000U

typedef enum {
    APP_FORMAT_NONE = 0,
    APP_FORMAT_ELF = 1,
    APP_FORMAT_COM = 2,
} shell_app_format_t;

typedef struct {
    int in_use;
    int pid;
    int leader_pid;
    int drive;
    int fat16_initialized;
    int app_clip_mode;
    int background;
    int serial_only;
    int join_on_exit;
    int input_ready_logged;
    unsigned int current_dir_cluster;
    unsigned int app_clip_src_cluster;
    unsigned int app_phys_base;
    unsigned int entry_virtual;
    unsigned int thread_arg;
    char current_path[APP_PATH_MAX];
    char app_clip_name[APP_CLIP_NAME_MAX];
    char task_name[24];
    char origin_name[24];
    void (*out_both)(const char* s);
    void (*str_to_upper)(char* s);
    void (*path_reset)(char* path);
    void (*path_pop)(char* path);
    int (*path_push)(char* path, int max_len, const char* name);
    unsigned int page_directory[1024] __attribute__((aligned(4096)));
    unsigned int user_pt[1024] __attribute__((aligned(4096)));
} shell_app_task_t;

static shell_app_task_t* const g_app_tasks = (shell_app_task_t*)APP_TASK_TABLE_PHYS;
static unsigned char* const g_elf_buffer = (unsigned char*)APP_ELF_BUFFER_PHYS;
static const unsigned int g_app_phys_slots[APP_BACKGROUND_SLOT_COUNT] = {
    0x00700000U,
    0x00800000U,
    0x00900000U,
    0x00A00000U,
};
static const unsigned int EFLAGS_IF = 0x00000200U;
static unsigned int app_rng_state = 0xA5F21C3Du;
static const unsigned char g_app_syscall_stub[] = {
    0x53,
    0x8B, 0x44, 0x24, 0x08,
    0x8B, 0x5C, 0x24, 0x0C,
    0x8B, 0x4C, 0x24, 0x10,
    0x8B, 0x54, 0x24, 0x14,
    0xCD, 0x80,
    0x5B,
    0xC3
};
static const unsigned char g_app_exit_stub[] = {
    0x89, 0xC3,
    0xB8, MINIDOS_SYSCALL_EXIT, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xEB, 0xFE
};
static const unsigned char g_app_thread_stub[] = {
    0x8B, 0x44, 0x24, 0x04,
    0x8B, 0x4C, 0x24, 0x08,
    0x8B, 0x54, 0x24, 0x0C,
    0x52,
    0x51,
    0xFF, 0xD0,
    0x83, 0xC4, 0x08,
    0xC3
};

static void shell_apps_background_thread(void* arg);
static void shell_apps_mem_copy(unsigned char* dst, const unsigned char* src, unsigned int size);
static int shell_apps_group_leader_pid(const shell_app_task_t* task);
static void shell_apps_release_task(shell_app_task_t* task);

static void shell_apps_flush_graphics(void) {
    video_present_pending();
}

static void shell_apps_log_task_marker(const char* marker, const shell_app_task_t* task) {
    if (!marker || !task) {
        return;
    }

    log_serial_raw(marker);
    log_serial_raw(" pid=");
    serial_print_hex((unsigned int)task->pid);
    log_serial_raw(" leader=");
    serial_print_hex((unsigned int)shell_apps_group_leader_pid(task));
    log_serial_raw(" entry=");
    serial_print_hex(task->entry_virtual);
    log_serial_raw(" base=");
    serial_print_hex(task->app_phys_base);
    log_serial_raw("\n");
}

static unsigned int shell_apps_user_stack_top_for_pid(int pid) {
    unsigned int index;

    if (pid < APP_SLOT_FIRST_PID || pid >= SCHEDULER_MAX_PROCESSES) {
        return 0;
    }

    index = (unsigned int)(pid - APP_SLOT_FIRST_PID);
    return APP_RUNTIME_BLOCK_VIRT - (index * APP_THREAD_STACK_BYTES);
}

static unsigned int shell_apps_phys_addr_for_virtual(const shell_app_task_t* task, unsigned int virt) {
    if (!task || task->app_phys_base == 0U) {
        return 0;
    }
    if (virt < APP_LOAD_VIRT_BASE || virt >= APP_LOAD_VIRT_LIMIT) {
        return 0;
    }

    return task->app_phys_base + (virt - APP_LOAD_VIRT_BASE);
}

static int shell_apps_user_range_valid(const shell_app_task_t* task, unsigned int addr, unsigned int size) {
    unsigned int end;

    if (!task || !task->in_use) {
        return 0;
    }
    if (size == 0U) {
        return 1;
    }
    if (addr < APP_LOAD_VIRT_BASE || addr >= APP_LOAD_VIRT_LIMIT) {
        return 0;
    }

    end = addr + size;
    if (end < addr || end > APP_LOAD_VIRT_LIMIT) {
        return 0;
    }
    return 1;
}

static int shell_apps_copy_user_bytes(const shell_app_task_t* task, const void* input, void* out, unsigned int size) {
    if (!out) {
        return 0;
    }
    if (size == 0U) {
        return 1;
    }
    if (!shell_apps_user_range_valid(task, (unsigned int)input, size)) {
        return 0;
    }

    shell_apps_mem_copy((unsigned char*)out, (const unsigned char*)input, size);
    return 1;
}

static int shell_apps_copy_user_string(const shell_app_task_t* task, const char* input, char* out, int out_size) {
    unsigned int addr;
    int i = 0;

    if (!task || !input || !out || out_size <= 0) {
        return 0;
    }

    addr = (unsigned int)input;
    if (addr < APP_LOAD_VIRT_BASE || addr >= APP_LOAD_VIRT_LIMIT) {
        return 0;
    }

    while (i < out_size - 1 && addr < APP_LOAD_VIRT_LIMIT) {
        char c = *(const char*)addr;

        out[i++] = c;
        addr++;
        if (c == '\0') {
            return 1;
        }
    }

    out[i < out_size ? i : (out_size - 1)] = '\0';
    return 0;
}

static int shell_apps_copy_user_upper(const shell_app_task_t* task, const char* input, char* out, int out_size) {
    if (!shell_apps_copy_user_string(task, input, out, out_size)) {
        return 0;
    }
    task->str_to_upper(out);
    return 1;
}

static int shell_apps_user_exec_ptr_valid(unsigned int addr) {
    return addr >= APP_LOAD_VIRT_BASE && addr < APP_USER_IMAGE_LIMIT;
}

static void shell_apps_mem_zero(unsigned char* dst, unsigned int size) {
    for (unsigned int i = 0; i < size; i++) {
        dst[i] = 0;
    }
}

static int shell_apps_host_valid(const shell_app_host_t* host) {
    return host
        && host->current_dir_cluster
        && host->current_path
        && host->current_path_size > 0
        && host->fat16_initialized
        && host->app_clip_src_cluster
        && host->app_clip_name
        && host->app_clip_name_size > 0
        && host->app_clip_mode
        && host->out_both
        && host->str_to_upper
        && host->path_reset
        && host->path_pop
        && host->path_push;
}

static void shell_apps_out_both(const shell_app_host_t* host, const char* text) {
    if (host && host->out_both && text) {
        host->out_both(text);
    }
}

static shell_app_task_t* shell_apps_find_task(int pid) {
    if (pid < 0 || pid >= SCHEDULER_MAX_PROCESSES) {
        return 0;
    }
    if (!g_app_tasks[pid].in_use) {
        return 0;
    }
    return &g_app_tasks[pid];
}

static int shell_apps_group_leader_pid(const shell_app_task_t* task) {
    if (!task) {
        return -1;
    }
    if (task->leader_pid >= APP_SLOT_FIRST_PID) {
        return task->leader_pid;
    }
    return task->pid;
}

static void shell_apps_reap_terminated_tasks(void) {
    for (int pid = APP_SLOT_FIRST_PID; pid < SCHEDULER_MAX_PROCESSES; pid++) {
        shell_app_task_t* task = &g_app_tasks[pid];
        process_state_t state;

        if (!task->in_use) {
            continue;
        }

        state = scheduler_get_process_state(pid);
        if (state == PROCESS_UNUSED || state == PROCESS_TERMINATED) {
            if (task->join_on_exit) {
                continue;
            }
            shell_apps_release_task(task);
        }
    }
}

static shell_app_task_t* shell_apps_active_task(void) {
    int pid = scheduler_get_current_pid();
    return shell_apps_find_task(pid);
}

static int shell_apps_task_valid(const shell_app_task_t* task) {
    return task
        && task->str_to_upper
        && task->path_reset
        && task->path_pop
        && task->path_push;
}

static void shell_apps_task_out(const shell_app_task_t* task, const char* text) {
    if (!task || !text) {
        return;
    }
    if (task->serial_only || !task->out_both) {
        log_serial_raw(text);
        return;
    }
    task->out_both(text);
}

static inline unsigned int read_eflags(void) {
    unsigned int flags;
    __asm__ volatile ("pushf\npop %0" : "=r"(flags));
    return flags;
}

static int shell_apps_copy_string(const char* input, char* out, int out_size) {
    int i = 0;

    if (!input || !out || out_size <= 0) {
        return 0;
    }

    while (input[i] != '\0' && i < out_size - 1) {
        out[i] = input[i];
        i++;
    }
    out[i] = '\0';
    return input[i] == '\0';
}

static char shell_apps_ascii_tolower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int shell_apps_has_extension(const char* name, const char* ext) {
    int name_len = 0;
    int ext_len = 0;

    while (name && name[name_len] != '\0') {
        name_len++;
    }
    while (ext && ext[ext_len] != '\0') {
        ext_len++;
    }

    if (ext_len == 0) {
        return 1;
    }
    if (name_len < ext_len) {
        return 0;
    }

    for (int i = 0; i < ext_len; i++) {
        if (shell_apps_ascii_tolower(name[name_len - ext_len + i]) != shell_apps_ascii_tolower(ext[i])) {
            return 0;
        }
    }
    return 1;
}

static int shell_apps_append_extension(const char* command, const char* ext, int only_if_missing, char* out, int out_size) {
    int i = 0;
    int ext_len = 0;

    if (!out || out_size <= 0) {
        return 0;
    }

    while (command[i] != '\0') {
        if (i >= out_size - 1) {
            return 0;
        }
        out[i] = command[i];
        i++;
    }
    out[i] = '\0';

    if (only_if_missing && shell_apps_has_extension(out, ext)) {
        return 1;
    }

    while (ext[ext_len] != '\0') {
        ext_len++;
    }
    if (i + ext_len >= out_size) {
        return 0;
    }

    for (int j = 0; j < ext_len; j++) {
        out[i++] = ext[j];
    }
    out[i] = '\0';
    return 1;
}

static void shell_apps_normalize_app_name(const char* input, char* out, int out_size) {
    int j = 0;

    for (int i = 0; input[i] != '\0' && j < out_size - 1; i++) {
        char c = input[i];
        if (c == '.') {
            break;
        }
        if (c == '_' || c == ' ' || c == '\t') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[j++] = c;
        if (j >= 8) {
            break;
        }
    }
    out[j] = '\0';
}

static int shell_apps_stage_elf_candidate(
    const shell_app_host_t* host,
    const char* command,
    char* task_name_out,
    int task_name_size,
    char* filename_out,
    int filename_size,
    int* bytes_read_out
) {
    char normalized[64];

    if (!shell_apps_host_valid(host) || !command || !filename_out || !bytes_read_out) {
        return 0;
    }

    shell_apps_normalize_app_name(command, normalized, (int)sizeof(normalized));
    if (normalized[0] == '\0') {
        return 0;
    }
    if (!shell_apps_append_extension(normalized, ".elf", 0, filename_out, filename_size)) {
        return 0;
    }

    host->str_to_upper(filename_out);
    *bytes_read_out = fat16_read_file_from_dir(*host->current_dir_cluster, filename_out, g_elf_buffer, APP_MAX_ELF_SIZE);
    if (*bytes_read_out <= 0) {
        return 0;
    }

    if (task_name_out && task_name_size > 0) {
        (void)shell_apps_copy_string(normalized, task_name_out, task_name_size);
    }
    return 1;
}

static int shell_apps_stage_com_candidate(
    const shell_app_host_t* host,
    const char* command,
    char* task_name_out,
    int task_name_size,
    char* filename_out,
    int filename_size,
    int* bytes_read_out
) {
    char normalized[64];

    if (!shell_apps_host_valid(host) || !command || !filename_out || !bytes_read_out) {
        return 0;
    }
    if (!shell_apps_append_extension(command, ".com", 1, filename_out, filename_size)) {
        return 0;
    }

    host->str_to_upper(filename_out);
    *bytes_read_out = fat16_read_file_from_dir(*host->current_dir_cluster, filename_out, g_elf_buffer, APP_MAX_COM_SIZE);
    if (*bytes_read_out <= 0) {
        return 0;
    }

    if (task_name_out && task_name_size > 0) {
        shell_apps_normalize_app_name(command, normalized, (int)sizeof(normalized));
        (void)shell_apps_copy_string(normalized, task_name_out, task_name_size);
    }
    return 1;
}

static void shell_apps_begin_input_session(shell_app_task_t* task) {
    if (task) {
        task->input_ready_logged = 0;
    }
}

static void shell_apps_note_input_ready(shell_app_task_t* task) {
    if (task && !task->input_ready_logged) {
        log_serial_raw("APPIN001\n");
        task->input_ready_logged = 1;
    }
}

static void shell_apps_note_session_return(shell_app_task_t* task) {
    if (task) {
        log_serial_raw("APPRET001\n");
        task->input_ready_logged = 0;
    }
}

static void shell_apps_begin_scheduler_origin(const char* origin_name) {
    scheduler_set_current_origin(origin_name, 1);
}

static void shell_apps_ensure_interrupts_enabled(void) {
    if ((read_eflags() & EFLAGS_IF) == 0U) {
        __asm__ volatile ("sti");
    }
}

static int shell_apps_try_get_char(char* out) {
    shell_app_task_t* task = shell_apps_active_task();

    if (!out || !shell_apps_task_valid(task) || task->background) {
        return 0;
    }

    shell_apps_ensure_interrupts_enabled();
    if (!task->background) {
        shell_apps_flush_graphics();
    }
    shell_apps_note_input_ready(task);

    if (keyboard_try_get_char(out)) {
        return 1;
    }

    if (serial_received()) {
        *out = serial_getchar();
        return 1;
    }

    return 0;
}

static char shell_apps_get_char(void) {
    char c = 0;

    while (!shell_apps_try_get_char(&c)) {
        timer_wait_for_interrupt();
    }

    return c;
}

static int shell_apps_get_mouse_state(app_mouse_state_t* out) {
    mouse_state_t state;
    shell_app_task_t* task = shell_apps_active_task();

    if (!out || !shell_apps_task_valid(task) || task->background) {
        return 0;
    }

    shell_apps_ensure_interrupts_enabled();
    if (!task->background) {
        shell_apps_flush_graphics();
    }
    shell_apps_note_input_ready(task);

    if (!mouse_get_state(&state)) {
        return 0;
    }

    out->x = state.x;
    out->y = state.y;
    out->dx = state.dx;
    out->dy = state.dy;
    out->buttons = state.buttons;
    out->seq = state.seq;
    out->present = state.present;
    return 1;
}

static int shell_apps_wait_event(unsigned int last_mouse_seq, unsigned int timeout_ms) {
    mouse_state_t state;
    shell_app_task_t* task = shell_apps_active_task();
    unsigned int start_ticks = scheduler_get_ticks();
    unsigned int timeout_ticks = timeout_ms ? timer_ms_to_ticks_ceil(timeout_ms) : 0;

    if (!shell_apps_task_valid(task)) {
        return 0;
    }

    shell_apps_ensure_interrupts_enabled();
    if (!task->background) {
        shell_apps_flush_graphics();
    }
    if (!task->background) {
        shell_apps_note_input_ready(task);
    }

    while (1) {
        int flags = 0;

        if (!task->background && (keyboard_has_input() || serial_received())) {
            flags |= APP_EVENT_KEY;
        }

        if (!task->background && mouse_get_state(&state) && state.present && state.seq != last_mouse_seq) {
            flags |= APP_EVENT_MOUSE;
        }

        if (timeout_ticks > 0 && (unsigned int)(scheduler_get_ticks() - start_ticks) >= timeout_ticks) {
            flags |= APP_EVENT_TIMER;
        }

        if (flags != 0) {
            return flags;
        }

        timer_wait_for_interrupt();
    }
}

static unsigned int shell_apps_random_u32(void) {
    unsigned int ticks = scheduler_get_ticks();
    unsigned int mix = ticks ^ (ticks << 16);

    app_rng_state ^= mix;
    app_rng_state ^= (app_rng_state << 13);
    app_rng_state ^= (app_rng_state >> 17);
    app_rng_state ^= (app_rng_state << 5);
    if (app_rng_state == 0) {
        app_rng_state = 0x6D2B79F5u ^ mix;
    }
    return app_rng_state;
}

static int shell_apps_ensure_host_fat16_ready(const shell_app_host_t* host) {
    if (!host || !host->fat16_initialized) {
        return 0;
    }
    if (!*host->fat16_initialized) {
        fat16_set_drive(drive_get_current());
        *host->fat16_initialized = fat16_init();
    }
    return *host->fat16_initialized;
}

static int shell_apps_find_free_pid(void) {
    shell_apps_reap_terminated_tasks();

    for (int pid = APP_SLOT_FIRST_PID; pid < SCHEDULER_MAX_PROCESSES; pid++) {
        if (!g_app_tasks[pid].in_use) {
            return pid;
        }
    }

    return -1;
}

static int shell_apps_phys_base_in_use(unsigned int phys_base) {
    shell_apps_reap_terminated_tasks();

    for (int pid = APP_SLOT_FIRST_PID; pid < SCHEDULER_MAX_PROCESSES; pid++) {
        if (g_app_tasks[pid].in_use && g_app_tasks[pid].app_phys_base == phys_base) {
            return 1;
        }
    }

    return 0;
}

static unsigned int shell_apps_allocate_phys_base(void) {
    for (unsigned int i = 0; i < APP_BACKGROUND_SLOT_COUNT; i++) {
        if (!shell_apps_phys_base_in_use(g_app_phys_slots[i])) {
            return g_app_phys_slots[i];
        }
    }

    return 0;
}

static void shell_apps_set_task_label(char* dst, int dst_size, const char* text, const char* fallback) {
    if (!dst || dst_size <= 0) {
        return;
    }

    if (!shell_apps_copy_string((text && text[0] != '\0') ? text : fallback, dst, dst_size)) {
        if (fallback) {
            (void)shell_apps_copy_string(fallback, dst, dst_size);
        } else {
            dst[0] = '\0';
        }
    }
}

static void shell_apps_copy_host_state(shell_app_task_t* task, const shell_app_host_t* host, int background, int serial_only) {
    if (!task || !host) {
        return;
    }

    shell_apps_mem_zero((unsigned char*)task, (unsigned int)sizeof(*task));
    task->in_use = 1;
    task->pid = scheduler_get_current_pid();
    task->leader_pid = task->pid;
    task->drive = drive_get_current();
    task->fat16_initialized = *host->fat16_initialized;
    task->app_clip_mode = *host->app_clip_mode;
    task->background = background ? 1 : 0;
    task->serial_only = serial_only ? 1 : 0;
    task->current_dir_cluster = *host->current_dir_cluster;
    task->app_clip_src_cluster = *host->app_clip_src_cluster;
    task->out_both = host->out_both;
    task->str_to_upper = host->str_to_upper;
    task->path_reset = host->path_reset;
    task->path_pop = host->path_pop;
    task->path_push = host->path_push;
    (void)shell_apps_copy_string(host->current_path, task->current_path, (int)sizeof(task->current_path));
    (void)shell_apps_copy_string(host->app_clip_name, task->app_clip_name, (int)sizeof(task->app_clip_name));
}

static void shell_apps_sync_host_state(const shell_app_task_t* task, const shell_app_host_t* host) {
    if (!task || !host) {
        return;
    }

    *host->current_dir_cluster = task->current_dir_cluster;
    *host->fat16_initialized = task->fat16_initialized;
    *host->app_clip_src_cluster = task->app_clip_src_cluster;
    *host->app_clip_mode = task->app_clip_mode;
    (void)shell_apps_copy_string(task->current_path, host->current_path, host->current_path_size);
    (void)shell_apps_copy_string(task->app_clip_name, host->app_clip_name, host->app_clip_name_size);
}

static void shell_apps_release_task(shell_app_task_t* task) {
    if (!task) {
        return;
    }

    shell_apps_mem_zero((unsigned char*)task, (unsigned int)sizeof(*task));
}

static int shell_apps_prepare_app_task(
    shell_app_task_t* task,
    const shell_app_host_t* host,
    int pid,
    unsigned int phys_base,
    int background,
    int serial_only,
    int join_on_exit
) {
    if (!task || !host || pid < APP_SLOT_FIRST_PID || pid >= SCHEDULER_MAX_PROCESSES || phys_base == 0U) {
        return 0;
    }

    log_serial_raw("APPBG051\n");
    shell_apps_copy_host_state(task, host, background, serial_only);
    log_serial_raw("APPBG052\n");
    task->pid = pid;
    task->leader_pid = pid;
    task->app_phys_base = phys_base;
    task->join_on_exit = join_on_exit ? 1 : 0;
    shell_apps_mem_zero((unsigned char*)phys_base, APP_SLOT_SIZE);
    if (!paging_build_app_directory(task->page_directory, task->user_pt, task->app_phys_base, APP_LOAD_VIRT_BASE, APP_SLOT_SIZE)) {
        return 0;
    }
    log_serial_raw("APPBG053\n");
    return 1;
}

static int shell_apps_clone_background_task(
    shell_app_task_t* dst,
    const shell_app_task_t* src,
    int pid,
    unsigned int entry_virtual,
    unsigned int thread_arg,
    const char* task_name
) {
    if (!dst || !src) {
        return 0;
    }

    shell_apps_mem_copy((unsigned char*)dst, (const unsigned char*)src, (unsigned int)sizeof(*dst));
    dst->in_use = 1;
    dst->pid = pid;
    dst->leader_pid = shell_apps_group_leader_pid(src);
    dst->entry_virtual = entry_virtual;
    dst->thread_arg = thread_arg;
    dst->join_on_exit = 0;
    dst->input_ready_logged = 0;
    shell_apps_set_task_label(dst->task_name, (int)sizeof(dst->task_name), task_name, "thread");
    if (!paging_build_app_directory(dst->page_directory, dst->user_pt, dst->app_phys_base, APP_LOAD_VIRT_BASE, APP_SLOT_SIZE)) {
        return 0;
    }
    return 1;
}

static int shell_apps_prepare_runtime_block(shell_app_task_t* task) {
    unsigned int base_phys;
    unsigned int* api_words;

    if (!task) {
        return 0;
    }

    base_phys = shell_apps_phys_addr_for_virtual(task, APP_RUNTIME_BLOCK_VIRT);
    if (base_phys == 0U) {
        return 0;
    }

    api_words = (unsigned int*)base_phys;
    api_words[0] = APP_SYSCALL_STUB_VIRT;
    shell_apps_mem_copy((unsigned char*)(base_phys + (APP_SYSCALL_STUB_VIRT - APP_RUNTIME_BLOCK_VIRT)),
        g_app_syscall_stub, (unsigned int)sizeof(g_app_syscall_stub));
    shell_apps_mem_copy((unsigned char*)(base_phys + (APP_EXIT_STUB_VIRT - APP_RUNTIME_BLOCK_VIRT)),
        g_app_exit_stub, (unsigned int)sizeof(g_app_exit_stub));
    shell_apps_mem_copy((unsigned char*)(base_phys + (APP_THREAD_STUB_VIRT - APP_RUNTIME_BLOCK_VIRT)),
        g_app_thread_stub, (unsigned int)sizeof(g_app_thread_stub));
    return 1;
}

static int shell_apps_prepare_user_launch(shell_app_task_t* task, unsigned int* entry_out, unsigned int* user_esp_out) {
    unsigned int stack_top;
    unsigned int stack_base_phys;
    unsigned int* stack_words;

    if (!task || !entry_out || !user_esp_out) {
        return 0;
    }
    if (!shell_apps_prepare_runtime_block(task)) {
        return 0;
    }

    stack_top = shell_apps_user_stack_top_for_pid(task->pid);
    stack_base_phys = shell_apps_phys_addr_for_virtual(task, stack_top - APP_THREAD_STACK_BYTES);
    if (stack_top == 0U || stack_base_phys == 0U) {
        return 0;
    }

    shell_apps_mem_zero((unsigned char*)stack_base_phys, APP_THREAD_STACK_BYTES);

    if (task->pid == shell_apps_group_leader_pid(task)) {
        stack_words = (unsigned int*)(shell_apps_phys_addr_for_virtual(task, stack_top) - (2U * sizeof(unsigned int)));
        stack_words[0] = APP_EXIT_STUB_VIRT;
        stack_words[1] = APP_RUNTIME_BLOCK_VIRT;
        *entry_out = task->entry_virtual;
        *user_esp_out = stack_top - (2U * sizeof(unsigned int));
    } else {
        stack_words = (unsigned int*)(shell_apps_phys_addr_for_virtual(task, stack_top) - (4U * sizeof(unsigned int)));
        stack_words[0] = APP_EXIT_STUB_VIRT;
        stack_words[1] = task->entry_virtual;
        stack_words[2] = APP_RUNTIME_BLOCK_VIRT;
        stack_words[3] = task->thread_arg;
        *entry_out = APP_THREAD_STUB_VIRT;
        *user_esp_out = stack_top - (4U * sizeof(unsigned int));
    }

    return 1;
}

static void shell_apps_release_group_members(int leader_pid, int skip_pid, int terminate_first) {
    for (int pid = APP_SLOT_FIRST_PID; pid < SCHEDULER_MAX_PROCESSES; pid++) {
        shell_app_task_t* task = &g_app_tasks[pid];

        if (!task->in_use || shell_apps_group_leader_pid(task) != leader_pid || task->pid == skip_pid) {
            continue;
        }

        if (terminate_first) {
            (void)scheduler_terminate_process(task->pid);
        }
        if (!terminate_first) {
            shell_apps_release_task(task);
        }
    }
}

static unsigned int shell_apps_enter_critical(void) {
    unsigned int flags = read_eflags();

    __asm__ volatile ("cli");
    return flags;
}

static void shell_apps_leave_critical(unsigned int flags) {
    if ((flags & EFLAGS_IF) != 0U) {
        __asm__ volatile ("sti");
    }
}

static int shell_apps_enter_drive_context(shell_app_task_t* task, int* previous_drive_out) {
    if (!shell_apps_task_valid(task) || !previous_drive_out) {
        return 0;
    }

    *previous_drive_out = drive_get_current();
    drive_set_current(task->drive);
    fat16_set_drive(task->drive);
    if (!task->fat16_initialized) {
        task->fat16_initialized = fat16_init();
    }
    return task->fat16_initialized;
}

static void shell_apps_leave_drive_context(int previous_drive) {
    drive_set_current(previous_drive);
    fat16_set_drive(previous_drive);
}

static void shell_apps_wait_for_task_exit(int pid) {
    while (pid >= APP_SLOT_FIRST_PID && pid < SCHEDULER_MAX_PROCESSES) {
        process_state_t state = scheduler_get_process_state(pid);
        if (state == PROCESS_TERMINATED || state == PROCESS_UNUSED) {
            return;
        }
        timer_wait_for_interrupt();
    }
}

static int shell_apps_spawn_app_thread(shell_app_task_t* parent, const app_thread_create_t* spec) {
    app_thread_create_t spec_copy;
    char child_name[24];
    const char* task_name = 0;
    shell_app_task_t* child;
    int pid;
    int actual_pid;

    if (!shell_apps_task_valid(parent) || !spec || !parent->background) {
        return -1;
    }
    if (!shell_apps_copy_user_bytes(parent, spec, &spec_copy, (unsigned int)sizeof(spec_copy))) {
        return -1;
    }
    if (!spec_copy.entry || !shell_apps_user_exec_ptr_valid((unsigned int)spec_copy.entry)) {
        return -1;
    }
    if (spec_copy.name) {
        if (!shell_apps_copy_user_string(parent, spec_copy.name, child_name, (int)sizeof(child_name))) {
            return -1;
        }
        task_name = child_name;
    }
    if (task_name && task_name[0] == '\0') {
        task_name = 0;
    }

    if (!parent->background) {
        return -1;
    }

    pid = shell_apps_find_free_pid();
    if (pid < 0) {
        return -1;
    }

    child = &g_app_tasks[pid];
    if (!shell_apps_clone_background_task(
        child,
        parent,
        pid,
        (unsigned int)spec_copy.entry,
        (unsigned int)spec_copy.arg,
        task_name
    )) {
        shell_apps_release_task(child);
        return -1;
    }
    shell_apps_log_task_marker("APPTH100", child);

    actual_pid = scheduler_spawn_kernel_task(
        child->task_name[0] != '\0' ? child->task_name : "thread",
        parent->origin_name[0] != '\0' ? parent->origin_name : parent->task_name,
        1,
        shell_apps_background_thread,
        child,
        child->page_directory
    );
    if (actual_pid < 0 || actual_pid != pid) {
        shell_apps_release_task(child);
        if (actual_pid >= 0 && actual_pid != pid) {
            (void)scheduler_terminate_process(actual_pid);
        }
        return -1;
    }

    return pid;
}

static int shell_apps_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    shell_app_task_t* task = shell_apps_active_task();
    FAT16_DirectoryEntry entry;
    unsigned int file_size = 0;
    char name_a[64];
    char name_b[64];

    if (!shell_apps_task_valid(task)) {
        return -1;
    }

    if (num == MINIDOS_SYSCALL_PUTS) {
        char text[APP_USER_TEXT_MAX];

        if (!shell_apps_copy_user_string(task, (const char*)a0, text, (int)sizeof(text))) {
            return -1;
        }
        shell_apps_task_out(task, text);
        return 0;
    }

    if (num == MINIDOS_SYSCALL_GET_CHAR) {
        return (int)(unsigned char)shell_apps_get_char();
    }

    if (num == MINIDOS_SYSCALL_GET_CHAR_NONBLOCK) {
        char c = 0;
        if (shell_apps_try_get_char(&c)) {
            return (int)(unsigned char)c;
        }
        return -1;
    }

    if (num == MINIDOS_SYSCALL_GET_MOUSE_STATE) {
        if (!shell_apps_user_range_valid(task, a0, (unsigned int)sizeof(app_mouse_state_t))) {
            return 0;
        }
        return shell_apps_get_mouse_state((app_mouse_state_t*)a0);
    }

    if (num == MINIDOS_SYSCALL_WAIT_EVENT) {
        return shell_apps_wait_event(a0, a1);
    }

    if (num == MINIDOS_SYSCALL_GET_TICKS) {
        if (!task->background) {
            shell_apps_flush_graphics();
        }
        return (int)scheduler_get_ticks();
    }

    if (num == MINIDOS_SYSCALL_GET_TIME) {
        rtc_time_t time;
        app_time_t* out = (app_time_t*)a0;

        if (!out
            || !shell_apps_user_range_valid(task, a0, (unsigned int)sizeof(*out))
            || !rtc_read_time(&time)) {
            return 0;
        }

        out->hours = time.hours;
        out->minutes = time.minutes;
        out->seconds = time.seconds;
        return 1;
    }

    if (num == MINIDOS_SYSCALL_THREAD_SPAWN) {
        return shell_apps_spawn_app_thread(task, (const app_thread_create_t*)a0);
    }

    if (num == MINIDOS_SYSCALL_THREAD_YIELD) {
        scheduler_yield();
        return 0;
    }

    if (num == MINIDOS_SYSCALL_THREAD_SELF) {
        return task->pid;
    }

    if (num == MINIDOS_SYSCALL_RANDOM) {
        unsigned int limit = a0;
        unsigned int value = shell_apps_random_u32() & 0x7FFFFFFFu;
        if (limit > 0) {
            value %= limit;
        }
        return (int)value;
    }

    if (num == MINIDOS_SYSCALL_FILE_SIZE) {
        int result = -1;
        int entered = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))) {
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered
                && fat16_find_entry(task->current_dir_cluster, name_a, &entry, 0, 0)
                && (entry.attributes & FAT16_ATTR_DIRECTORY) == 0) {
                result = (int)entry.file_size;
            }
        }

        if (entered) {
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_FILE_READ) {
        int result = -1;
        int previous_drive = 0;
        unsigned int flags;

        if (!a1 || (int)a2 <= 0 || !shell_apps_user_range_valid(task, a1, a2)) {
            return -1;
        }
        flags = shell_apps_enter_critical();
        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_enter_drive_context(task, &previous_drive)) {
            result = fat16_read_file_from_dir(task->current_dir_cluster, name_a, (unsigned char*)a1, (int)a2);
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_FILE_WRITE) {
        int result = 0;
        int previous_drive = 0;
        unsigned int flags;

        if (((const unsigned char*)a1 == 0 && (int)a2 > 0)
            || (int)a2 < 0
            || ((int)a2 > 0 && !shell_apps_user_range_valid(task, a1, a2))) {
            return 0;
        }
        flags = shell_apps_enter_critical();
        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_enter_drive_context(task, &previous_drive)) {
            result = fat16_write_file_from_dir(task->current_dir_cluster, name_a, (const unsigned char*)a1, (int)a2);
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_LIST_ENTRY) {
        int result = 0;
        int previous_drive = 0;
        unsigned int flags;

        if (!(char*)a1
            || !shell_apps_user_range_valid(task, a1, 13U)
            || (a2 != 0U && !shell_apps_user_range_valid(task, a2, (unsigned int)sizeof(int)))) {
            return 0;
        }
        flags = shell_apps_enter_critical();
        if (shell_apps_enter_drive_context(task, &previous_drive)) {
            result = fat16_get_entry_by_index(task->current_dir_cluster, a0, (char*)a1, 13, (int*)a2, &file_size);
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_COPY_FILE) {
        int result = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_copy_user_upper(task, (const char*)a1, name_b, sizeof(name_b))
            && shell_apps_enter_drive_context(task, &previous_drive)) {
            result = fat16_copy_file(task->current_dir_cluster, name_a, name_b);
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_MOVE_FILE) {
        int result = 0;
        int entered = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_copy_user_upper(task, (const char*)a1, name_b, sizeof(name_b))) {
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered
                && fat16_find_entry(task->current_dir_cluster, name_a, &entry, 0, 0)
                && (entry.attributes & FAT16_ATTR_DIRECTORY) == 0) {
                result = fat16_update_entry(task->current_dir_cluster, name_a, name_b, entry.cluster_low, entry.file_size, entry.attributes);
            }
        }
        if (entered) {
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_GFX_CLEAR) {
        if (task->background) {
            return 0;
        }
        video_clear_color(a0);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_RECT) {
        app_gfx_rect_t rect;

        if (task->background || !shell_apps_copy_user_bytes(task, (const void*)a0, &rect, (unsigned int)sizeof(rect))) {
            return 0;
        }
        video_fill_rect(rect.x, rect.y, rect.w, rect.h, rect.color);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_TEXT) {
        app_gfx_text_t text;
        char rendered[APP_USER_TEXT_MAX];

        if (task->background || !shell_apps_copy_user_bytes(task, (const void*)a0, &text, (unsigned int)sizeof(text))) {
            return 0;
        }
        if (!text.text || !shell_apps_copy_user_string(task, text.text, rendered, (int)sizeof(rendered))) {
            return 0;
        }
        video_draw_text_at(text.x, text.y, rendered, text.fg, text.bg);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_SIZE) {
        int* out_w = (int*)a0;
        int* out_h = (int*)a1;
        if (!out_w
            || !out_h
            || task->background
            || !shell_apps_user_range_valid(task, a0, (unsigned int)sizeof(*out_w))
            || !shell_apps_user_range_valid(task, a1, (unsigned int)sizeof(*out_h))) {
            return 0;
        }
        *out_w = video_get_width();
        *out_h = video_get_height();
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_PRESENT) {
        if (task->background) {
            return 0;
        }
        video_present_pending();
        return 1;
    }

    if (num == MINIDOS_SYSCALL_CHDIR) {
        int result = 0;
        int previous_drive = 0;
        unsigned int flags;

        if (!shell_apps_copy_user_string(task, (const char*)a0, name_a, sizeof(name_a))) {
            return 0;
        }

        if (name_a[0] == '\\' || name_a[0] == '/') {
            task->current_dir_cluster = 0;
            task->path_reset(task->current_path);
            return 1;
        }
        if (name_a[0] == '.' && name_a[1] == '.' && name_a[2] == '\0') {
            unsigned int parent_cluster = 0;
            int entered = 0;
            flags = shell_apps_enter_critical();
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered && fat16_get_parent_cluster(task->current_dir_cluster, &parent_cluster)) {
                task->current_dir_cluster = parent_cluster;
                task->path_pop(task->current_path);
                result = 1;
            }
            if (entered) {
                shell_apps_leave_drive_context(previous_drive);
            }
            shell_apps_leave_critical(flags);
            return result;
        }
        if (name_a[0] == '.' && name_a[1] == '\0') {
            return 1;
        }

        task->str_to_upper(name_a);
        {
            unsigned int next_cluster = 0;
            int entered = 0;
            flags = shell_apps_enter_critical();
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered
                && fat16_find_dir_cluster(task->current_dir_cluster, name_a, &next_cluster)
                && task->path_push(task->current_path, APP_PATH_MAX, name_a)) {
                task->current_dir_cluster = next_cluster;
                result = 1;
            }
            if (entered) {
                shell_apps_leave_drive_context(previous_drive);
            }
            shell_apps_leave_critical(flags);
        }
        return result;
    }

    if (num == MINIDOS_SYSCALL_MKDIR) {
        int result = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_enter_drive_context(task, &previous_drive)) {
            result = fat16_mkdir(task->current_dir_cluster, name_a);
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_RMDIR) {
        int result = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_enter_drive_context(task, &previous_drive)) {
            result = fat16_rmdir(task->current_dir_cluster, name_a);
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_DELETE_ENTRY) {
        int result = 0;
        int entered = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))) {
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered && fat16_find_entry(task->current_dir_cluster, name_a, &entry, 0, 0)) {
                if (entry.attributes & FAT16_ATTR_DIRECTORY) {
                    result = fat16_rmdir(task->current_dir_cluster, name_a);
                } else {
                    result = fat16_delete_entry(task->current_dir_cluster, name_a, 1);
                }
            }
        }
        if (entered) {
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_RENAME_ENTRY) {
        int result = 0;
        int entered = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_copy_user_upper(task, (const char*)a1, name_b, sizeof(name_b))) {
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered && fat16_find_entry(task->current_dir_cluster, name_a, &entry, 0, 0)) {
                result = fat16_update_entry(task->current_dir_cluster, name_a, name_b, entry.cluster_low, entry.file_size, entry.attributes);
            }
        }
        if (entered) {
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_COPY_TO_DIR || num == MINIDOS_SYSCALL_MOVE_TO_DIR) {
        unsigned int dst_dir_cluster = 0;
        int result = 0;
        int entered = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))
            && shell_apps_copy_user_upper(task, (const char*)a1, name_b, sizeof(name_b))) {
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered
                && fat16_find_dir_cluster(task->current_dir_cluster, name_b, &dst_dir_cluster)
                && fat16_copy_file_between_dirs(task->current_dir_cluster, name_a, dst_dir_cluster, name_a)) {
                result = 1;
                if (num == MINIDOS_SYSCALL_MOVE_TO_DIR) {
                    result = fat16_delete_entry(task->current_dir_cluster, name_a, 1);
                }
            }
        }
        if (entered) {
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_CLIP_SET) {
        int result = 0;
        int entered = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (shell_apps_copy_user_upper(task, (const char*)a0, name_a, sizeof(name_a))) {
            entered = shell_apps_enter_drive_context(task, &previous_drive);
            if (entered
                && fat16_find_entry(task->current_dir_cluster, name_a, &entry, 0, 0)
                && (entry.attributes & FAT16_ATTR_DIRECTORY) == 0
                && shell_apps_copy_string(name_a, task->app_clip_name, sizeof(task->app_clip_name))) {
                task->app_clip_src_cluster = task->current_dir_cluster;
                task->app_clip_mode = ((int)a1 == 2) ? 2 : 1;
                result = 1;
            }
        }
        if (entered) {
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    if (num == MINIDOS_SYSCALL_CLIP_PASTE) {
        unsigned int dst_dir_cluster = task->current_dir_cluster;
        int result = 0;
        int previous_drive = 0;
        unsigned int flags = shell_apps_enter_critical();

        if (task->app_clip_mode == 0 || task->app_clip_name[0] == '\0') {
            shell_apps_leave_critical(flags);
            return 0;
        }
        if (shell_apps_enter_drive_context(task, &previous_drive)) {
            if (a0 != 0U) {
                if (!shell_apps_copy_user_string(task, (const char*)a0, name_a, sizeof(name_a))) {
                    shell_apps_leave_drive_context(previous_drive);
                    shell_apps_leave_critical(flags);
                    return 0;
                }
                if (name_a[0] != '\0') {
                    task->str_to_upper(name_a);
                    if (!fat16_find_dir_cluster(task->current_dir_cluster, name_a, &dst_dir_cluster)) {
                        shell_apps_leave_drive_context(previous_drive);
                        shell_apps_leave_critical(flags);
                        return 0;
                    }
                }
            }
            if (fat16_copy_file_between_dirs(task->app_clip_src_cluster, task->app_clip_name, dst_dir_cluster, task->app_clip_name)) {
                result = 1;
                if (task->app_clip_mode == 2) {
                    result = fat16_delete_entry(task->app_clip_src_cluster, task->app_clip_name, 1);
                    if (result) {
                        task->app_clip_mode = 0;
                        task->app_clip_name[0] = '\0';
                    }
                }
            }
            shell_apps_leave_drive_context(previous_drive);
        }
        shell_apps_leave_critical(flags);
        return result;
    }

    return -1;
}

int shell_apps_handle_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    return shell_apps_syscall(num, a0, a1, a2);
}

void shell_apps_on_current_task_exit(void) {
    shell_app_task_t* task = shell_apps_active_task();
    int leader_pid;

    if (!shell_apps_task_valid(task)) {
        return;
    }

    leader_pid = shell_apps_group_leader_pid(task);
    shell_apps_log_task_marker("APPBG190", task);
    if (!task->background && task->pid == leader_pid) {
        shell_apps_note_session_return(task);
    }
    if (task->pid == leader_pid) {
        shell_apps_release_group_members(leader_pid, task->pid, 1);
    }
}

void shell_apps_on_current_task_fault(unsigned int vector, unsigned int error_code, unsigned int eip) {
    shell_app_task_t* task = shell_apps_active_task();
    int leader_pid;

    if (!shell_apps_task_valid(task)) {
        return;
    }

    leader_pid = shell_apps_group_leader_pid(task);
    log_serial_raw("APPFLT900 pid=");
    serial_print_hex((unsigned int)task->pid);
    log_serial_raw(" leader=");
    serial_print_hex((unsigned int)leader_pid);
    log_serial_raw(" vector=");
    serial_print_hex(vector);
    log_serial_raw(" error=");
    serial_print_hex(error_code);
    log_serial_raw(" eip=");
    serial_print_hex(eip);
    log_serial_raw("\n");

    if (!task->background && task->pid == leader_pid) {
        shell_apps_task_out(task, "App faulted and was terminated\n");
        shell_apps_note_session_return(task);
    }

    shell_apps_release_group_members(leader_pid, task->pid, 1);
}

static void shell_apps_mem_copy(unsigned char* dst, const unsigned char* src, unsigned int size) {
    for (unsigned int i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static int shell_apps_load_elf_image(const unsigned char* elf_data, int elf_size, unsigned int load_phys_base, unsigned int* entry_virtual_out) {
    const elf32_header_t* ehdr;

    if (!elf_data || !entry_virtual_out || elf_size < (int)sizeof(elf32_header_t)) {
        return -1;
    }

    ehdr = (const elf32_header_t*)elf_data;
    if (ehdr->ident[0] != 0x7F || ehdr->ident[1] != 'E' || ehdr->ident[2] != 'L' || ehdr->ident[3] != 'F') {
        return -1;
    }
    if (ehdr->ident[4] != 1 || ehdr->ident[5] != 1 || ehdr->version != 1) {
        return -1;
    }
    if (ehdr->type != 2 || ehdr->machine != 3) {
        return -1;
    }
    if (ehdr->phentsize != sizeof(elf32_program_header_t)) {
        return -1;
    }
    if (ehdr->phnum == 0 || ehdr->phnum > 16) {
        return -1;
    }
    if ((int)ehdr->phoff + (int)(ehdr->phnum * ehdr->phentsize) > elf_size) {
        return -1;
    }

    for (unsigned int i = 0; i < ehdr->phnum; i++) {
        const elf32_program_header_t* phdr = (const elf32_program_header_t*)(elf_data + ehdr->phoff + i * ehdr->phentsize);
        unsigned char* dst;
        const unsigned char* src;

        if (phdr->type != 1) {
            continue;
        }
        if (phdr->memsz == 0) {
            continue;
        }
        if (phdr->memsz < phdr->filesz) {
            return -1;
        }
        if ((int)phdr->offset + (int)phdr->filesz > elf_size) {
            return -1;
        }
        if (phdr->vaddr < APP_LOAD_VIRT_BASE || (phdr->vaddr + phdr->memsz) > APP_USER_IMAGE_LIMIT) {
            return -1;
        }

        dst = (unsigned char*)(load_phys_base + (phdr->vaddr - APP_LOAD_VIRT_BASE));
        src = elf_data + phdr->offset;
        shell_apps_mem_copy(dst, src, phdr->filesz);
        if (phdr->memsz > phdr->filesz) {
            shell_apps_mem_zero(dst + phdr->filesz, phdr->memsz - phdr->filesz);
        }
    }

    if (ehdr->entry < APP_LOAD_VIRT_BASE || ehdr->entry >= APP_USER_IMAGE_LIMIT) {
        return -1;
    }

    *entry_virtual_out = ehdr->entry;
    return 0;
}

static int shell_apps_load_com_image(const unsigned char* com_data, int com_size, unsigned int load_phys_base, unsigned int* entry_virtual_out) {
    if (!com_data || !entry_virtual_out || com_size <= 0 || com_size > APP_MAX_COM_SIZE) {
        return -1;
    }
    if ((APP_LOAD_VIRT_BASE + (unsigned int)com_size) > APP_USER_IMAGE_LIMIT) {
        return -1;
    }

    shell_apps_mem_copy((unsigned char*)load_phys_base, com_data, (unsigned int)com_size);
    *entry_virtual_out = APP_LOAD_VIRT_BASE;
    return 0;
}

static void shell_apps_background_thread(void* arg) {
    shell_app_task_t* task = (shell_app_task_t*)arg;
    unsigned int entry_virtual = 0;
    unsigned int user_esp = 0;

    if (!task || !task->in_use) {
        return;
    }

    shell_apps_begin_input_session(task);
    if (task->origin_name[0] != '\0') {
        shell_apps_begin_scheduler_origin(task->origin_name);
    }
    shell_apps_ensure_interrupts_enabled();
    shell_apps_log_task_marker("APPBG100", task);
    if (!shell_apps_prepare_user_launch(task, &entry_virtual, &user_esp)) {
        shell_apps_task_out(task, "User runtime setup failed\n");
        shell_apps_on_current_task_exit();
        scheduler_exit_current_task();
    }
    scheduler_enter_current_user_mode(entry_virtual, user_esp);
}

static int shell_apps_try_execute_com(const shell_app_host_t* host, const char* command, const char* args) {
    char normalized[64];
    char filename[64];
    shell_app_task_t* task;
    int pid;
    int actual_pid;
    int bytes_read;
    unsigned int entry_virtual = 0;
    unsigned int phys_base = 0;

    if (!shell_apps_host_valid(host)) {
        return 0;
    }
    if (command[0] == '\0' || args[0] != '\0') {
        return 0;
    }
    if (!shell_apps_ensure_host_fat16_ready(host)) {
        return 0;
    }
    if (!shell_apps_stage_com_candidate(host, command, normalized, (int)sizeof(normalized), filename, (int)sizeof(filename), &bytes_read)) {
        return 0;
    }

    shell_apps_out_both(host, "Executing ");
    shell_apps_out_both(host, filename);
    shell_apps_out_both(host, "...\n");

    shell_apps_reap_terminated_tasks();
    pid = shell_apps_find_free_pid();
    if (pid < 0) {
        shell_apps_out_both(host, "No scheduler slot available\n");
        return 1;
    }

    phys_base = shell_apps_allocate_phys_base();
    if (phys_base == 0U) {
        shell_apps_out_both(host, "No app memory slot available\n");
        return 1;
    }

    task = &g_app_tasks[pid];
    if (!shell_apps_prepare_app_task(task, host, pid, phys_base, 0, 0, 1)) {
        shell_apps_release_task(task);
        shell_apps_out_both(host, "Unable to prepare app task\n");
        return 1;
    }

    if (shell_apps_load_com_image(g_elf_buffer, bytes_read, task->app_phys_base, &entry_virtual) != 0) {
        shell_apps_release_task(task);
        shell_apps_out_both(host, "Invalid COM or load error\n");
        return 1;
    }

    task->entry_virtual = entry_virtual;
    shell_apps_set_task_label(task->task_name, (int)sizeof(task->task_name), normalized, "app");
    shell_apps_set_task_label(task->origin_name, (int)sizeof(task->origin_name), filename, "APP.COM");
    actual_pid = scheduler_spawn_kernel_task(
        normalized[0] != '\0' ? normalized : "app",
        filename,
        1,
        shell_apps_background_thread,
        task,
        task->page_directory
    );
    if (actual_pid < 0 || actual_pid != pid) {
        shell_apps_release_task(task);
        if (actual_pid >= 0 && actual_pid != pid) {
            (void)scheduler_terminate_process(actual_pid);
        }
        shell_apps_out_both(host, "Unable to start app task\n");
        return 1;
    }

    shell_apps_wait_for_task_exit(pid);
    shell_apps_sync_host_state(task, host);
    shell_apps_release_task(task);
    return 1;
}

int shell_apps_try_execute_elf(const shell_app_host_t* host, const char* command, const char* args) {
    char normalized[64];
    char filename[64];
    shell_app_task_t* task;
    int pid;
    int actual_pid;
    int bytes_read;
    unsigned int entry_virtual = 0;
    unsigned int phys_base = 0;

    if (!shell_apps_host_valid(host)) {
        return 0;
    }
    if (command[0] == '\0' || args[0] != '\0') {
        return 0;
    }

    shell_apps_normalize_app_name(command, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return 0;
    }
    if (shell_apps_has_extension(normalized, ".elf")) {
        if (!shell_apps_append_extension(normalized, "", 0, filename, sizeof(filename))) {
            return 0;
        }
    } else {
        if (!shell_apps_append_extension(normalized, ".elf", 0, filename, sizeof(filename))) {
            return 0;
        }
    }
    if (!shell_apps_ensure_host_fat16_ready(host)) {
        return 0;
    }

    host->str_to_upper(filename);
    bytes_read = fat16_read_file_from_dir(*host->current_dir_cluster, filename, g_elf_buffer, APP_MAX_ELF_SIZE);
    if (bytes_read <= 0) {
        return 0;
    }

    shell_apps_out_both(host, "Executing ");
    shell_apps_out_both(host, filename);
    shell_apps_out_both(host, "...\n");

    shell_apps_reap_terminated_tasks();
    pid = shell_apps_find_free_pid();
    if (pid < 0) {
        shell_apps_out_both(host, "No scheduler slot available\n");
        return 1;
    }

    phys_base = shell_apps_allocate_phys_base();
    if (phys_base == 0U) {
        shell_apps_out_both(host, "No app memory slot available\n");
        return 1;
    }

    task = &g_app_tasks[pid];
    if (!shell_apps_prepare_app_task(task, host, pid, phys_base, 0, 0, 1)) {
        shell_apps_release_task(task);
        shell_apps_out_both(host, "Unable to prepare app task\n");
        return 1;
    }

    if (shell_apps_load_elf_image(g_elf_buffer, bytes_read, task->app_phys_base, &entry_virtual) != 0) {
        shell_apps_release_task(task);
        shell_apps_out_both(host, "Invalid ELF or load error\n");
        return 1;
    }

    task->entry_virtual = entry_virtual;
    shell_apps_set_task_label(task->task_name, (int)sizeof(task->task_name), normalized, "app");
    shell_apps_set_task_label(task->origin_name, (int)sizeof(task->origin_name), filename, "APP.ELF");
    actual_pid = scheduler_spawn_kernel_task(
        normalized[0] != '\0' ? normalized : "app",
        filename,
        1,
        shell_apps_background_thread,
        task,
        task->page_directory
    );
    if (actual_pid < 0 || actual_pid != pid) {
        shell_apps_release_task(task);
        if (actual_pid >= 0 && actual_pid != pid) {
            (void)scheduler_terminate_process(actual_pid);
        }
        shell_apps_out_both(host, "Unable to start app task\n");
        return 1;
    }

    shell_apps_wait_for_task_exit(pid);
    shell_apps_sync_host_state(task, host);
    shell_apps_release_task(task);
    return 1;
}

int shell_apps_try_execute_program(const shell_app_host_t* host, const char* command, const char* args) {
    if (!command) {
        return 0;
    }
    if (shell_apps_has_extension(command, ".com")) {
        return shell_apps_try_execute_com(host, command, args);
    }
    if (shell_apps_has_extension(command, ".elf")) {
        return shell_apps_try_execute_elf(host, command, args);
    }
    if (shell_apps_try_execute_elf(host, command, args)) {
        return 1;
    }
    return shell_apps_try_execute_com(host, command, args);
}

int shell_apps_run_background(const shell_app_host_t* host, const char* command, int* pid_out) {
    char normalized[64];
    char filename[64];
    shell_app_task_t* task;
    int pid;
    int actual_pid;
    int bytes_read;
    shell_app_format_t format = APP_FORMAT_NONE;
    unsigned int entry_virtual = 0;
    unsigned int phys_base = 0;

    if (!shell_apps_host_valid(host) || !command || command[0] == '\0') {
        return 0;
    }

    shell_apps_reap_terminated_tasks();
    log_serial_raw("APPBG001\n");

    shell_apps_normalize_app_name(command, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return 0;
    }
    log_serial_raw("APPBG002\n");
    if (!shell_apps_ensure_host_fat16_ready(host)) {
        return 0;
    }
    log_serial_raw("APPBG003\n");

    if (!shell_apps_has_extension(command, ".com")
        && shell_apps_stage_elf_candidate(host, command, 0, 0, filename, (int)sizeof(filename), &bytes_read)) {
        format = APP_FORMAT_ELF;
    } else if (!shell_apps_has_extension(command, ".elf")
        && shell_apps_stage_com_candidate(host, command, 0, 0, filename, (int)sizeof(filename), &bytes_read)) {
        format = APP_FORMAT_COM;
    } else {
        return 0;
    }

    pid = shell_apps_find_free_pid();
    if (pid < 0) {
        return 0;
    }
    log_serial_raw("APPBG004\n");

    phys_base = shell_apps_allocate_phys_base();
    if (phys_base == 0U) {
        return 0;
    }
    log_serial_raw("APPBG005\n");

    task = &g_app_tasks[pid];
    if (!shell_apps_prepare_app_task(task, host, pid, phys_base, 1, 1, 0)) {
        shell_apps_release_task(task);
        return 0;
    }
    log_serial_raw("APPBG006\n");
    log_serial_raw("APPBG007\n");
    if ((format == APP_FORMAT_ELF
            && shell_apps_load_elf_image(g_elf_buffer, bytes_read, task->app_phys_base, &entry_virtual) != 0)
        || (format == APP_FORMAT_COM
            && shell_apps_load_com_image(g_elf_buffer, bytes_read, task->app_phys_base, &entry_virtual) != 0)) {
        shell_apps_release_task(task);
        return 0;
    }
    log_serial_raw("APPBG008\n");

    task->entry_virtual = entry_virtual;
    shell_apps_set_task_label(task->task_name, (int)sizeof(task->task_name), normalized, "app");
    shell_apps_set_task_label(
        task->origin_name,
        (int)sizeof(task->origin_name),
        filename,
        (format == APP_FORMAT_COM) ? "APP.COM" : "APP.ELF"
    );
    shell_apps_log_task_marker("APPBG010", task);
    actual_pid = scheduler_spawn_kernel_task(
        normalized[0] != '\0' ? normalized : "app",
        filename,
        1,
        shell_apps_background_thread,
        task,
        task->page_directory
    );
    if (actual_pid < 0 || actual_pid != pid) {
        shell_apps_release_task(task);
        if (actual_pid >= 0 && actual_pid != pid) {
            (void)scheduler_terminate_process(actual_pid);
        }
        return 0;
    }

    if (pid_out) {
        *pid_out = pid;
    }
    return 1;
}

int shell_apps_stop_background(int pid) {
    shell_apps_reap_terminated_tasks();

    shell_app_task_t* task = shell_apps_find_task(pid);
    int leader_pid;
    int stopped = 0;

    if (!task) {
        return 0;
    }

    leader_pid = shell_apps_group_leader_pid(task);
    if (leader_pid < APP_SLOT_FIRST_PID) {
        return 0;
    }

    shell_apps_release_group_members(leader_pid, leader_pid, 1);
    if (scheduler_terminate_process(leader_pid)) {
        stopped = 1;
    }

    return stopped;
}

static int shell_apps_script_is_space(char c) {
    return c == ' ' || c == '\t';
}

static int shell_apps_script_is_rem_line(const char* line) {
    char a = line[0];
    char b = line[1];
    char c = line[2];

    if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
    if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    if (a == 'R' && b == 'E' && c == 'M') {
        char next = line[3];
        if (next == '\0' || shell_apps_script_is_space(next)) {
            return 1;
        }
    }
    return 0;
}

static int shell_apps_try_execute_aut(const shell_app_host_t* host, const char* command, const char* args) {
    char filename[64];
    static unsigned char script_buffer[2049];
    char line[65];
    int bytes_read;
    int i;

    if (command[0] == '\0' || args[0] != '\0') {
        return 0;
    }
    if (!shell_apps_append_extension(command, ".aut", 1, filename, sizeof(filename))) {
        return 0;
    }
    if (!shell_apps_ensure_host_fat16_ready(host)) {
        return 0;
    }

    host->str_to_upper(filename);
    bytes_read = fat16_read_file_from_dir(*host->current_dir_cluster, filename, script_buffer, 2048);
    if (bytes_read <= 0) {
        return 0;
    }

    script_buffer[bytes_read] = '\0';
    shell_apps_out_both(host, "Running ");
    shell_apps_out_both(host, filename);
    shell_apps_out_both(host, "...\n");

    i = 0;
    while (i < bytes_read) {
        int line_len = 0;
        int start;
        int end;

        while (i < bytes_read && script_buffer[i] != '\n') {
            if (script_buffer[i] != '\r' && line_len < (int)sizeof(line) - 1) {
                line[line_len++] = (char)script_buffer[i];
            }
            i++;
        }
        if (i < bytes_read && script_buffer[i] == '\n') {
            i++;
        }
        line[line_len] = '\0';

        start = 0;
        while (line[start] != '\0' && shell_apps_script_is_space(line[start])) {
            start++;
        }
        end = line_len;
        while (end > start && shell_apps_script_is_space(line[end - 1])) {
            end--;
        }
        line[end] = '\0';

        if (line[start] == '\0' || shell_apps_script_is_rem_line(&line[start])) {
            continue;
        }

        shell_execute(&line[start]);
    }

    return 1;
}

int shell_apps_try_execute(const shell_app_host_t* host, const char* command, const char* args) {
    if (!shell_apps_host_valid(host)) {
        return 0;
    }
    if (shell_apps_try_execute_program(host, command, args)) {
        return 1;
    }
    return shell_apps_try_execute_aut(host, command, args);
}
