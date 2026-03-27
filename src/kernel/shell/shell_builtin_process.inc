static const unsigned int TOP_DEFAULT_INTERVAL_MS = 200U;
static const unsigned int TOP_DEFAULT_SAMPLES = 1U;

#define PROC_COL_PID 4
#define PROC_COL_NAME 12
#define PROC_COL_STATE 10
#define PROC_COL_MEM 8
#define PROC_COL_CPU 6
#define PROC_COL_EXE 14

static void shell_builtin_format_memory(unsigned int kb, char* out) {
    const char* unit = "KB";
    unsigned int unit_kb = 1;
    int pos = 0;

    if (kb >= 1024U * 1024U) {
        unit = "GB";
        unit_kb = 1024U * 1024U;
    } else if (kb >= 1024U) {
        unit = "MB";
        unit_kb = 1024U;
    }

    if (unit_kb == 1) {
        pos += shell_builtin_uint_to_dec(kb, out + pos);
    } else {
        unsigned int tenths = (kb * 10 + unit_kb / 2) / unit_kb;
        unsigned int whole = tenths / 10;
        unsigned int frac = tenths % 10;

        pos += shell_builtin_uint_to_dec(whole, out + pos);
        out[pos++] = '.';
        out[pos++] = (char)('0' + frac);
    }

    out[pos++] = ' ';
    out[pos++] = unit[0];
    out[pos++] = unit[1];
    out[pos] = '\0';
}

static void shell_builtin_format_percent_tenths(unsigned int value_tenths, char* out) {
    unsigned int whole = value_tenths / 10U;
    unsigned int frac = value_tenths % 10U;
    int pos = 0;

    pos += shell_builtin_uint_to_dec(whole, out + pos);
    out[pos++] = '.';
    out[pos++] = (char)('0' + frac);
    out[pos++] = '%';
    out[pos] = '\0';
}

static void shell_builtin_format_stack_reserve(const scheduler_process_snapshot_t* snap, char* out, int out_size) {
    if (!snap || !out || out_size <= 0) {
        return;
    }

    if (snap->kernel_stack_base == 0 || snap->kernel_stack_top == 0) {
        shell_builtin_copy_string("-", out, out_size);
        return;
    }

    shell_builtin_format_memory(scheduler_get_kernel_stack_bytes() / 1024U, out);
}

static void shell_builtin_format_origin(const scheduler_process_snapshot_t* snap, char* out, int out_size) {
    if (!snap || !out || out_size <= 0) {
        return;
    }

    if (!snap->origin_is_executable || !snap->origin_name || snap->origin_name[0] == '\0') {
        shell_builtin_copy_string("-", out, out_size);
        return;
    }

    shell_builtin_copy_string(snap->origin_name, out, out_size);
}

static void shell_builtin_append_cell(char* line, int* pos, int line_size, const char* text, int width, int right_align) {
    int len;
    int visible;

    if (!line || !pos || line_size <= 0 || width <= 0) {
        return;
    }

    if (!text) {
        text = "-";
    }

    len = shell_builtin_string_length(text);
    visible = (len < width) ? len : width;

    if (right_align) {
        while (visible < width && *pos < line_size - 1) {
            line[(*pos)++] = ' ';
            visible++;
        }
        visible = (len < width) ? len : width;
    }

    for (int i = 0; i < visible && *pos < line_size - 1; i++) {
        line[(*pos)++] = text[i];
    }

    if (!right_align) {
        for (int i = visible; i < width && *pos < line_size - 1; i++) {
            line[(*pos)++] = ' ';
        }
    }

    if (*pos < line_size - 1) {
        line[(*pos)++] = ' ';
    }
}

static void shell_builtin_print_process_table_header(const shell_builtin_host_t* host) {
    char line[96];
    int pos = 0;

    if (!host) {
        return;
    }

    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "PID", PROC_COL_PID, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "NAME", PROC_COL_NAME, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "STATE", PROC_COL_STATE, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "MEM", PROC_COL_MEM, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "CPU", PROC_COL_CPU, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), "EXE", PROC_COL_EXE, 0);
    if (pos > 0) {
        pos--;
    }
    line[pos++] = '\n';
    line[pos] = '\0';
    host->out_both(line);
}

static void shell_builtin_print_process_row(const shell_builtin_host_t* host, const scheduler_process_snapshot_t* snap, unsigned int cpu_tenths) {
    char line[128];
    char pid_str[16];
    char cpu_str[16];
    char stack_str[24];
    char origin_str[24];
    int pos = 0;

    if (!host || !snap) {
        return;
    }

    {
        int len = shell_builtin_uint_to_dec((unsigned int)snap->pid, pid_str);
        pid_str[len] = '\0';
    }
    shell_builtin_format_percent_tenths(cpu_tenths, cpu_str);
    shell_builtin_format_stack_reserve(snap, stack_str, (int)sizeof(stack_str));
    shell_builtin_format_origin(snap, origin_str, (int)sizeof(origin_str));

    shell_builtin_append_cell(line, &pos, (int)sizeof(line), pid_str, PROC_COL_PID, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), snap->name ? snap->name : "unknown", PROC_COL_NAME, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), process_state_name(snap->state), PROC_COL_STATE, 0);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), stack_str, PROC_COL_MEM, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), cpu_str, PROC_COL_CPU, 1);
    shell_builtin_append_cell(line, &pos, (int)sizeof(line), origin_str, PROC_COL_EXE, 0);
    if (pos > 0) {
        pos--;
    }
    line[pos++] = '\n';
    line[pos] = '\0';
    host->out_both(line);
}

static int shell_builtin_parse_top_args(const char* args, unsigned int* interval_ms, unsigned int* samples) {
    int i = 0;
    int field = 0;
    char token[32];

    if (!args || !interval_ms || !samples) {
        return 0;
    }

    *interval_ms = TOP_DEFAULT_INTERVAL_MS;
    *samples = TOP_DEFAULT_SAMPLES;

    while (args[i] != '\0') {
        int j = 0;
        unsigned int value = 0;

        while (args[i] == ' ' || args[i] == '\t') {
            i++;
        }
        if (args[i] == '\0') {
            break;
        }
        if (field >= 2) {
            return 0;
        }

        while (args[i] != '\0' && args[i] != ' ' && args[i] != '\t') {
            if (j >= (int)sizeof(token) - 1) {
                return 0;
            }
            token[j++] = args[i++];
        }
        token[j] = '\0';

        if (!shell_builtin_parse_uint_arg(token, &value)) {
            return 0;
        }

        if (field == 0) {
            *interval_ms = value;
        } else {
            *samples = value;
        }
        field++;
    }

    if (*interval_ms == 0) {
        *interval_ms = 1U;
    }
    if (*samples == 0) {
        *samples = 1U;
    }

    return 1;
}

static int shell_builtin_cmd_mem(const shell_builtin_host_t* host) {
    char mem_str[24];

    shell_builtin_format_memory(g_memory_kb, mem_str);
    host->out_both("System Memory: ");
    host->out_both(mem_str);
    host->out_both("\n");
    return 1;
}

static int shell_builtin_cmd_ps(const shell_builtin_host_t* host, const char* args) {
    scheduler_process_snapshot_t snaps[16];
    int count;
    unsigned int total_ticks = 0;

    if (args[0] != '\0') {
        host->out_both("Usage: ps\n");
        return 1;
    }

    count = scheduler_snapshot_processes(snaps, (int)(sizeof(snaps) / sizeof(snaps[0])), 0);

    if (count <= 0) {
        host->out_both("no scheduler-visible tasks\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        total_ticks += snaps[i].runtime_ticks;
    }

    shell_builtin_print_process_table_header(host);
    for (int i = 0; i < count; i++) {
        unsigned int cpu_tenths = 0;

        if (total_ticks > 0) {
            cpu_tenths = (snaps[i].runtime_ticks * 1000U + (total_ticks / 2U)) / total_ticks;
        }
        shell_builtin_print_process_row(host, &snaps[i], cpu_tenths);
    }

    return 1;
}

static int shell_builtin_cmd_top(const shell_builtin_host_t* host, const char* args) {
    scheduler_process_snapshot_t before[16];
    scheduler_process_snapshot_t after[16];
    unsigned int before_ticks[16];
    unsigned int interval_ms;
    unsigned int samples;
    int count;

    if (!shell_builtin_parse_top_args(args, &interval_ms, &samples)) {
        host->out_both("Usage: top [interval_ms] [samples]\n");
        return 1;
    }

    for (unsigned int sample = 0; sample < samples; sample++) {
        unsigned int sleep_ticks;
        unsigned int total_delta = 0;

        count = scheduler_snapshot_processes(before, (int)(sizeof(before) / sizeof(before[0])), 0);
        for (int i = 0; i < count; i++) {
            before_ticks[i] = before[i].runtime_ticks;
        }

        sleep_ticks = timer_ms_to_ticks_ceil(interval_ms);
        if (sleep_ticks == 0) {
            sleep_ticks = 1;
        }
        scheduler_sleep_ticks(sleep_ticks);

        count = scheduler_snapshot_processes(after, (int)(sizeof(after) / sizeof(after[0])), 0);
        for (int i = 0; i < count; i++) {
            unsigned int delta = after[i].runtime_ticks - before_ticks[i];
            total_delta += delta;
        }

        if (sample > 0U) {
            host->out_both("\n");
        }

        shell_builtin_print_process_table_header(host);
        for (int i = 0; i < count; i++) {
            unsigned int delta = after[i].runtime_ticks - before_ticks[i];
            unsigned int cpu_tenths = 0;

            if (total_delta > 0) {
                cpu_tenths = (delta * 1000U + (total_delta / 2U)) / total_delta;
            }
            shell_builtin_print_process_row(host, &after[i], cpu_tenths);
        }
    }

    return 1;
}
