#include "rtc.h"

#define CMOS_ADDR_PORT 0x70
#define CMOS_DATA_PORT 0x71

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static unsigned char rtc_read_reg(unsigned char reg) {
    outb(CMOS_ADDR_PORT, (unsigned char)(0x80 | reg));
    return inb(CMOS_DATA_PORT);
}

static void rtc_write_reg(unsigned char reg, unsigned char value) {
    outb(CMOS_ADDR_PORT, (unsigned char)(0x80 | reg));
    outb(CMOS_DATA_PORT, value);
}

static int rtc_is_updating(void) {
    return (rtc_read_reg(0x0A) & 0x80) != 0;
}

static unsigned char bcd_to_bin(unsigned char bcd) {
    return (unsigned char)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

static unsigned char bin_to_bcd(unsigned char bin) {
    return (unsigned char)(((bin / 10) << 4) | (bin % 10));
}

static int is_leap_year(unsigned short year) {
    if ((year % 400) == 0) {
        return 1;
    }
    if ((year % 100) == 0) {
        return 0;
    }
    return (year % 4) == 0;
}

static int days_in_month(unsigned short year, unsigned char month) {
    static const unsigned char month_days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return month_days[month - 1];
}

int rtc_read_time(rtc_time_t* time) {
    if (!time) {
        return 0;
    }

    unsigned char sec1, min1, hour1;
    unsigned char sec2, min2, hour2;
    unsigned char status_b;
    int safety = 100000;

    do {
        while (rtc_is_updating() && --safety > 0) { }
        if (safety <= 0) {
            return 0;
        }

        sec1 = rtc_read_reg(0x00);
        min1 = rtc_read_reg(0x02);
        hour1 = rtc_read_reg(0x04);
        status_b = rtc_read_reg(0x0B);

        while (rtc_is_updating() && --safety > 0) { }
        if (safety <= 0) {
            return 0;
        }

        sec2 = rtc_read_reg(0x00);
        min2 = rtc_read_reg(0x02);
        hour2 = rtc_read_reg(0x04);
    } while ((sec1 != sec2 || min1 != min2 || hour1 != hour2) && safety > 0);

    if (safety <= 0) {
        return 0;
    }

    unsigned char seconds = sec2;
    unsigned char minutes = min2;
    unsigned char hours = hour2;

    if ((status_b & 0x04) == 0) {
        seconds = bcd_to_bin(seconds);
        minutes = bcd_to_bin(minutes);
        hours = (unsigned char)(bcd_to_bin((unsigned char)(hours & 0x7F)) | (hours & 0x80));
    }

    if ((status_b & 0x02) == 0) {
        int pm = (hours & 0x80) != 0;
        hours &= 0x7F;
        if (pm && hours < 12) {
            hours = (unsigned char)(hours + 12);
        } else if (!pm && hours == 12) {
            hours = 0;
        }
    } else {
        hours &= 0x7F;
    }

    if (hours > 23 || minutes > 59 || seconds > 59) {
        return 0;
    }

    time->hours = hours;
    time->minutes = minutes;
    time->seconds = seconds;
    return 1;
}

int rtc_set_time(const rtc_time_t* time) {
    if (!time) {
        return 0;
    }
    if (time->hours > 23 || time->minutes > 59 || time->seconds > 59) {
        return 0;
    }

    unsigned char status_b = rtc_read_reg(0x0B);
    unsigned char new_hours = time->hours;
    unsigned char new_minutes = time->minutes;
    unsigned char new_seconds = time->seconds;

    if ((status_b & 0x02) == 0) {
        int pm = new_hours >= 12;
        unsigned char hour12 = (unsigned char)(new_hours % 12);
        if (hour12 == 0) {
            hour12 = 12;
        }
        new_hours = hour12;
        if (pm) {
            new_hours |= 0x80;
        }
    }

    if ((status_b & 0x04) == 0) {
        new_seconds = bin_to_bcd(new_seconds);
        new_minutes = bin_to_bcd(new_minutes);
        new_hours = (unsigned char)(bin_to_bcd((unsigned char)(new_hours & 0x7F)) | (new_hours & 0x80));
    }

    rtc_write_reg(0x0B, (unsigned char)(status_b | 0x80));
    rtc_write_reg(0x00, new_seconds);
    rtc_write_reg(0x02, new_minutes);
    rtc_write_reg(0x04, new_hours);
    rtc_write_reg(0x0B, status_b);

    return 1;
}

int rtc_read_date(rtc_date_t* date) {
    if (!date) {
        return 0;
    }

    unsigned char day1, month1, year1, century1;
    unsigned char day2, month2, year2, century2;
    unsigned char status_b;
    int safety = 100000;

    do {
        while (rtc_is_updating() && --safety > 0) { }
        if (safety <= 0) {
            return 0;
        }

        day1 = rtc_read_reg(0x07);
        month1 = rtc_read_reg(0x08);
        year1 = rtc_read_reg(0x09);
        century1 = rtc_read_reg(0x32);
        status_b = rtc_read_reg(0x0B);

        while (rtc_is_updating() && --safety > 0) { }
        if (safety <= 0) {
            return 0;
        }

        day2 = rtc_read_reg(0x07);
        month2 = rtc_read_reg(0x08);
        year2 = rtc_read_reg(0x09);
        century2 = rtc_read_reg(0x32);
    } while ((day1 != day2 || month1 != month2 || year1 != year2 || century1 != century2) && safety > 0);

    if (safety <= 0) {
        return 0;
    }

    unsigned char day = day2;
    unsigned char month = month2;
    unsigned char year = year2;
    unsigned char century = century2;

    if ((status_b & 0x04) == 0) {
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
        century = bcd_to_bin(century);
    }

    unsigned short full_year;
    if (century >= 19 && century <= 99) {
        full_year = (unsigned short)(century * 100 + year);
    } else {
        full_year = (unsigned short)(2000 + year);
    }

    if (month < 1 || month > 12) {
        return 0;
    }
    if (day < 1 || day > days_in_month(full_year, month)) {
        return 0;
    }

    date->year = full_year;
    date->month = month;
    date->day = day;
    return 1;
}

int rtc_set_date(const rtc_date_t* date) {
    if (!date) {
        return 0;
    }
    if (date->year < 1980 || date->year > 2099) {
        return 0;
    }
    if (date->month < 1 || date->month > 12) {
        return 0;
    }
    if (date->day < 1 || date->day > days_in_month(date->year, date->month)) {
        return 0;
    }

    unsigned char status_b = rtc_read_reg(0x0B);
    unsigned char year = (unsigned char)(date->year % 100);
    unsigned char century = (unsigned char)(date->year / 100);
    unsigned char month = date->month;
    unsigned char day = date->day;

    if ((status_b & 0x04) == 0) {
        year = bin_to_bcd(year);
        century = bin_to_bcd(century);
        month = bin_to_bcd(month);
        day = bin_to_bcd(day);
    }

    rtc_write_reg(0x0B, (unsigned char)(status_b | 0x80));
    rtc_write_reg(0x07, day);
    rtc_write_reg(0x08, month);
    rtc_write_reg(0x09, year);
    rtc_write_reg(0x32, century);
    rtc_write_reg(0x0B, status_b);

    return 1;
}
