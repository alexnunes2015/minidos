#include "minidos_app.h"

#define BADPTR_ADDR 0x00010000U

static void expect_result(
    const minidos_app_api_t* api,
    const char* marker,
    int actual,
    int expected,
    int* failures
) {
    if (actual == expected) {
        app_puts(api, marker);
        return;
    }

    *failures += 1;
    app_puts(api, "SYSC900\n");
}

int app_main(const minidos_app_api_t* api) {
    unsigned char buffer[8] = {0};
    int out_value = 0;
    int failures = 0;
    int rect_storage[1] = {0};
    app_gfx_rect_batch_t batch = {
        (const app_gfx_rect_t*)rect_storage,
        APP_GFX_RECT_BATCH_MAX + 1U,
    };
    app_gfx_text_clipped_t clipped = {
        0, 0, "x", 0, 0, 0, 0, 1, 1, 2U
    };
    app_gfx_surface_blit_t surface = {
        (const unsigned char*)0x01000000U,
        1, 1, 4, 99,
        0, 0, -1, -1, -1, -1, 0, 0
    };

    app_puts(api, "SYSC100\n");

    expect_result(api, "SYSC101\n", app_syscall(api, 0xFFFFFFFFU, 0, 0, 0), -1, &failures);
    expect_result(api, "SYSC102\n", app_syscall(api, MINIDOS_SYSCALL_PUTS, BADPTR_ADDR, 0, 0), -1, &failures);
    expect_result(api, "SYSC103\n", app_file_size(api, (const char*)BADPTR_ADDR), -1, &failures);
    expect_result(api, "SYSC104\n", app_file_read(api, (const char*)BADPTR_ADDR, buffer, (int)sizeof(buffer)), -1, &failures);
    expect_result(api, "SYSC105\n", app_file_write(api, (const char*)BADPTR_ADDR, buffer, 1), 0, &failures);
    expect_result(api, "SYSC106\n", app_list_entry(api, 0, (char*)BADPTR_ADDR, &out_value), 0, &failures);
    expect_result(api, "SYSC107\n", app_mouse_state(api, (app_mouse_state_t*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC108\n", app_get_time(api, (app_time_t*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC109\n", app_thread_create(api, (const app_thread_create_t*)BADPTR_ADDR), -1, &failures);
    expect_result(api, "SYSC110\n", app_drive_valid(api, 127U), 0, &failures);
    expect_result(api, "SYSC111\n", app_copy_file(api, (const char*)BADPTR_ADDR, "X"), 0, &failures);
    expect_result(api, "SYSC112\n", app_move_file(api, (const char*)BADPTR_ADDR, "X"), 0, &failures);
    expect_result(api, "SYSC113\n", app_chdir(api, (const char*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC114\n", app_mkdir(api, (const char*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC115\n", app_rmdir(api, (const char*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC116\n", app_delete_entry(api, (const char*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC117\n", app_rename_entry(api, (const char*)BADPTR_ADDR, "X"), 0, &failures);
    expect_result(api, "SYSC118\n", app_copy_to_dir(api, (const char*)BADPTR_ADDR, "X"), 0, &failures);
    expect_result(api, "SYSC119\n", app_move_to_dir(api, (const char*)BADPTR_ADDR, "X"), 0, &failures);
    expect_result(api, "SYSC120\n", app_clip_set(api, (const char*)BADPTR_ADDR, 1), 0, &failures);
    expect_result(api, "SYSC121\n", app_gfx_rect(api, (const app_gfx_rect_t*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC122\n", app_gfx_rect_batch(api, &batch), 0, &failures);
    expect_result(api, "SYSC123\n", app_gfx_text(api, (const app_gfx_text_t*)BADPTR_ADDR), 0, &failures);
    expect_result(api, "SYSC124\n", app_gfx_text_clipped(api, &clipped), 0, &failures);
    expect_result(api, "SYSC125\n", app_gfx_size(api, (int*)BADPTR_ADDR, &out_value), 0, &failures);
    expect_result(api, "SYSC126\n", app_gfx_surface_blit(api, &surface), 0, &failures);

    if (failures != 0) {
        return 1;
    }

    app_puts(api, "SYSC190\n");
    return 0;
}
