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
    shell_apps_begin_foreground_graphics(task);
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
    shell_apps_end_foreground_graphics(task);
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
    shell_apps_end_foreground_graphics(task);
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
