#ifndef MINIDOS_APP_H
#define MINIDOS_APP_H

typedef struct {
    int (*syscall)(unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2);
} minidos_app_api_t;

enum {
    MINIDOS_SYSCALL_PUTS = 1,
    MINIDOS_SYSCALL_GET_CHAR = 2,
    MINIDOS_SYSCALL_FILE_SIZE = 3,
    MINIDOS_SYSCALL_LIST_ENTRY = 4,
    MINIDOS_SYSCALL_COPY_FILE = 5,
    MINIDOS_SYSCALL_MOVE_FILE = 6,
    MINIDOS_SYSCALL_GFX_CLEAR = 7,
    MINIDOS_SYSCALL_GFX_RECT = 8,
    MINIDOS_SYSCALL_GFX_TEXT = 9,
    MINIDOS_SYSCALL_GFX_SIZE = 10,
    MINIDOS_SYSCALL_CHDIR = 11,
    MINIDOS_SYSCALL_MKDIR = 12,
    MINIDOS_SYSCALL_RMDIR = 13,
    MINIDOS_SYSCALL_DELETE_ENTRY = 14,
    MINIDOS_SYSCALL_RENAME_ENTRY = 15,
    MINIDOS_SYSCALL_COPY_TO_DIR = 16,
    MINIDOS_SYSCALL_MOVE_TO_DIR = 17,
    MINIDOS_SYSCALL_CLIP_SET = 18,
    MINIDOS_SYSCALL_CLIP_PASTE = 19,
    MINIDOS_SYSCALL_RANDOM = 20,
    MINIDOS_SYSCALL_FILE_READ = 21,
    MINIDOS_SYSCALL_FILE_WRITE = 22,
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
    unsigned int color;
} app_gfx_rect_t;

typedef struct {
    int x;
    int y;
    const char* text;
    unsigned int fg;
    unsigned int bg;
} app_gfx_text_t;

static inline int app_syscall(const minidos_app_api_t* api, unsigned int num, unsigned int a0, unsigned int a1, unsigned int a2) {
    if (api && api->syscall) {
        return api->syscall(num, a0, a1, a2);
    }
    return -1;
}

static inline void app_puts(const minidos_app_api_t* api, const char* text) {
    (void)app_syscall(api, MINIDOS_SYSCALL_PUTS, (unsigned int)text, 0, 0);
}

static inline char app_get_char(const minidos_app_api_t* api) {
    int c = app_syscall(api, MINIDOS_SYSCALL_GET_CHAR, 0, 0, 0);
    if (c < 0) {
        return 0;
    }
    return (char)c;
}

static inline int app_file_size(const minidos_app_api_t* api, const char* path) {
    return app_syscall(api, MINIDOS_SYSCALL_FILE_SIZE, (unsigned int)path, 0, 0);
}

static inline int app_list_entry(const minidos_app_api_t* api, unsigned int index, char* out_name, int* out_is_dir) {
    return app_syscall(api, MINIDOS_SYSCALL_LIST_ENTRY, index, (unsigned int)out_name, (unsigned int)out_is_dir);
}

static inline int app_copy_file(const minidos_app_api_t* api, const char* src_name, const char* dst_name) {
    return app_syscall(api, MINIDOS_SYSCALL_COPY_FILE, (unsigned int)src_name, (unsigned int)dst_name, 0);
}

static inline int app_move_file(const minidos_app_api_t* api, const char* src_name, const char* dst_name) {
    return app_syscall(api, MINIDOS_SYSCALL_MOVE_FILE, (unsigned int)src_name, (unsigned int)dst_name, 0);
}

static inline int app_gfx_clear(const minidos_app_api_t* api, unsigned int color) {
    return app_syscall(api, MINIDOS_SYSCALL_GFX_CLEAR, color, 0, 0);
}

static inline int app_gfx_rect(const minidos_app_api_t* api, const app_gfx_rect_t* rect) {
    return app_syscall(api, MINIDOS_SYSCALL_GFX_RECT, (unsigned int)rect, 0, 0);
}

static inline int app_gfx_text(const minidos_app_api_t* api, const app_gfx_text_t* text) {
    return app_syscall(api, MINIDOS_SYSCALL_GFX_TEXT, (unsigned int)text, 0, 0);
}

static inline int app_gfx_size(const minidos_app_api_t* api, int* out_w, int* out_h) {
    return app_syscall(api, MINIDOS_SYSCALL_GFX_SIZE, (unsigned int)out_w, (unsigned int)out_h, 0);
}

static inline int app_chdir(const minidos_app_api_t* api, const char* dir_name) {
    return app_syscall(api, MINIDOS_SYSCALL_CHDIR, (unsigned int)dir_name, 0, 0);
}

static inline int app_mkdir(const minidos_app_api_t* api, const char* dir_name) {
    return app_syscall(api, MINIDOS_SYSCALL_MKDIR, (unsigned int)dir_name, 0, 0);
}

static inline int app_rmdir(const minidos_app_api_t* api, const char* dir_name) {
    return app_syscall(api, MINIDOS_SYSCALL_RMDIR, (unsigned int)dir_name, 0, 0);
}

static inline int app_delete_entry(const minidos_app_api_t* api, const char* name) {
    return app_syscall(api, MINIDOS_SYSCALL_DELETE_ENTRY, (unsigned int)name, 0, 0);
}

static inline int app_rename_entry(const minidos_app_api_t* api, const char* old_name, const char* new_name) {
    return app_syscall(api, MINIDOS_SYSCALL_RENAME_ENTRY, (unsigned int)old_name, (unsigned int)new_name, 0);
}

static inline int app_copy_to_dir(const minidos_app_api_t* api, const char* src_name, const char* dst_dir_name) {
    return app_syscall(api, MINIDOS_SYSCALL_COPY_TO_DIR, (unsigned int)src_name, (unsigned int)dst_dir_name, 0);
}

static inline int app_move_to_dir(const minidos_app_api_t* api, const char* src_name, const char* dst_dir_name) {
    return app_syscall(api, MINIDOS_SYSCALL_MOVE_TO_DIR, (unsigned int)src_name, (unsigned int)dst_dir_name, 0);
}

static inline int app_clip_set(const minidos_app_api_t* api, const char* src_name, int mode) {
    return app_syscall(api, MINIDOS_SYSCALL_CLIP_SET, (unsigned int)src_name, (unsigned int)mode, 0);
}

static inline int app_clip_paste(const minidos_app_api_t* api, const char* dst_dir_name_or_empty) {
    return app_syscall(api, MINIDOS_SYSCALL_CLIP_PASTE, (unsigned int)dst_dir_name_or_empty, 0, 0);
}

static inline int app_random(const minidos_app_api_t* api, unsigned int limit) {
    return app_syscall(api, MINIDOS_SYSCALL_RANDOM, limit, 0, 0);
}

static inline int app_file_read(const minidos_app_api_t* api, const char* name, unsigned char* buffer, int max_size) {
    return app_syscall(api, MINIDOS_SYSCALL_FILE_READ, (unsigned int)name, (unsigned int)buffer, (unsigned int)max_size);
}

static inline int app_file_write(const minidos_app_api_t* api, const char* name, const unsigned char* buffer, int size) {
    return app_syscall(api, MINIDOS_SYSCALL_FILE_WRITE, (unsigned int)name, (unsigned int)buffer, (unsigned int)size);
}

#endif
