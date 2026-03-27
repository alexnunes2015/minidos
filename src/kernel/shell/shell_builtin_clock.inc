static void shell_builtin_append_two_digits(char* out, int* pos, unsigned char value) {
    out[(*pos)++] = (char)('0' + (value / 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

static void shell_builtin_append_four_digits(char* out, int* pos, unsigned short value) {
    out[(*pos)++] = (char)('0' + ((value / 1000) % 10));
    out[(*pos)++] = (char)('0' + ((value / 100) % 10));
    out[(*pos)++] = (char)('0' + ((value / 10) % 10));
    out[(*pos)++] = (char)('0' + (value % 10));
}

static void shell_builtin_format_time_string(const rtc_time_t* time, char* out) {
    int pos = 0;
    shell_builtin_append_two_digits(out, &pos, time->hours);
    out[pos++] = ':';
    shell_builtin_append_two_digits(out, &pos, time->minutes);
    out[pos++] = ':';
    shell_builtin_append_two_digits(out, &pos, time->seconds);
    out[pos] = '\0';
}

static void shell_builtin_format_date_string(const rtc_date_t* date, char* out) {
    int pos = 0;
    shell_builtin_append_two_digits(out, &pos, date->day);
    out[pos++] = '/';
    shell_builtin_append_two_digits(out, &pos, date->month);
    out[pos++] = '/';
    shell_builtin_append_four_digits(out, &pos, date->year);
    out[pos] = '\0';
}

static int shell_builtin_parse_time_arg(const char* args, rtc_time_t* time) {
    int values[3] = {0, 0, 0};
    int value_count = 0;
    int current_value = 0;
    int digits = 0;
    int i = 0;

    if (!args || !time) {
        return 0;
    }

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }

    for (; args[i] != '\0'; i++) {
        char c = args[i];
        if (c >= '0' && c <= '9') {
            current_value = current_value * 10 + (c - '0');
            digits++;
            if (digits > 2) {
                return 0;
            }
        } else if (c == ':') {
            if (digits == 0 || value_count >= 2) {
                return 0;
            }
            values[value_count++] = current_value;
            current_value = 0;
            digits = 0;
        } else if (c == ' ' || c == '\t') {
            while (args[i] == ' ' || args[i] == '\t') {
                i++;
            }
            if (args[i] != '\0') {
                return 0;
            }
            break;
        } else {
            return 0;
        }
    }

    if (digits == 0) {
        return 0;
    }
    values[value_count++] = current_value;

    if (value_count != 2 && value_count != 3) {
        return 0;
    }
    if (values[0] > 23 || values[1] > 59) {
        return 0;
    }

    time->hours = (unsigned char)values[0];
    time->minutes = (unsigned char)values[1];
    time->seconds = (unsigned char)((value_count == 3) ? values[2] : 0);
    return time->seconds <= 59;
}

static int shell_builtin_parse_date_arg(const char* args, rtc_date_t* date) {
    int values[3] = {0, 0, 0};
    int digit_counts[3] = {0, 0, 0};
    int value_index = 0;
    int i = 0;
    unsigned short year;

    if (!args || !date) {
        return 0;
    }

    while (args[i] == ' ' || args[i] == '\t') {
        i++;
    }

    for (; args[i] != '\0'; i++) {
        char c = args[i];
        if (c >= '0' && c <= '9') {
            if (value_index > 2) {
                return 0;
            }
            values[value_index] = values[value_index] * 10 + (c - '0');
            digit_counts[value_index]++;
            if (value_index < 2 && digit_counts[value_index] > 2) {
                return 0;
            }
            if (value_index == 2 && digit_counts[value_index] > 4) {
                return 0;
            }
        } else if (c == '/' || c == '-') {
            if (digit_counts[value_index] == 0 || value_index >= 2) {
                return 0;
            }
            value_index++;
        } else if (c == ' ' || c == '\t') {
            while (args[i] == ' ' || args[i] == '\t') {
                i++;
            }
            if (args[i] != '\0') {
                return 0;
            }
            break;
        } else {
            return 0;
        }
    }

    if (value_index != 2 || digit_counts[2] == 0) {
        return 0;
    }

    if (digit_counts[2] == 2) {
        if (values[2] >= 80) {
            year = (unsigned short)(1900 + values[2]);
        } else {
            year = (unsigned short)(2000 + values[2]);
        }
    } else if (digit_counts[2] == 4) {
        year = (unsigned short)values[2];
    } else {
        return 0;
    }

    if (year < 1980 || year > 2099) {
        return 0;
    }
    if (values[1] < 1 || values[1] > 12) {
        return 0;
    }
    if (values[0] < 1 || values[0] > 31) {
        return 0;
    }

    date->day = (unsigned char)values[0];
    date->month = (unsigned char)values[1];
    date->year = year;
    return 1;
}

static int shell_builtin_cmd_time(const shell_builtin_host_t* host, const char* args) {
    rtc_time_t current_time;

    if (!rtc_read_time(&current_time)) {
        host->out_both("Unable to read RTC time\n");
        return 1;
    }

    if (args[0] == '\0') {
        char time_str[9];
        shell_builtin_format_time_string(&current_time, time_str);
        host->out_both("Current time is: ");
        host->out_both(time_str);
        host->out_both("\n");
        host->out_screen("To set a new time, use: time HH:MM[:SS]\n");
        return 1;
    }

    {
        rtc_time_t new_time;
        char time_str[9];

        if (!shell_builtin_parse_time_arg(args, &new_time)) {
            host->out_both("Invalid time format. Use HH:MM or HH:MM:SS\n");
            return 1;
        }

        if (!rtc_set_time(&new_time)) {
            host->out_both("Failed to set RTC time\n");
            return 1;
        }

        if (!rtc_read_time(&current_time)) {
            host->out_both("Time updated, but verification read failed\n");
            return 1;
        }

        shell_builtin_format_time_string(&current_time, time_str);
        host->out_both("Time updated to: ");
        host->out_both(time_str);
        host->out_both("\n");
    }

    return 1;
}

static int shell_builtin_cmd_date(const shell_builtin_host_t* host, const char* args) {
    rtc_date_t current_date;

    if (!rtc_read_date(&current_date)) {
        host->out_both("Unable to read RTC date\n");
        return 1;
    }

    if (args[0] == '\0') {
        char date_str[11];
        shell_builtin_format_date_string(&current_date, date_str);
        host->out_both("Current date is: ");
        host->out_both(date_str);
        host->out_both("\n");
        host->out_screen("To set a new date, use: date DD/MM/YYYY\n");
        return 1;
    }

    {
        rtc_date_t new_date;
        char date_str[11];

        if (!shell_builtin_parse_date_arg(args, &new_date)) {
            host->out_both("Invalid date format. Use DD/MM/YYYY\n");
            return 1;
        }

        if (!rtc_set_date(&new_date)) {
            host->out_both("Failed to set RTC date\n");
            return 1;
        }

        if (!rtc_read_date(&current_date)) {
            host->out_both("Date updated, but verification read failed\n");
            return 1;
        }

        shell_builtin_format_date_string(&current_date, date_str);
        host->out_both("Date updated to: ");
        host->out_both(date_str);
        host->out_both("\n");
    }

    return 1;
}
