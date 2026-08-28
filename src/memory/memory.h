#ifndef MEMORY_H
#define MEMORY_H
#include <stddef.h>
#include <stdint.h>

struct e820_entry
{
    uint64_t base_addr;     // Starting address of the memory region
    uint64_t length;        // Size of the region in bytes
    uint32_t type;          // Type of the memory region (1 = usable, others=reserved, ect...)
    uint32_t extended_attr; // Extended attributes (often zero)
} __attribute__((packed));

void *memset(void *ptr, int c, size_t size);
int memcmp(void *s1, void *s2, int count);
void *memcpy(void *dest, void *src, int len);

// Largest usable (type=1) E820 region — home of the minimal heap.
struct e820_entry *e820_largest_free_entry();
// Sum of all usable E820 lengths.
size_t e820_total_accessible_memory();
// Entry count stored at 0x7DFE by the bootloader.
size_t e820_total_entries();
// The Nth E820 entry at 0x7E00.
struct e820_entry *e820_entry(size_t index);

#endif
