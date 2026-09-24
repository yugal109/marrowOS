#ifndef ISR80H_SYSINFO_H
#define ISR80H_SYSINFO_H

#include <stdint.h>

// Mirrored by the userland copy in stdlib's marrowos.h
struct system_info
{
    char cpu_name[64];
    uint64_t memory_bytes;
    uint64_t uptime_ms;
    uint32_t disk_count;
    uint32_t cpu_mhz;
    uint32_t cpu_cores;
    uint32_t cpu_threads;
    uint32_t screen_width;
    uint32_t screen_height;
    // Per core for L1 and L2, shared for L3; 0 if unknown
    uint32_t cache_l1_kb;
    uint32_t cache_l2_kb;
    uint32_t cache_l3_kb;
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    // Core temperature; valid only where the chip and platform allow reading it
    uint32_t cpu_temp_valid;
    uint32_t cpu_temp_c;
};

struct system_disk_info
{
    char name[48];
    uint64_t size_bytes;
};

enum
{
    SYSTEM_POWER_RESTART,
    SYSTEM_POWER_OFF
};

struct interrupt_frame;
void *isr80h_command26_system_info(struct interrupt_frame *frame);
void *isr80h_command27_disk_info(struct interrupt_frame *frame);
void *isr80h_command28_power(struct interrupt_frame *frame);

#endif
