#include "rtc.h"
#include "io/io.h"
#include <stdbool.h>

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT 0x71
#define CMOS_REG_SECONDS 0x00
#define CMOS_REG_MINUTES 0x02
#define CMOS_REG_HOURS 0x04
#define CMOS_REG_DAY 0x07
#define CMOS_REG_MONTH 0x08
#define CMOS_REG_YEAR 0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B
#define CMOS_STATUS_A_UPDATING 0x80
#define CMOS_STATUS_B_24_HOUR 0x02
#define CMOS_STATUS_B_BINARY 0x04
#define CMOS_HOURS_PM 0x80
#define RTC_FIELDS 6
#define RTC_READ_ATTEMPTS 5

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_INDEX_PORT, reg);
    return insb(CMOS_DATA_PORT);
}

static uint8_t bcd_to_binary(uint8_t value)
{
    return (value & 0x0F) + (value >> 4) * 10;
}

static void rtc_read_raw(uint8_t raw[RTC_FIELDS])
{
    // The clock is inconsistent while it rolls over to the next second
    for (int i = 0; i < 1000000 && (cmos_read(CMOS_REG_STATUS_A) & CMOS_STATUS_A_UPDATING); i++)
    {
        // wait
    }

    raw[0] = cmos_read(CMOS_REG_SECONDS);
    raw[1] = cmos_read(CMOS_REG_MINUTES);
    raw[2] = cmos_read(CMOS_REG_HOURS);
    raw[3] = cmos_read(CMOS_REG_DAY);
    raw[4] = cmos_read(CMOS_REG_MONTH);
    raw[5] = cmos_read(CMOS_REG_YEAR);
}

static bool rtc_raw_equal(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < RTC_FIELDS; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

void rtc_read(struct rtc_time *out)
{
    // Read until two passes agree, so a rollover between reads can't slip through
    uint8_t raw[RTC_FIELDS], again[RTC_FIELDS];
    rtc_read_raw(raw);
    for (int i = 0; i < RTC_READ_ATTEMPTS; i++)
    {
        rtc_read_raw(again);
        if (rtc_raw_equal(raw, again))
        {
            break;
        }
        for (int f = 0; f < RTC_FIELDS; f++)
        {
            raw[f] = again[f];
        }
    }

    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    bool pm = (raw[2] & CMOS_HOURS_PM) != 0;
    raw[2] &= ~CMOS_HOURS_PM;

    if (!(status_b & CMOS_STATUS_B_BINARY))
    {
        for (int f = 0; f < RTC_FIELDS; f++)
        {
            raw[f] = bcd_to_binary(raw[f]);
        }
    }

    uint32_t hour = raw[2];
    if (!(status_b & CMOS_STATUS_B_24_HOUR))
    {
        hour = (hour % 12) + (pm ? 12 : 0);
    }

    out->second = raw[0];
    out->minute = raw[1];
    out->hour = hour;
    out->day = raw[3];
    out->month = raw[4];
    // Two-digit year; the century register is unreliable
    out->year = 2000 + raw[5];
}
