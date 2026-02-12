#ifndef RTC_H
#define RTC_H

typedef struct {
    unsigned char hours;
    unsigned char minutes;
    unsigned char seconds;
} rtc_time_t;

typedef struct {
    unsigned short year;
    unsigned char month;
    unsigned char day;
} rtc_date_t;

int rtc_read_time(rtc_time_t* time);
int rtc_set_time(const rtc_time_t* time);
int rtc_read_date(rtc_date_t* date);
int rtc_set_date(const rtc_date_t* date);

#endif
