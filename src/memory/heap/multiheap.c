#include "multiheap.h"
#include "kernel.h"
#include "memory/paging/paging.h"
#include "status.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Allocate the multiheap descriptor from starting_heap. All later metadata
// (nodes, other heap structs, paging-shadow tables) also comes from this pool.
struct multiheap *multiheap_new(struct heap *starting_heap)
{
    struct multiheap *multiheap = heap_zalloc(starting_heap, sizeof(struct multiheap));
    if (!multiheap)
    {
        goto out;
    }
    multiheap->starting_heap = starting_heap;
    multiheap->first_multiheap = 0;
    multiheap->total_heaps = 0;
out:
    return multiheap;
}

// Walk the chain and return the tail node (used when linking a new heap on).
struct multiheap_single_heap *multiheap_get_last_heap(struct multiheap *multiheap)
{
    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current->next != 0)
    {
        current = current->next;
    }
    return current;
}

// True if this node is allowed to take part in kpalloc second-pass defrag.
static bool multiheap_heap_allows_paging(struct multiheap_single_heap *heap)
{
    return heap->flags & MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING;
}

// Highest physical eaddr across all heaps. That value becomes max_end_data_addr,
// the split: below = real RAM, above = paging-shadow virtual space.
void *multiheap_get_max_memory_end_address(struct multiheap *multiheap)
{
    void *max_addr = 0x00;
    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current)
    {
        if (current->heap->eaddr >= max_addr)
        {
            max_addr = current->heap->eaddr;
        }
        current = current->next;
    }
    return max_addr;
}

// Find the physical heap node whose [saddr, eaddr] contains this address.
struct multiheap_single_heap *multiheap_get_heap_for_address(struct multiheap *multiheap, void *address)
{
    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current)
    {
        if (heap_is_address_within_heap(current->heap, address))
        {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// One comparison: ptr >= max_end_data_addr means it came from kpalloc second pass.
bool multiheap_is_address_virtual(struct multiheap *multiheap, void *ptr)
{
    return ptr >= multiheap->max_end_data_addr;
}

// Has multiheap_ready() run? After that, the door is locked and shadows exist.
bool multiheap_is_ready(struct multiheap *multiheap)
{
    return multiheap->flags & MULTIHEAP_FLAG_IS_READY;
}

// New heaps can only be added before ready. Adding after would break the
// physical/virtual split because max_end_data_addr is already frozen.
bool multiheap_can_add_heap(struct multiheap *multiheap)
{
    return !multiheap_is_ready(multiheap);
}

// Convert a paging-shadow virtual ptr back to the mirrored physical address
// by subtracting max_end_data_addr. Used to find the twin physical heap.
void *multiheap_virtual_address_to_physical(struct multiheap *multiheap, void *ptr)
{
    void *phys_addr = (void *)((uintptr_t)ptr - ((uintptr_t)multiheap->max_end_data_addr));
    return phys_addr;
}

// Find which node owns this virtual address by checking each paging_heap range.
struct multiheap_single_heap *multiheap_get_paging_heap_for_address(struct multiheap *multiheap, void *address)
{
    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current)
    {
        if (!multiheap_heap_allows_paging(current))
        {
            current = current->next;
            continue;
        }

        if (heap_is_address_within_heap(current->paging_heap, address))
        {
            return current;
        }
        current = current->next;
    }
    return 0;
}

// Given any ptr (kmalloc physical OR kpalloc virtual), fill in:
//   heap_out        — physical heap node
//   paging_heap_out — paging-shadow node (NULL if ptr is physical)
//   real_phys_addr  — the physical address to free on the real heap
void multiheap_get_heap_and_paging_heap_for_address(struct multiheap *multiheap,
                                                    void *ptr,
                                                    struct multiheap_single_heap **heap_out,
                                                    struct multiheap_single_heap **paging_heap_out,
                                                    void **real_phys_addr)
{
    void *real_addr = ptr;
    // is this a paging address nor not?
    if (multiheap_is_address_virtual(multiheap, ptr))
    {
        // This is a paging address so lets get the heap thats its
        // associated with
        *paging_heap_out = multiheap_get_paging_heap_for_address(multiheap, ptr);

        // recalculate the real address
        real_addr = multiheap_virtual_address_to_physical(multiheap, ptr);
    }

    *heap_out = multiheap_get_heap_for_address(multiheap, real_addr);
    *real_phys_addr = real_addr;
}

// How many 4KB blocks this allocation occupies. For a virtual ptr we count
// on the paging heap (contiguous virtual slots); that is how many phys pages to free.
size_t multiheap_allocation_block_count(struct multiheap *multiheap, void *ptr)
{
    struct multiheap_single_heap *paging_heap = NULL;
    struct multiheap_single_heap *phys_heap = NULL;
    struct multiheap_single_heap *heap_to_check = NULL;
    void *real_phys_addr = NULL;
    multiheap_get_heap_and_paging_heap_for_address(multiheap, ptr, &phys_heap, &paging_heap, &real_phys_addr);

    if (paging_heap)
    {
        heap_to_check = paging_heap;
    }

    if (!heap_to_check)
    {
        // not allocated from us
        return 0;
    }

    size_t total_blocks = heap_allocation_block_count(heap_to_check->heap, ptr);
    return total_blocks;
}

// Block count converted to bytes (N * 4096).
size_t multiheap_allocation_byte_count(struct multiheap *multiheap, void *ptr)
{
    return multiheap_allocation_block_count(multiheap, ptr) * MARROWOS_HEAP_BLOCK_SIZE;
}

// Link an already-built struct heap into the chain. Rejected after ready.
int multiheap_add_heap(struct multiheap *multiheap, struct heap *heap, int flags)
{
    // Don't allow heaps to be added if the multi-heap has been marked
    // as ready
    if (!multiheap_can_add_heap(multiheap))
    {
        return -EINVARG;
    }

    struct multiheap_single_heap *new_heap = heap_zalloc(multiheap->starting_heap, sizeof(struct multiheap_single_heap));
    if (!new_heap)
    {
        return -ENOMEM;
    }

    new_heap->heap = heap;
    new_heap->next = 0;
    new_heap->flags = flags;
    if (multiheap->first_multiheap == 0)
    {
        multiheap->first_multiheap = new_heap;
    }
    else
    {
        struct multiheap_single_heap *last = multiheap_get_last_heap(multiheap);
        last->next = new_heap;
    }

    multiheap->total_heaps += 1;
    return 0;
}

// Same as add_heap, but forces EXTERNALLY_OWNED so teardown will not free this heap
// (used for the minimal heap, which lives in kernel BSS).
int multiheap_add_existing_heap(struct multiheap *multiheap, struct heap *heap, int flags)
{
    flags |= MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED;
    return multiheap_add_heap(multiheap, heap, flags);
}

// Build a brand-new heap over an E820 range: struct + table come from starting_heap,
// data pool is the raw [saddr, eaddr] region. Then link it into the chain.
int multiheap_add(struct multiheap *multiheap, void *saddr, void *eaddr, int flags)
{
    struct heap *heap = heap_zalloc(multiheap->starting_heap, sizeof(struct heap));
    struct heap_table *table = heap_zalloc(multiheap->starting_heap, sizeof(struct heap_table));
    if (!heap || !table)
    {
        return -ENOMEM;
    }

    int res = heap_create(heap, saddr, eaddr, table);
    if (res < 0)
    {
        heap_free(multiheap->starting_heap, heap);
        heap_free(multiheap->starting_heap, table);
        return res;
    }

    return multiheap_add_heap(multiheap, heap, flags);
}

// Free one allocation. Physical (kmalloc): mark blocks free on that heap.
// Virtual (kpalloc): for each virtual page, reverse-translate to physical,
// recursively free the physical block, then free the virtual slots (callback unmaps PT).
void multiheap_free(struct multiheap *multiheap, void *ptr)
{
    struct multiheap_single_heap *paging_heap = NULL;
    struct multiheap_single_heap *phys_heap = NULL;
    void *real_phys_addr = NULL;

    multiheap_get_heap_and_paging_heap_for_address(multiheap, ptr, &phys_heap, &paging_heap, &real_phys_addr);

    if (paging_heap)
    {
        size_t total_blocks = heap_allocation_block_count(paging_heap->paging_heap, ptr);
        size_t starting_block = heap_address_to_block(paging_heap->paging_heap, ptr);
        size_t ending_block = starting_block + total_blocks;
        for (size_t i = starting_block; i < ending_block; i++)
        {
            void *virtual_address_for_block = (void *)((uintptr_t)ptr) + ((i - starting_block) * MARROWOS_HEAP_BLOCK_SIZE);
            void *data_phys_addr = paging_get_physical_address(paging_current_descriptor(), virtual_address_for_block);

            // We have the physical address now we can call multiheap_free again
            multiheap_free(multiheap, data_phys_addr);
        }

        // Release the allocation in the paging heap
        heap_free(paging_heap->paging_heap, ptr);
    }
    else if (phys_heap)
    {
        heap_free(phys_heap->heap, real_phys_addr);
    }
}

// Destroy the multiheap object and every heap it owns. Leaves EXTERNALLY_OWNED heaps alone.
void multiheap_free_heap(struct multiheap *multiheap)
{
    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current != 0)
    {
        struct multiheap_single_heap *next = current->next;
        if (!(current->flags & MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED))
        {
            heap_free(multiheap->starting_heap, current->heap);
        }
        current = next;
    }

    heap_free(multiheap->starting_heap, multiheap);
}

// First pass: walk every physical heap, try heap_malloc for N contiguous blocks.
// Success = identity-mapped physical ptr. Failure = fragmentation or OOM.
void *multiheap_alloc_first_pass(struct multiheap *multiheap, size_t size)
{
    void *allocation_ptr = NULL;
    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current != 0)
    {
        allocation_ptr = heap_malloc(current->heap, size);
        if (allocation_ptr)
        {
            // Memory was allocated
            break;
        }

        current = current->next;
    }

    return allocation_ptr;
}

// Pick a DEFRAG heap that has enough scattered free physical blocks, then
// allocate N *contiguous* slots in its paging shadow. Returns the virtual start.
void *multiheap_alloc_paging(struct multiheap *multiheap, size_t size, struct multiheap_single_heap **eligible_heap_out)
{
    void *allocation_ptr = NULL;
    size_t total_required_blocks = size / MARROWOS_HEAP_BLOCK_SIZE;
    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current != 0)
    {
        if (!multiheap_heap_allows_paging(current))
        {
            current = current->next;
            continue;
        }
        if (current->heap->free_blocks < total_required_blocks)
        {
            current = current->next;
            continue;
        }
        allocation_ptr = heap_malloc(current->paging_heap, size);
        if (allocation_ptr)
        {
            if (eligible_heap_out)
            {
                *eligible_heap_out = current;
            }
            break;
        }

        current = current->next;
    }
    return allocation_ptr;
}

// Defrag: contiguous virtual range + one scattered physical 4KB page mapped to
// each slot. Caller sees one linear buffer; physically the pages are anywhere.
void *multiheap_alloc_second_pass(struct multiheap *multiheap, size_t size)
{
    void *allocation_ptr = NULL;
    struct paging_desc *paging_desc = paging_current_descriptor();
    if (!paging_desc)
    {
        panic("setup paging before defragmentation processes can happen\n");
    }

    size = heap_align_value_to_upper(size);
    size_t total_blocks = size / MARROWOS_HEAP_BLOCK_SIZE;
    struct multiheap_single_heap *chosen_real_heap = NULL;

    void *defragmented_virtual_memory_saddr = multiheap_alloc_paging(multiheap, size, &chosen_real_heap);
    if (!defragmented_virtual_memory_saddr)
    {
        allocation_ptr = NULL;
        goto out;
    }

    void *defragmented_virtual_memory_current_addr = defragmented_virtual_memory_saddr;
    allocation_ptr = defragmented_virtual_memory_saddr;

    for (size_t i = 0; i < total_blocks; i++)
    {
        void *block_addr = heap_zalloc(chosen_real_heap->heap, MARROWOS_HEAP_BLOCK_SIZE);
        if (!block_addr)
        {
            panic("Something went wrong, is there not enough bytes in physical heap but there is in paging heap.");
        }
        paging_map(paging_desc,
                   defragmented_virtual_memory_current_addr,
                   block_addr,
                   PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);
        defragmented_virtual_memory_current_addr += (uint64_t)MARROWOS_HEAP_BLOCK_SIZE;
    }

out:
    return allocation_ptr;
}

// Paging-heap free callback: wipe the PT entry so that virtual page is NOT PRESENT again.
void multiheap_paging_heap_free_block(void *ptr)
{
    paging_map(paging_current_descriptor(), ptr, NULL, 0);
}

// Point of no return. Lock adding heaps, freeze max_end_data_addr, then for every
// DEFRAG heap create a virtual shadow (clean block table, range mapped NOT PRESENT).
int multiheap_ready(struct multiheap *multiheap)
{
    int res = 0;
    multiheap->flags |= MULTIHEAP_FLAG_IS_READY;

    struct paging_desc *paging_desc = paging_current_descriptor();
    if (!paging_desc)
    {
        panic("You must've had paging setup at this point for this to work\n");
    }

    void *max_end_addr = multiheap_get_max_memory_end_address(multiheap);
    multiheap->max_end_data_addr = max_end_addr;

    struct multiheap_single_heap *current = multiheap->first_multiheap;
    while (current)
    {
        if (multiheap_heap_allows_paging(current))
        {
            void *paging_heap_starting_address = max_end_addr + (uint64_t)current->heap->saddr;
            void *paging_heap_ending_address = max_end_addr + (uint64_t)current->heap->eaddr;

            struct heap_table *paging_heap_table = heap_zalloc(multiheap->starting_heap, sizeof(struct heap_table));
            paging_heap_table->entries = heap_zalloc(multiheap->starting_heap, current->heap->table->total * sizeof(HEAP_BLOCK_TABLE_ENTRY));
            paging_heap_table->total = current->heap->table->total;

            struct heap *paging_heap = heap_zalloc(multiheap->starting_heap, sizeof(struct heap));
            heap_create(paging_heap, paging_heap_starting_address, paging_heap_ending_address, paging_heap_table);

            paging_map_to(paging_current_descriptor(), paging_heap_starting_address, paging_heap_starting_address, paging_heap_ending_address, 0);

            heap_callbacks_set(paging_heap, NULL, multiheap_paging_heap_free_block);
            current->paging_heap = paging_heap;
        }
        current = current->next;
    }

out:
    return res;
}

// kmalloc: first pass only. No defrag. NULL if no contiguous physical run exists.
void *multiheap_alloc(struct multiheap *multiheap, size_t size)
{
    void *allocation_ptr = multiheap_alloc_first_pass(multiheap, size);
    if (allocation_ptr)
    {
        return allocation_ptr;
    }

    // Normal alloc does not defragment with paging
    return NULL;
}

// kpalloc: first pass, then second pass if physical RAM is fragmented.
void *multiheap_palloc(struct multiheap *multiheap, size_t size)
{
    void *allocation_ptr = multiheap_alloc_first_pass(multiheap, size);
    if (allocation_ptr)
    {
        return allocation_ptr;
    }

    // Possible fragmentation, no pointer able to be found
    // in all heaps.
    // perform second pass..

    allocation_ptr = multiheap_alloc_second_pass(multiheap, size);
    return allocation_ptr;
}
