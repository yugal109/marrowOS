#include "tsc.h"
#include "cpuid.h"
#include "io/io.h"

uint64_t tsc_freq_val = 0;

#define PIT_FREQUENCY_HZ 1193182
#define PIT_CALIBRATION_MS 10

// PIT channel 2 in mode 0: its output bit (port 0x61 bit 5) goes high once
// the count reaches zero, giving a fixed wall-clock window to count TSC ticks in.
static uint64_t tsc_calibrate_with_pit(void)
{
    uint16_t count = (PIT_FREQUENCY_HZ * PIT_CALIBRATION_MS) / 1000;

    uint8_t port61 = insb(0x61);
    outb(0x61, (port61 & ~0x02) | 0x01); // gate channel 2 on, speaker off

    outb(0x43, 0xB0); // channel 2, lo/hi byte, mode 0, binary
    outb(0x42, count & 0xFF);
    outb(0x42, count >> 8);

    TIME_TSC start = read_tsc();
    while (!(insb(0x61) & 0x20))
    {
    }
    TIME_TSC end = read_tsc();

    outb(0x61, port61);
    return (end - start) * (1000 / PIT_CALIBRATION_MS);
}
TIME_TSC tsc_frequency(void)
{
    if (tsc_freq_val != 0)
    {
        return tsc_freq_val;
    }

    uint32_t eax, ebx, ecx, edx;
    uint64_t tsc_freq = 0;

    // Leaves above the CPU's max basic leaf aren't zero on Intel — they
    // return the highest supported leaf's data instead, i.e. garbage here.
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;

    // 0x15: crystal Hz (ecx) * ratio ebx/eax, already in Hz
    if (max_leaf >= 0x15)
    {
        cpuid(0x15, 0, &eax, &ebx, &ecx, &edx);
        if (eax != 0 && ebx != 0 && ecx != 0)
        {
            tsc_freq = ((uint64_t)ecx * (uint64_t)ebx) / (uint64_t)eax;
        }
    }

    // 0x16: base frequency in MHz
    if (tsc_freq == 0 && max_leaf >= 0x16)
    {
        cpuid(0x16, 0, &eax, &ebx, &ecx, &edx);
        if (eax != 0)
        {
            tsc_freq = (uint64_t)eax * 1000000ULL;
        }
    }

    // Older CPUs (pre-Skylake) have neither: measure it against PIT channel 2
    if (tsc_freq == 0)
    {
        tsc_freq = tsc_calibrate_with_pit();
    }

    tsc_freq_val = tsc_freq;

    return (TIME_TSC)tsc_freq;
}

TIME_TSC read_tsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi)::"memory");
    return ((TIME_TSC)hi << 32) | lo;
}

TIME_MILISECONDS tsc_miliseconds()
{
    TIME_MICROSECONDS microseconds = tsc_microseconds();
    return microseconds / 1000;
}

TIME_SECONDS tsc_seconds()
{
    TIME_MILISECONDS miliseconds = tsc_miliseconds();
    return miliseconds / 1000;
}

void udelay(TIME_MICROSECONDS microseconds)
{
    TIME_TSC tsc_freq = tsc_frequency();
    TIME_TSC start = read_tsc();
    TIME_TSC cycles_to_wait = (microseconds * tsc_freq) / 1000000;
    while ((read_tsc() - start) < cycles_to_wait)
    {
        __asm__ volatile("pause");
    }
}
