#ifndef IO_RTC_H
#define IO_RTC_H

#include <stdint.h>

struct rtc_time
{
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
};

// Reads the battery-backed clock, as set in the BIOS (no timezone)
void rtc_read(struct rtc_time *out);

#endif
