#include "sysinfo.h"
#include "task/task.h"
#include "task/process.h"
#include "disk/disk.h"
#include "io/cpuid.h"
#include "io/power.h"
#include "io/tsc.h"
#include "io/rtc.h"
#include "graphics/graphics.h"
#include "memory/memory.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#define CPUID_EXTENDED_MAX 0x80000000
#define CPUID_BRAND_LAST 0x80000004
#define CPUID_BRAND_FIRST 0x80000002
#define CPUID_TOPOLOGY_LEAF 0x0B
#define CPUID_TOPOLOGY_COUNT_MASK 0xFFFF
#define CPUID_CACHE_LEAF 4
#define CPUID_THERMAL_LEAF 6
#define CPUID_THERMAL_DTS (1u << 0)
#define CPUID_HYPERVISOR (1u << 31)
// "GenuineIntel" as returned in ebx, edx, ecx
#define CPUID_VENDOR_INTEL_EBX 0x756E6547
#define CPUID_VENDOR_INTEL_EDX 0x49656E69
#define CPUID_VENDOR_INTEL_ECX 0x6C65746E
// Sandy Bridge is the first model that has the temperature target register;
// the two Atoms after it (Cedarview) do not
#define INTEL_MODEL_FIRST_WITH_TJMAX 0x2A
#define INTEL_MODEL_CEDARVIEW_A 0x35
#define INTEL_MODEL_CEDARVIEW_B 0x36
#define MSR_THERM_STATUS 0x19C
#define MSR_TEMPERATURE_TARGET 0x1A2
#define THERM_STATUS_READING_VALID (1u << 31)
#define THERM_STATUS_READOUT_SHIFT 16
#define THERM_STATUS_READOUT_MASK 0x7F
#define TEMPERATURE_TARGET_SHIFT 16
#define TEMPERATURE_TARGET_MASK 0xFF
#define TJMAX_DEFAULT 100
#define TJMAX_MIN 70
#define TJMAX_MAX 125
#define CACHE_TYPE_MASK 0x1F
#define CACHE_TYPE_INSTRUCTION 2
#define CACHE_MAX_ENTRIES 16
// The clock chip is slow to read, so reuse a reading for this long
#define RTC_CACHE_MS 250

// The brand string never changes, so read it once
static char cpu_name_cache[64];
static bool cpu_name_cached = false;

static struct rtc_time rtc_cache;
static uint64_t rtc_cache_read_ms = 0;
static bool rtc_cached = false;

// Cores and threads the chip reports, not how many the OS runs on
static void system_cpu_topology(uint32_t *cores, uint32_t *threads)
{
    uint32_t eax, ebx, ecx, edx;
    *cores = 1;
    *threads = 1;

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;
    if (max_leaf >= CPUID_TOPOLOGY_LEAF)
    {
        // Level 0 counts threads per core, level 1 logical processors per package
        cpuid(CPUID_TOPOLOGY_LEAF, 0, &eax, &ebx, &ecx, &edx);
        uint32_t per_core = ebx & CPUID_TOPOLOGY_COUNT_MASK;
        cpuid(CPUID_TOPOLOGY_LEAF, 1, &eax, &ebx, &ecx, &edx);
        uint32_t logical = ebx & CPUID_TOPOLOGY_COUNT_MASK;
        if (per_core && logical >= per_core)
        {
            *threads = logical;
            *cores = logical / per_core;
            return;
        }
    }

    // Older chips: logical processor count from leaf 1
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    uint32_t logical = (ebx >> 16) & 0xFF;
    if (logical)
    {
        *threads = logical;
        *cores = logical;
    }
}

static uint64_t msr_read(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Reading a register the chip lacks faults, and nothing here recovers from
// that, so every condition is checked first; false means "don't know".
static bool system_cpu_temperature(uint32_t *celsius)
{
    uint32_t eax, ebx, ecx, edx;

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;
    if (ebx != CPUID_VENDOR_INTEL_EBX || edx != CPUID_VENDOR_INTEL_EDX || ecx != CPUID_VENDOR_INTEL_ECX || max_leaf < CPUID_THERMAL_LEAF)
    {
        return false;
    }

    // A hypervisor decides which registers exist, and faults on the rest
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (ecx & CPUID_HYPERVISOR)
    {
        return false;
    }

    uint32_t family = (eax >> 8) & 0xF;
    uint32_t model = (eax >> 4) & 0xF;
    if (family == 6)
    {
        model |= ((eax >> 16) & 0xF) << 4;
    }
    if (family != 6 || model < INTEL_MODEL_FIRST_WITH_TJMAX || model == INTEL_MODEL_CEDARVIEW_A || model == INTEL_MODEL_CEDARVIEW_B)
    {
        return false;
    }

    cpuid(CPUID_THERMAL_LEAF, 0, &eax, &ebx, &ecx, &edx);
    if (!(eax & CPUID_THERMAL_DTS))
    {
        return false;
    }

    // The sensor reads degrees below the throttle point (TjMax)
    uint32_t tjmax = (msr_read(MSR_TEMPERATURE_TARGET) >> TEMPERATURE_TARGET_SHIFT) & TEMPERATURE_TARGET_MASK;
    if (tjmax < TJMAX_MIN || tjmax > TJMAX_MAX)
    {
        tjmax = TJMAX_DEFAULT;
    }

    uint64_t status = msr_read(MSR_THERM_STATUS);
    if (!(status & THERM_STATUS_READING_VALID))
    {
        return false;
    }

    uint32_t below = (status >> THERM_STATUS_READOUT_SHIFT) & THERM_STATUS_READOUT_MASK;
    if (below > tjmax)
    {
        return false;
    }

    *celsius = tjmax - below;
    return true;
}

// Data and unified caches from CPUID leaf 4 (Intel); all zero elsewhere
static void system_cpu_cache(uint32_t *l1_kb, uint32_t *l2_kb, uint32_t *l3_kb)
{
    uint32_t eax, ebx, ecx, edx;
    *l1_kb = 0;
    *l2_kb = 0;
    *l3_kb = 0;

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < CPUID_CACHE_LEAF)
    {
        return;
    }

    for (uint32_t i = 0; i < CACHE_MAX_ENTRIES; i++)
    {
        cpuid(CPUID_CACHE_LEAF, i, &eax, &ebx, &ecx, &edx);
        uint32_t type = eax & CACHE_TYPE_MASK;
        if (type == 0)
        {
            break;
        }
        if (type == CACHE_TYPE_INSTRUCTION)
        {
            continue;
        }

        uint64_t ways = (ebx >> 22) + 1;
        uint64_t partitions = ((ebx >> 12) & 0x3FF) + 1;
        uint64_t line_size = (ebx & 0xFFF) + 1;
        uint64_t sets = (uint64_t)ecx + 1;
        uint32_t kb = (uint32_t)((ways * partitions * line_size * sets) / 1024);

        uint32_t level = (eax >> 5) & 0x7;
        if (level == 1)
        {
            *l1_kb = kb;
        }
        else if (level == 2)
        {
            *l2_kb = kb;
        }
        else if (level == 3)
        {
            *l3_kb = kb;
        }
    }
}

static void system_cpu_name(char *out, size_t size)
{
    uint32_t eax, ebx, ecx, edx;
    char brand[49] = {0};

    cpuid(CPUID_EXTENDED_MAX, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= CPUID_BRAND_LAST)
    {
        // The brand string is 48 characters spread over three leaves
        for (int i = 0; i < 3; i++)
        {
            uint32_t regs[4];
            cpuid(CPUID_BRAND_FIRST + i, 0, &regs[0], &regs[1], &regs[2], &regs[3]);
            memcpy(brand + i * 16, regs, sizeof(regs));
        }
    }
    else
    {
        // No brand string: fall back to the vendor id
        cpuid(0, 0, &eax, &ebx, &ecx, &edx);
        memcpy(brand, &ebx, 4);
        memcpy(brand + 4, &edx, 4);
        memcpy(brand + 8, &ecx, 4);
    }

    // Some CPUs right-align the brand string
    const char *start = brand;
    while (*start == ' ')
    {
        start++;
    }

    size_t len = 0;
    while (start[len] && len < size - 1)
    {
        out[len] = start[len];
        len++;
    }
    out[len] = 0;
}

// Copies into the calling process's memory; pages need not be contiguous
static int sysinfo_copy_to_user(struct process *process, void *virt_dest, const void *src, size_t len)
{
    if (process_validate_memory_or_terminate(process, virt_dest, len) < 0)
    {
        return -EINVARG;
    }

    for (size_t i = 0; i < len; i++)
    {
        uint8_t *dest = process_virtual_address_to_physical(process, (uint8_t *)virt_dest + i);
        if (!dest)
        {
            return -EINVARG;
        }
        *dest = ((const uint8_t *)src)[i];
    }

    return 0;
}

void *isr80h_command26_system_info(struct interrupt_frame *frame)
{
    void *virt_info = task_get_stack_item(task_current(), 0);

    struct system_info info;
    memset(&info, 0, sizeof(info));
    if (!cpu_name_cached)
    {
        system_cpu_name(cpu_name_cache, sizeof(cpu_name_cache));
        cpu_name_cached = true;
    }
    memcpy(info.cpu_name, cpu_name_cache, sizeof(info.cpu_name));

    system_cpu_topology(&info.cpu_cores, &info.cpu_threads);
    info.cpu_mhz = (uint32_t)(tsc_frequency() / 1000000);
    info.disk_count = (uint32_t)disk_real_total();
    info.memory_bytes = e820_total_accessible_memory();
    info.uptime_ms = tsc_uptime_miliseconds();
    system_cpu_cache(&info.cache_l1_kb, &info.cache_l2_kb, &info.cache_l3_kb);
    info.cpu_temp_valid = system_cpu_temperature(&info.cpu_temp_c) ? 1 : 0;

    uint64_t now_ms = tsc_miliseconds();
    if (!rtc_cached || now_ms - rtc_cache_read_ms >= RTC_CACHE_MS)
    {
        rtc_read(&rtc_cache);
        rtc_cache_read_ms = now_ms;
        rtc_cached = true;
    }
    info.year = rtc_cache.year;
    info.month = rtc_cache.month;
    info.day = rtc_cache.day;
    info.hour = rtc_cache.hour;
    info.minute = rtc_cache.minute;
    info.second = rtc_cache.second;

    struct graphics_info *screen = graphics_screen_info();
    if (screen)
    {
        info.screen_width = screen->width;
        info.screen_height = screen->height;
    }

    return (void *)(long)sysinfo_copy_to_user(task_current()->process, virt_info, &info, sizeof(info));
}

void *isr80h_command27_disk_info(struct interrupt_frame *frame)
{
    int index = (int)(long)task_get_stack_item(task_current(), 0);
    void *virt_info = task_get_stack_item(task_current(), 1);

    struct disk *disk = disk_real_get(index);
    if (!disk)
    {
        return (void *)(long)-EINVARG;
    }

    struct system_disk_info info;
    memset(&info, 0, sizeof(info));
    const char *name = disk->model[0] ? disk->model : "Unknown disk";
    for (size_t i = 0; name[i] && i < sizeof(info.name) - 1; i++)
    {
        info.name[i] = name[i];
    }
    info.size_bytes = disk->size_bytes;

    return (void *)(long)sysinfo_copy_to_user(task_current()->process, virt_info, &info, sizeof(info));
}

void *isr80h_command28_power(struct interrupt_frame *frame)
{
    int action = (int)(long)task_get_stack_item(task_current(), 0);
    if (action == SYSTEM_POWER_RESTART)
    {
        system_reboot();
    }

    if (action == SYSTEM_POWER_OFF)
    {
        return (void *)(long)system_poweroff();
    }

    return (void *)(long)-EINVARG;
}
