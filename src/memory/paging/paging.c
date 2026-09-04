#include "paging.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "memory/heap/heap.h"
#include "status.h"
#include "kernel.h"

// it’s the kernel’s “which map is live right now” pointer
static struct paging_desc *current_paging_desc = 0;

// True if this 8-byte paging entry is all zeros (no table / no mapping yet).
static bool paging_null_entry(struct paging_desc_entry *entry)
{
    struct paging_desc_entry null_desc = {0};
    return memcmp(entry, &null_desc, sizeof(struct paging_desc_entry)) == 0;
}

// Zeroed 512-entry PML4 (or any table-sized blob). Lower tables are born in paging_map.
struct paging_pml_entries *paging_pml4_entries_new()
{
    struct paging_pml_entries *entries_desc = kzalloc(sizeof(struct paging_pml_entries));
    return entries_desc;
}

void paging_desc_entry_free(struct paging_desc_entry *table_entry, paging_map_level_t level)
{
    if (paging_null_entry(table_entry))
    {
        return;
    }
    if (level == 0)
    {
        panic("The level must be a page table entry\n");
    }

    if (level > PAGING_MAP_LEVEL_4)
    {
        panic("Level five and higher is not supported\n");
    }

    if (level > 1)
    {
        // Loop through the child tables
        for (int i = 0; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++)
        {
            struct paging_desc_entry *entry = &table_entry[i];
            if (!paging_null_entry(entry))
            {
                struct paging_desc_entry *child_entry =
                    (struct paging_desc_entry *)((uint64_t)(entry->address) << 12);
                if (child_entry)
                {
                    paging_desc_entry_free(child_entry, level - 1);
                }
            }
        }
    }
    kfree(table_entry);
}

void paging_desc_free(struct paging_desc *desc)
{
    paging_map_level_t level = desc->level;
    // loop through all entries and free
    for (int i = 0; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++)
    {
        // Free all the root entires PML4|5
        struct paging_desc_entry *entry = &desc->pml->entries[i];
        if (!paging_null_entry(entry))
        {
            struct paging_desc_entry *child_entry = (struct paging_desc_entry *)((uint64_t)(entry->address) << 12);
            if (child_entry)
            {
                // subtract 1 so the level does down once
                paging_desc_entry_free(child_entry, level - 1);
            }
        }
    }

    // free pml structure
    kfree(desc->pml);

    // free the descriptor
    kfree(desc);
}

// Only 4-level paging is supported (Intel PML5 not implemented).
static bool paging_map_level_is_valid(paging_map_level_t level)
{
    // level 5 not supported yet
    return level == PAGING_MAP_LEVEL_4;
}

// The paging_desc last loaded into CR3 via paging_switch.
struct paging_desc *paging_current_descriptor()
{
    return current_paging_desc;
}

// Round ptr up to the next 4KB page. Used so mappings never start mid-page.
void *paging_align_address(void *ptr)
{
    if ((uintptr_t)ptr % PAGING_PAGE_SIZE)
    {
        return (void *)((uintptr_t)ptr + PAGING_PAGE_SIZE - ((uintptr_t)ptr % PAGING_PAGE_SIZE));
    }
    return ptr;
}

// Round addr down to the start of its 4KB page.
void *paging_align_to_lower_page(void *addr)
{
    uintptr_t _addr = (uintptr_t)addr;
    _addr -= (_addr % PAGING_PAGE_SIZE);
    return (void *)_addr;
}

// Make desc live: remember it, then CR3 = address of its PML4.
void paging_switch(struct paging_desc *desc)
{
    current_paging_desc = desc;
    paging_load_directory((uint64_t *)(&desc->pml->entries[0]));
}

// Allocate a paging_desc + empty PML4. PDPT/PD/PT tables are created lazily by paging_map.
struct paging_desc *paging_desc_new(paging_map_level_t root_map_level)
{
    if (!paging_map_level_is_valid(root_map_level))
    {
        // Invalid map level
        return NULL;
    }

    struct paging_desc *desc = kzalloc(sizeof(struct paging_desc));
    if (!desc)
    {
        // memory error -ENOMEM;
        return NULL;
    }

    desc->pml = paging_pml4_entries_new();
    desc->level = root_map_level;
    return desc;
}

// True if addr is 4096-aligned.
bool paging_is_aligned(void *addr)
{
    return ((uintptr_t)addr % PAGING_PAGE_SIZE) == 0;
}

// Map one 4KB page virt -> phys. Creates missing PDPT/PD/PT tables as needed, then writes the PT slot.
int paging_map(struct paging_desc *desc, void *virt, void *phys, int flags)
{
    int res = 0;
    // extract the array indexes from virtual address
    uintptr_t va = (uintptr_t)virt;

    // Bits	    Name	        Purpose
    // 63–48	Sign Extension	Must be all 1s (for kernel) or all 0s (for user space)
    // 47–39	PML4 Index	    Index into PML4 table
    // 38–30	PDPT Index	    Index into Page Directory Pointer Table (PDPT)
    // 29–21	PD Index	    Index into Page Directory (PD)
    // 20–12	PT Index	    Index into Page Table (PT)
    // 11–0	    Offset	        Offset within the page

    size_t pml4_index = (va >> 39) & 0x1FF;
    size_t pdpt_index = (va >> 30) & 0x1FF;
    size_t pd_index = (va >> 21) & 0x1FF;
    size_t pt_index = (va >> 12) & 0x1FF;

    struct paging_desc_entry *pml4_entry = &desc->pml->entries[pml4_index];

    if (paging_null_entry(pml4_entry))
    {
        // allocate a PDPT
        void *new_pdpt = kzalloc(sizeof(struct paging_desc_entry) * PAGING_TOTAL_ENTRIES_PER_TABLE);
        pml4_entry->address = ((uintptr_t)new_pdpt) >> 12;
        pml4_entry->present = 1;
        pml4_entry->read_write = 1;
        pml4_entry->user_supervisor = 1;
    }

    struct paging_desc_entry *pdpt_entries = (struct paging_desc_entry *)((uintptr_t)(pml4_entry->address) << 12);

    struct paging_desc_entry *pdpt_entry = &pdpt_entries[pdpt_index];
    if (paging_null_entry(pdpt_entry))
    {
        void *new_pd = kzalloc(sizeof(struct paging_desc_entry) * PAGING_TOTAL_ENTRIES_PER_TABLE);
        pdpt_entry->address = ((uintptr_t)new_pd) >> 12;
        pdpt_entry->present = 1;
        pdpt_entry->read_write = 1;
        pdpt_entry->user_supervisor = 1;
    }

    struct paging_desc_entry *pd_entries = (struct paging_desc_entry *)((uintptr_t)(pdpt_entry->address) << 12);

    struct paging_desc_entry *pd_entry = &pd_entries[pd_index];
    if (paging_null_entry(pd_entry))
    {
        void *new_pt = kzalloc(sizeof(struct paging_desc_entry) * PAGING_TOTAL_ENTRIES_PER_TABLE);
        pd_entry->address = ((uintptr_t)new_pt) >> 12;
        pd_entry->present = 1;
        pd_entry->read_write = 1;
        pd_entry->user_supervisor = 1;
    }

    struct paging_desc_entry *pt_entries = (struct paging_desc_entry *)((uintptr_t)(pd_entry->address) << 12);

    struct paging_desc_entry *pt_entry = &pt_entries[pt_index];
    if (!paging_null_entry(pt_entry))
    {
        // invalidate the cache
        paging_invalidate_tlb_entry(virt);
    }
    pt_entry->address = ((uintptr_t)phys) >> 12;
    pt_entry->present = (flags & PAGING_IS_PRESENT) ? 1 : 0;
    pt_entry->read_write = (flags & PAGING_IS_WRITEABLE) ? 1 : 0;
    pt_entry->user_supervisor = (flags & PAGING_ACCESS_FROM_ALL) ? 1 : 0;
    return res;
}

// Identity-map first 1MB (VGA/BIOS) plus every E820 type=1 region. Call before paging_switch.
int paging_map_e820_memory_regions(struct paging_desc *desc)
{
    /* MAC-QEMU-FIX: identity-map 4MB — kernel@1MB / early stack@2MB often reserved in E820 */
    paging_map_to(desc,
                  (void *)0x00,
                  (void *)0x00,
                  (void *)0x400000,
                  PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);
    /* MAC-QEMU-FIX-END */

    size_t total_entries = e820_total_entries();
    for (int i = 0; i < total_entries; i++)
    {
        struct e820_entry *entry = e820_entry(i);
        if (entry->type == 1)
        {
            void *base_addr = (void *)entry->base_addr;
            void *end_addr = (void *)(entry->base_addr + entry->length);

            // Why up? Because you don't want to map below the start of the region.
            // That could overlap reserved memory before it.
            if (!paging_is_aligned(base_addr))
            {
                // we need to round up,
                base_addr = paging_align_address(base_addr);
            }

            // Why down? Because you don't want to map past the end of the region.
            // That could overlap reserved memory after it.
            if (!paging_is_aligned(end_addr))
            {
                end_addr = paging_align_to_lower_page(end_addr);
            }

            paging_map_to(desc, base_addr, base_addr, end_addr, PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);
        }
    }
    return 0;
}

// Map count consecutive pages: virt+i*4096 -> phys+i*4096.
int paging_map_range(struct paging_desc *desc, void *virt, void *phys, size_t count, int flags)
{
    int res = 0;
    for (size_t i = 0; i < count; i++)
    {
        res = paging_map(desc, virt, phys, flags);
        if (res < 0)
            break;

        virt += PAGING_PAGE_SIZE;
        phys += PAGING_PAGE_SIZE;
    }
    return res;
}

// Map the byte range [phys, phys_end) onto virt. All three addresses must be page-aligned.
int paging_map_to(struct paging_desc *desc, void *virt, void *phys, void *phys_end, int flags)
{
    int res = 0;

    // virtualaddress = 0x1000
    // phys = 0x5000;
    // end_phys 0x7000

    // va 0x1000, 0x2000, 0x3000
    // pa 0x5000, 0x6000, 0x7000

    if ((uintptr_t)virt % PAGING_PAGE_SIZE)
    {
        res = -EINVARG;
        goto out;
    }

    if ((uintptr_t)phys % PAGING_PAGE_SIZE)
    {
        res = -EINVARG;
        goto out;
    }

    if ((uintptr_t)phys_end % PAGING_PAGE_SIZE)
    {
        res = -EINVARG;
        goto out;
    }

    if ((uintptr_t)phys_end < (uintptr_t)phys)
    {
        res = -EINVARG;
        goto out;
    }

    uint64_t total_bytes = phys_end - phys;
    size_t total_pages = total_bytes / PAGING_PAGE_SIZE;
    res = paging_map_range(desc, virt, phys, total_pages, flags);

out:
    return res;
}

// Walk PML4 -> PDPT -> PD -> PT using virt's 9-bit indexes. Returns the PT entry pointer (or NULL).
struct paging_desc_entry *paging_get(struct paging_desc *desc, void *virtual_address)
{
    uintptr_t va = (uintptr_t)virtual_address;

    size_t pml4_index = (va >> 39) & 0x1FF;
    size_t pdpt_index = (va >> 30) & 0x1FF;
    size_t pd_index = (va >> 21) & 0x1FF;
    size_t pt_index = (va >> 12) & 0x1FF;

    struct paging_desc_entry *pml4_entry = &desc->pml->entries[pml4_index];
    if (paging_null_entry(pml4_entry))
        return NULL;

    struct paging_desc_entry *pdpt_entries = (struct paging_desc_entry *)((uintptr_t)(pml4_entry->address) << 12);
    struct paging_desc_entry *pdpt_entry = &pdpt_entries[pdpt_index];
    if (paging_null_entry(pdpt_entry))
        return NULL;

    struct paging_desc_entry *pd_entries = (struct paging_desc_entry *)((uintptr_t)(pdpt_entry->address) << 12);
    struct paging_desc_entry *pd_entry = &pd_entries[pd_index];
    if (paging_null_entry(pd_entry))
        return NULL;

    struct paging_desc_entry *pt_entries = (struct paging_desc_entry *)((uintptr_t)(pd_entry->address) << 12);
    return &pt_entries[pt_index];
}

// Physical byte address for virt: (PT.address << 12) + (virt & 0xFFF).
void *paging_get_physical_address(struct paging_desc *desc, void *virtual_address)
{
    struct paging_desc_entry *desc_entry = paging_get(desc, virtual_address);
    if (!desc_entry)
    {
        return NULL;
    }

    uint64_t physical_base = ((uint64_t)desc_entry->address) << 12;
    uint64_t offset = ((uint64_t)virtual_address) & 0xFFF;

    uint64_t full_address = physical_base + offset;
    return (void *)full_address;
}

// OLD CODE BELOW
//==========================================================

// void paging_load_directory(uint32_t *directory);

// static uint32_t *current_directory = 0;

// struct paging_4gb_chunk *paging_new_4gb(uint8_t flags)
// {
//     uint32_t *directory = kzalloc(sizeof(uint32_t) * PAGING_TOTAL_ENTRIES_PER_TABLE);
//     int offset = 0;
//     for (int i = 0; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++)
//     {
//         uint32_t *entry = kzalloc(sizeof(uint32_t) * PAGING_TOTAL_ENTRIES_PER_TABLE);
//         for (int b = 0; b < PAGING_TOTAL_ENTRIES_PER_TABLE; b++)
//         {
//             entry[b] = (offset + (b * PAGING_PAGE_SIZE)) | flags;
//         }
//         offset += (PAGING_TOTAL_ENTRIES_PER_TABLE * PAGING_PAGE_SIZE);
//         directory[i] = (uint32_t)entry | flags | PAGING_IS_WRITEABLE;
//     }

//     struct paging_4gb_chunk *chunk_4gb = kzalloc(sizeof(struct paging_4gb_chunk));
//     chunk_4gb->directory_entry = directory;
//     return chunk_4gb;
// }

// void paging_switch(struct paging_4gb_chunk *directory)
// {
//     paging_load_directory(directory->directory_entry);
//     current_directory = directory->directory_entry;
// }

// void paging_free_4gb(struct paging_4gb_chunk *chunk)
// {
//     for (int i = 0; i < 1024; i++)
//     {
//         uint32_t entry = chunk->directory_entry[i];
//         uint32_t *table = (uint32_t *)(entry & 0xfffff000);
//         kfree(table);
//     }
//     kfree(chunk->directory_entry);
//     kfree(chunk);
// }

// uint32_t *paging_4gb_chunk_get_directory(struct paging_4gb_chunk *chunk)
// {
//     return chunk->directory_entry;
// }

// bool paging_is_aligned(void *addr)
// {
//     return ((uint32_t)addr % PAGING_PAGE_SIZE) == 0;
// }

// int paging_get_indexes(void *virtual_address, uint32_t *directory_index_out, uint32_t *table_index_out)
// {
//     int res = 0;
//     if (!paging_is_aligned(virtual_address))
//     {
//         res = -EINVARG;
//         goto out;
//     }
//     *directory_index_out = ((uint32_t)virtual_address / (PAGING_TOTAL_ENTRIES_PER_TABLE * PAGING_PAGE_SIZE));
//     *table_index_out = ((uint32_t)virtual_address % (PAGING_TOTAL_ENTRIES_PER_TABLE * PAGING_PAGE_SIZE) / PAGING_PAGE_SIZE);
// out:
//     return res;
// }

// void *paging_align_address(void *ptr)
// {
//     if ((uint32_t)ptr % PAGING_PAGE_SIZE)
//     {
//         return (void *)((uint32_t)ptr + PAGING_PAGE_SIZE - ((uint32_t)ptr % PAGING_PAGE_SIZE));
//     }
//     return ptr;
// }

// void *paging_align_to_lower_page(void *addr)
// {
//     uint32_t _addr = (uint32_t)addr;
//     _addr -= (_addr % PAGING_PAGE_SIZE);
//     return (void *)_addr;
// }

// int paging_map(struct paging_4gb_chunk *directory, void *virt, void *phys, int flags)
// {
//     if (((unsigned int)virt % PAGING_PAGE_SIZE) || ((unsigned int)phys % PAGING_PAGE_SIZE))
//     {
//         return -EINVARG;
//     }
//     return paging_set(directory->directory_entry, virt, (uint32_t)phys | flags);
// }

// int paging_map_range(struct paging_4gb_chunk *directory, void *virt, void *phys, int count, int flags)
// {
//     int res = 0;
//     for (int i = 0; i < count; i++)
//     {
//         res = paging_map(directory, virt, phys, flags);
//         if (res < 0)
//             break;
//         virt += PAGING_PAGE_SIZE;
//         phys += PAGING_PAGE_SIZE;
//     }
//     return res;
// }

// int paging_map_to(struct paging_4gb_chunk *directory, void *virt, void *phys, void *phys_end, int flags)
// {
//     int res = 0;
//     if ((uint32_t)virt % PAGING_PAGE_SIZE)
//     {
//         res = -EINVARG;
//         goto out;
//     }

//     if ((uint32_t)phys % PAGING_PAGE_SIZE)
//     {
//         res = -EINVARG;
//         goto out;
//     }

//     if ((uint32_t)phys_end % PAGING_PAGE_SIZE)
//     {
//         res = -EINVARG;
//         goto out;
//     }
//     if ((uint32_t)phys_end < (uint32_t)phys)
//     {
//         res = -EINVARG;
//         goto out;
//     }

//     uint32_t total_bytes = phys_end - phys;
//     int total_pages = total_bytes / PAGING_PAGE_SIZE;
//     res = paging_map_range(directory, virt, phys, total_pages, flags);

// out:
//     return res;
// }

// int paging_set(uint32_t *directory, void *virt, uint32_t val)
// {
//     if (!paging_is_aligned(virt))
//     {
//         return -EINVARG;
//     }

//     uint32_t directory_index = 0;
//     uint32_t table_index = 0;
//     int res = paging_get_indexes(virt, &directory_index, &table_index);
//     if (res < 0)
//     {
//         return res;
//     }
//     uint32_t entry = directory[directory_index];
//     uint32_t *table = (uint32_t *)(entry & 0xfffff000);
//     table[table_index] = val;

//     return 0;
// }

// void *paging_get_physical_address(uint32_t *directory, void *virt)
// {
//     void *virt_addr_new = (void *)paging_align_to_lower_page(virt);
//     void *difference = (void *)((uint32_t)virt - (uint32_t)virt_addr_new);
//     return (void *)((paging_get(directory, virt_addr_new) & 0xfffff000) + difference);
// }

// uint32_t paging_get(uint32_t *directory, void *virt)
// {
//     uint32_t directory_index = 0;
//     uint32_t table_index = 0;
//     paging_get_indexes(virt, &directory_index, &table_index);
//     uint32_t entry = directory[directory_index];
//     uint32_t *table = (uint32_t *)(entry & 0xfffff000);
//     return table[table_index];
// }
