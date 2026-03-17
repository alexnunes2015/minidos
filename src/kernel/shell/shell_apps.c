#include "shell_apps.h"
#include "shell.h"
#include "drive.h"
#include "fat16.h"
#include "keyboard.h"
#include "logger.h"
#include "mouse.h"
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

static const shell_app_host_t* active_host = 0;
static const unsigned int EFLAGS_IF = 0x00000200U;
static int app_input_ready_logged = 0;
static unsigned int app_rng_state = 0xA5F21C3Du;

static void shell_apps_flush_graphics(void) {
    video_present_pending();
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

static inline unsigned int read_eflags(void) {
    unsigned int flags;
    __asm__ volatile ("pushf\npop %0" : "=r"(flags));
    return flags;
}

static int shell_apps_ensure_fat16_ready(const shell_app_host_t* host) {
    if (!host || !host->fat16_initialized) {
        return 0;
    }
    if (!*host->fat16_initialized) {
        fat16_set_drive(drive_get_current());
        *host->fat16_initialized = fat16_init();
    }
    return *host->fat16_initialized;
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

static int shell_apps_copy_upper(const shell_app_host_t* host, const char* input, char* out, int out_size) {
    if (!shell_apps_copy_string(input, out, out_size)) {
        return 0;
    }
    host->str_to_upper(out);
    return 1;
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

static void shell_apps_begin_input_session(void) {
    app_input_ready_logged = 0;
}

static void shell_apps_note_input_ready(void) {
    if (!app_input_ready_logged) {
        log_serial_raw("APPIN001\n");
        app_input_ready_logged = 1;
    }
}

static void shell_apps_note_session_return(void) {
    log_serial_raw("APPRET001\n");
    app_input_ready_logged = 0;
}

static void shell_apps_begin_scheduler_origin(const char* origin_name) {
    scheduler_set_current_origin(origin_name, 1);
}

static void shell_apps_end_scheduler_origin(void) {
    scheduler_set_current_origin(0, 0);
}

static void shell_apps_ensure_interrupts_enabled(void) {
    if ((read_eflags() & EFLAGS_IF) == 0U) {
        __asm__ volatile ("sti");
    }
}

static int shell_apps_try_get_char(char* out) {
    if (!out || !shell_apps_host_valid(active_host)) {
        return 0;
    }

    shell_apps_ensure_interrupts_enabled();
    shell_apps_flush_graphics();
    shell_apps_note_input_ready();

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

    if (!out || !shell_apps_host_valid(active_host)) {
        return 0;
    }

    shell_apps_ensure_interrupts_enabled();
    shell_apps_flush_graphics();
    shell_apps_note_input_ready();

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
    unsigned int start_ticks = scheduler_get_ticks();
    unsigned int timeout_ticks = timeout_ms ? timer_ms_to_ticks_ceil(timeout_ms) : 0;

    if (!shell_apps_host_valid(active_host)) {
        return 0;
    }

    shell_apps_ensure_interrupts_enabled();
    shell_apps_flush_graphics();
    shell_apps_note_input_ready();

    while (1) {
        int flags = 0;

        if (keyboard_has_input() || serial_received()) {
            flags |= APP_EVENT_KEY;
        }

        if (mouse_get_state(&state) && state.present && state.seq != last_mouse_seq) {
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

static int shell_apps_syscall(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    FAT16_DirectoryEntry entry;
    unsigned int file_size = 0;
    char name_a[64];
    char name_b[64];

    if (!shell_apps_host_valid(active_host)) {
        return -1;
    }

    if (num == MINIDOS_SYSCALL_PUTS) {
        shell_apps_out_both(active_host, (const char*)a0);
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
        return shell_apps_get_mouse_state((app_mouse_state_t*)a0);
    }

    if (num == MINIDOS_SYSCALL_WAIT_EVENT) {
        return shell_apps_wait_event(a0, a1);
    }

    if (num == MINIDOS_SYSCALL_GET_TICKS) {
        shell_apps_flush_graphics();
        return (int)scheduler_get_ticks();
    }

    if (num == MINIDOS_SYSCALL_GET_TIME) {
        rtc_time_t time;
        app_time_t* out = (app_time_t*)a0;

        if (!out || !rtc_read_time(&time)) {
            return 0;
        }

        out->hours = time.hours;
        out->minutes = time.minutes;
        out->seconds = time.seconds;
        return 1;
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
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
            return -1;
        }
        if (!fat16_find_entry(*active_host->current_dir_cluster, name_a, &entry, 0, 0)) {
            return -1;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return -1;
        }
        return (int)entry.file_size;
    }

    if (num == MINIDOS_SYSCALL_FILE_READ) {
        if (!a1 || (int)a2 <= 0) {
            return -1;
        }
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
            return -1;
        }
        return fat16_read_file_from_dir(*active_host->current_dir_cluster, name_a, (unsigned char*)a1, (int)a2);
    }

    if (num == MINIDOS_SYSCALL_FILE_WRITE) {
        if (((const unsigned char*)a1 == 0 && (int)a2 > 0) || (int)a2 < 0) {
            return 0;
        }
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
            return 0;
        }
        return fat16_write_file_from_dir(*active_host->current_dir_cluster, name_a, (const unsigned char*)a1, (int)a2);
    }

    if (num == MINIDOS_SYSCALL_LIST_ENTRY) {
        if (!(char*)a1) {
            return 0;
        }
        return fat16_get_entry_by_index(*active_host->current_dir_cluster, a0, (char*)a1, 13, (int*)a2, &file_size);
    }

    if (num == MINIDOS_SYSCALL_COPY_FILE) {
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))
            || !shell_apps_copy_upper(active_host, (const char*)a1, name_b, sizeof(name_b))) {
            return 0;
        }
        return fat16_copy_file(*active_host->current_dir_cluster, name_a, name_b);
    }

    if (num == MINIDOS_SYSCALL_MOVE_FILE) {
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))
            || !shell_apps_copy_upper(active_host, (const char*)a1, name_b, sizeof(name_b))) {
            return 0;
        }
        if (!fat16_find_entry(*active_host->current_dir_cluster, name_a, &entry, 0, 0)) {
            return 0;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return 0;
        }
        return fat16_update_entry(*active_host->current_dir_cluster, name_a, name_b, entry.cluster_low, entry.file_size, entry.attributes);
    }

    if (num == MINIDOS_SYSCALL_GFX_CLEAR) {
        video_clear_color(a0);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_RECT) {
        const app_gfx_rect_t* rect = (const app_gfx_rect_t*)a0;
        if (!rect) {
            return 0;
        }
        video_fill_rect(rect->x, rect->y, rect->w, rect->h, rect->color);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_TEXT) {
        const app_gfx_text_t* text = (const app_gfx_text_t*)a0;
        if (!text || !text->text) {
            return 0;
        }
        video_draw_text_at(text->x, text->y, text->text, text->fg, text->bg);
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_SIZE) {
        int* out_w = (int*)a0;
        int* out_h = (int*)a1;
        if (!out_w || !out_h) {
            return 0;
        }
        *out_w = video_get_width();
        *out_h = video_get_height();
        return 1;
    }

    if (num == MINIDOS_SYSCALL_GFX_PRESENT) {
        video_present_pending();
        return 1;
    }

    if (num == MINIDOS_SYSCALL_CHDIR) {
        if (!shell_apps_copy_string((const char*)a0, name_a, sizeof(name_a))) {
            return 0;
        }

        if (name_a[0] == '\\' || name_a[0] == '/') {
            *active_host->current_dir_cluster = 0;
            active_host->path_reset(active_host->current_path);
            return 1;
        }
        if (name_a[0] == '.' && name_a[1] == '.' && name_a[2] == '\0') {
            unsigned int parent_cluster = 0;
            if (fat16_get_parent_cluster(*active_host->current_dir_cluster, &parent_cluster)) {
                *active_host->current_dir_cluster = parent_cluster;
                active_host->path_pop(active_host->current_path);
                return 1;
            }
            return 0;
        }
        if (name_a[0] == '.' && name_a[1] == '\0') {
            return 1;
        }

        active_host->str_to_upper(name_a);
        {
            unsigned int next_cluster = 0;
            if (!fat16_find_dir_cluster(*active_host->current_dir_cluster, name_a, &next_cluster)) {
                return 0;
            }
            if (!active_host->path_push(active_host->current_path, active_host->current_path_size, name_a)) {
                return 0;
            }
            *active_host->current_dir_cluster = next_cluster;
        }
        return 1;
    }

    if (num == MINIDOS_SYSCALL_MKDIR) {
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
            return 0;
        }
        return fat16_mkdir(*active_host->current_dir_cluster, name_a);
    }

    if (num == MINIDOS_SYSCALL_RMDIR) {
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
            return 0;
        }
        return fat16_rmdir(*active_host->current_dir_cluster, name_a);
    }

    if (num == MINIDOS_SYSCALL_DELETE_ENTRY) {
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
            return 0;
        }
        if (!fat16_find_entry(*active_host->current_dir_cluster, name_a, &entry, 0, 0)) {
            return 0;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return fat16_rmdir(*active_host->current_dir_cluster, name_a);
        }
        return fat16_delete_entry(*active_host->current_dir_cluster, name_a, 1);
    }

    if (num == MINIDOS_SYSCALL_RENAME_ENTRY) {
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))
            || !shell_apps_copy_upper(active_host, (const char*)a1, name_b, sizeof(name_b))) {
            return 0;
        }
        if (!fat16_find_entry(*active_host->current_dir_cluster, name_a, &entry, 0, 0)) {
            return 0;
        }
        return fat16_update_entry(*active_host->current_dir_cluster, name_a, name_b, entry.cluster_low, entry.file_size, entry.attributes);
    }

    if (num == MINIDOS_SYSCALL_COPY_TO_DIR || num == MINIDOS_SYSCALL_MOVE_TO_DIR) {
        unsigned int dst_dir_cluster = 0;

        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))
            || !shell_apps_copy_upper(active_host, (const char*)a1, name_b, sizeof(name_b))) {
            return 0;
        }
        if (!fat16_find_dir_cluster(*active_host->current_dir_cluster, name_b, &dst_dir_cluster)) {
            return 0;
        }
        if (!fat16_copy_file_between_dirs(*active_host->current_dir_cluster, name_a, dst_dir_cluster, name_a)) {
            return 0;
        }
        if (num == MINIDOS_SYSCALL_MOVE_TO_DIR) {
            if (!fat16_delete_entry(*active_host->current_dir_cluster, name_a, 1)) {
                return 0;
            }
        }
        return 1;
    }

    if (num == MINIDOS_SYSCALL_CLIP_SET) {
        if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
            return 0;
        }
        if (!fat16_find_entry(*active_host->current_dir_cluster, name_a, &entry, 0, 0)) {
            return 0;
        }
        if (entry.attributes & FAT16_ATTR_DIRECTORY) {
            return 0;
        }

        *active_host->app_clip_src_cluster = *active_host->current_dir_cluster;
        if (!shell_apps_copy_string(name_a, active_host->app_clip_name, active_host->app_clip_name_size)) {
            return 0;
        }
        *active_host->app_clip_mode = ((int)a1 == 2) ? 2 : 1;
        return 1;
    }

    if (num == MINIDOS_SYSCALL_CLIP_PASTE) {
        unsigned int dst_dir_cluster = *active_host->current_dir_cluster;

        if (*active_host->app_clip_mode == 0 || active_host->app_clip_name[0] == '\0') {
            return 0;
        }
        if ((const char*)a0 && ((const char*)a0)[0] != '\0') {
            if (!shell_apps_copy_upper(active_host, (const char*)a0, name_a, sizeof(name_a))) {
                return 0;
            }
            if (!fat16_find_dir_cluster(*active_host->current_dir_cluster, name_a, &dst_dir_cluster)) {
                return 0;
            }
        }
        if (!fat16_copy_file_between_dirs(*active_host->app_clip_src_cluster, active_host->app_clip_name, dst_dir_cluster, active_host->app_clip_name)) {
            return 0;
        }
        if (*active_host->app_clip_mode == 2) {
            if (!fat16_delete_entry(*active_host->app_clip_src_cluster, active_host->app_clip_name, 1)) {
                return 0;
            }
            *active_host->app_clip_mode = 0;
            active_host->app_clip_name[0] = '\0';
        }
        return 1;
    }

    return -1;
}

static void shell_apps_mem_copy(unsigned char* dst, const unsigned char* src, unsigned int size) {
    for (unsigned int i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static void shell_apps_mem_zero(unsigned char* dst, unsigned int size) {
    for (unsigned int i = 0; i < size; i++) {
        dst[i] = 0;
    }
}

static int shell_apps_load_elf_and_run(const unsigned char* elf_data, int elf_size, const minidos_app_api_t* api) {
    const elf32_header_t* ehdr;
    typedef int (*app_entry_t)(const minidos_app_api_t* api);
    app_entry_t entry;

    if (elf_size < (int)sizeof(elf32_header_t)) {
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
        if (phdr->vaddr < 0x200000 || (phdr->vaddr + phdr->memsz) > 0x300000) {
            return -1;
        }

        dst = (unsigned char*)phdr->vaddr;
        src = elf_data + phdr->offset;
        shell_apps_mem_copy(dst, src, phdr->filesz);
        if (phdr->memsz > phdr->filesz) {
            shell_apps_mem_zero(dst + phdr->filesz, phdr->memsz - phdr->filesz);
        }
    }

    if (ehdr->entry < 0x200000 || ehdr->entry >= 0x300000) {
        return -1;
    }

    entry = (app_entry_t)ehdr->entry;
    return entry(api);
}

static int shell_apps_try_execute_com(const shell_app_host_t* host, const char* command, const char* args) {
    char filename[64];
    static unsigned char* const load_addr = (unsigned char*)0x200000;
    static const int max_com_size = 65536;
    minidos_app_api_t api;
    int bytes_read;
    int exit_code;
    typedef int (*com_entry_t)(const minidos_app_api_t* api);
    com_entry_t entry;

    if (command[0] == '\0' || args[0] != '\0') {
        return 0;
    }
    if (!shell_apps_append_extension(command, ".com", 1, filename, sizeof(filename))) {
        return 0;
    }
    if (!shell_apps_ensure_fat16_ready(host)) {
        return 0;
    }

    host->str_to_upper(filename);
    bytes_read = fat16_read_file_from_dir(*host->current_dir_cluster, filename, load_addr, max_com_size);
    if (bytes_read <= 0) {
        return 0;
    }

    shell_apps_out_both(host, "Executing ");
    shell_apps_out_both(host, filename);
    shell_apps_out_both(host, "...\n");

    shell_apps_begin_scheduler_origin(filename);
    active_host = host;
    shell_apps_begin_input_session();
    shell_apps_ensure_interrupts_enabled();
    video_set_deferred_present(1);

    api.syscall = shell_apps_syscall;
    entry = (com_entry_t)load_addr;
    exit_code = entry(&api);

    video_present_pending();
    video_set_deferred_present(0);
    shell_apps_note_session_return();
    active_host = 0;
    app_input_ready_logged = 0;
    shell_apps_end_scheduler_origin();
    (void)exit_code;
    return 1;
}

int shell_apps_try_execute_elf(const shell_app_host_t* host, const char* command, const char* args) {
    char normalized[64];
    char filename[64];
    static unsigned char* const elf_buffer = (unsigned char*)0x300000;
    static const int max_elf_size = 262144;
    minidos_app_api_t api;
    int bytes_read;
    int exit_code;

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
    if (!shell_apps_ensure_fat16_ready(host)) {
        return 0;
    }

    host->str_to_upper(filename);
    bytes_read = fat16_read_file_from_dir(*host->current_dir_cluster, filename, elf_buffer, max_elf_size);
    if (bytes_read <= 0) {
        return 0;
    }

    shell_apps_out_both(host, "Executing ");
    shell_apps_out_both(host, filename);
    shell_apps_out_both(host, "...\n");

    shell_apps_begin_scheduler_origin(filename);
    active_host = host;
    shell_apps_begin_input_session();
    shell_apps_ensure_interrupts_enabled();
    video_set_deferred_present(1);

    api.syscall = shell_apps_syscall;
    exit_code = shell_apps_load_elf_and_run(elf_buffer, bytes_read, &api);
    if (exit_code == -1) {
        video_present_pending();
        video_set_deferred_present(0);
        active_host = 0;
        app_input_ready_logged = 0;
        shell_apps_end_scheduler_origin();
        shell_apps_out_both(host, "Invalid ELF or load error\n");
        return 1;
    }

    video_present_pending();
    video_set_deferred_present(0);
    shell_apps_note_session_return();
    active_host = 0;
    app_input_ready_logged = 0;
    shell_apps_end_scheduler_origin();
    (void)exit_code;
    return 1;
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
    if (!shell_apps_ensure_fat16_ready(host)) {
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
    if (shell_apps_try_execute_elf(host, command, args)) {
        return 1;
    }
    if (shell_apps_try_execute_com(host, command, args)) {
        return 1;
    }
    return shell_apps_try_execute_aut(host, command, args);
}
