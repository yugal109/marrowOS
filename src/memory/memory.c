#include "memory.h"
#include "config.h"

// Count of E820 entries the bootloader stored at 0x7DFE.
size_t e820_total_entries()
{
    return *((uint16_t *)MARROWOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION);
}

// Pointer to the Nth E820 entry in the table at 0x7E00. NULL if index is out of range.
struct e820_entry *e820_entry(size_t index)
{
    if (index >= e820_total_entries())
    {
        return NULL;
    }

    struct e820_entry *entries = (struct e820_entry *)MARROWOS_MEMORY_MAP_LOCATION;
    return &entries[index];
}

// Largest type=1 (usable RAM) region. Used to pick the minimal-heap home.
struct e820_entry *e820_largest_free_entry()
{
    size_t total_memory_entries = e820_total_entries();
    struct e820_entry *entires = (struct e820_entry *)MARROWOS_MEMORY_MAP_LOCATION;

    // all we want is , long contiguous memory regions
    struct e820_entry *chosen_entry = NULL;

    for (int i = 0; i < total_memory_entries; i++)
    {
        struct e820_entry *entry = &entires[i];
        if (entry->type == 1)
        {
            // usable memory
            if (chosen_entry == NULL)
            {
                chosen_entry = entry;
                continue;
            }
            if (entry->length > chosen_entry->length)
            {
                chosen_entry = entry;
            }
        }
    }

    return chosen_entry;
}

// Sum of every type=1 E820 length. Total usable RAM, not one contiguous blob.
size_t e820_total_accessible_memory()
{
    size_t total_memory_entries = e820_total_entries();
    struct e820_entry *entries = (struct e820_entry *)MARROWOS_MEMORY_MAP_LOCATION;
    size_t total_memory = 0;
    for (int i = 0; i < total_memory_entries; i++)
    {
        struct e820_entry *entry = &entries[i];
        if (entry->type == 1)
        {
            // usable memory
            total_memory += entry->length;
        }
    }
    return total_memory;
}

// Fill size bytes at ptr with byte c. Freestanding memset (no libc).
void *memset(void *ptr, int c, size_t size)
{
    char *c_ptr = (char *)ptr;
    for (int i = 0; i < size; i++)
    {
        c_ptr[i] = (char)c;
    }
    return ptr;
}

// Compare count bytes. Returns 0 if equal, <0 or >0 like libc memcmp.
int memcmp(void *s1, void *s2, int count)
{
    char *c1 = s1;
    char *c2 = s2;
    while (count-- > 0)
    {
        if (*c1++ != *c2++)
        {
            return c1[-1] < c2[-1] ? -1 : 1;
        }
    }
    return 0;
}

// Copy len bytes from src to dest. No overlap handling.
void *memcpy(void *dest, void *src, int len)
{
    char *d = dest;
    char *s = src;
    while (len--)
    {
        *d++ = *s++;
    }
    return dest;
}
