#ifndef KERNEL_MULTI_HEAP_H
#define KERNEL_MULTI_HEAP_H

#include "heap.h"

enum
{
    // Heap struct lives outside multiheap (minimal heap in BSS). Do not free it.
    MULTIHEAP_HEAP_FLAG_EXTERNALLY_OWNED = 0b00000001,
    // Eligible for kpalloc second pass: gets a virtual paging-shadow heap.
    MULTIHEAP_HEAP_FLAG_DEFRAGMENT_WITH_PAGING = 0b00000010
};

// One node in the multiheap chain: one physical E820 heap + optional virtual shadow.
struct multiheap_single_heap
{
    struct heap *heap;         // physical heap (real RAM, identity mapped)
    struct heap *paging_heap;  // virtual shadow above max_end_data_addr (NULL if no DEFRAG flag)
    int flags;
    struct multiheap_single_heap *next;
};

enum
{
    // Set by multiheap_ready(). After this, no more heaps can be added.
    MULTIHEAP_FLAG_IS_READY = 0x01
};

struct multiheap
{
    // Bootstrap heap that funds all multiheap metadata (always the minimal heap).
    struct heap *starting_heap;

    // Linked list of physical heap nodes (minimal heap first, then other E820 regions).
    struct multiheap_single_heap *first_multiheap;

    // Highest physical eaddr. ptr < this = physical; ptr >= this = virtual (paging shadow).
    void *max_end_data_addr;
    int flags;
    size_t total_heaps;
};

// Create the multiheap struct itself, allocated FROM starting_heap (chicken-and-egg bootstrap).
struct multiheap *multiheap_new(struct heap *starting_heap);

// Lock the multiheap, freeze max_end_data_addr, create paging shadows for every DEFRAG heap.
// Must run AFTER paging_switch(). Called from kheap_post_paging().
int multiheap_ready(struct multiheap *multiheap);

// True once multiheap_ready() has run.
bool multiheap_is_ready(struct multiheap *multiheap);

// Inverse of is_ready: new heaps are only allowed during setup.
bool multiheap_can_add_heap(struct multiheap *multiheap);

// Wrap an already-created heap (minimal heap) and flag it EXTERNALLY_OWNED.
int multiheap_add_existing_heap(struct multiheap *multiheap, struct heap *heap, int flags);

// Create a new heap over [saddr, eaddr], fund its struct/table from starting_heap, link it in.
int multiheap_add(struct multiheap *multiheap, void *saddr, void *eaddr, int flags);

// kmalloc path: first pass only. Contiguous physical blocks or NULL. Never uses paging.
void *multiheap_alloc(struct multiheap *multiheap, size_t size);

// kpalloc path: first pass, then second pass defrag if physical RAM is fragmented.
void *multiheap_palloc(struct multiheap *multiheap, size_t size);

// Second pass: grab contiguous virtual slots, map each to a scattered physical 4KB block.
void *multiheap_alloc_second_pass(struct multiheap *multiheap, size_t size);

// Find a DEFRAG heap with enough free physical blocks, then malloc N contiguous virtual slots.
void *multiheap_alloc_paging(struct multiheap *multiheap, size_t size, struct multiheap_single_heap **eligible_heap_out);

// kfree path: physical ptr -> heap_free; virtual ptr -> free each mapped phys block, then unmap.
void multiheap_free(struct multiheap *multiheap, void *ptr);

// Tear down the whole multiheap (skips EXTERNALLY_OWNED heaps).
void multiheap_free_heap(struct multiheap *multiheap);

// True if ptr lives in paging-shadow space (ptr >= max_end_data_addr).
bool multiheap_is_address_virtual(struct multiheap *multiheap, void *ptr);

// Which physical heap node owns this address.
struct multiheap_single_heap *multiheap_get_heap_for_address(struct multiheap *multiheap, void *address);

// Which node owns this virtual (paging-heap) address.
struct multiheap_single_heap *multiheap_get_paging_heap_for_address(struct multiheap *multiheap, void *address);

// Resolve ptr into physical heap, paging heap (if virtual), and the real physical address.
void multiheap_get_heap_and_paging_heap_for_address(struct multiheap *multiheap, void *ptr, struct multiheap_single_heap **heap_out, struct multiheap_single_heap **paging_heap_out, void **real_phys_addr);

// How many 4KB blocks this allocation occupies (walks HAS_NEXT in the owning heap table).
size_t multiheap_allocation_block_count(struct multiheap *multiheap, void *ptr);

// Same as block_count, in bytes (blocks * 4096).
size_t multiheap_allocation_byte_count(struct multiheap *multiheap, void *ptr);

#endif
