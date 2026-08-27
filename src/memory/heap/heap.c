#include "heap.h"
#include "kernel.h"
#include "status.h"
#include "memory/memory.h"
#include <stdbool.h>
#include <stdint.h>

int64_t heap_address_to_block(struct heap *heap, void *address);

static int heap_validate_table(void *ptr, void *end, struct heap_table *table)
{
    int res = 0;
    size_t table_size = (size_t)(end - ptr);
    size_t total_blocks = table_size / MARROWOS_HEAP_BLOCK_SIZE;
    if (table->total != total_blocks)
    {
        res = -EINVARG;
        goto out;
    }
out:
    return res;
}

static bool heap_validate_alignment(void *ptr)
{
    return ((uintptr_t)ptr % MARROWOS_HEAP_BLOCK_SIZE) == 0;
}

int heap_create(struct heap *heap, void *ptr, void *end, struct heap_table *table)
{
    int res = 0;

    if (!heap_validate_alignment(ptr) || !heap_validate_alignment(end))
    {
        res = -EINVARG;
        goto out;
    };
    memset(heap, 0, sizeof(struct heap));
    heap->saddr = ptr;
    heap->eaddr = end;
    heap->table = table;
    heap->total_blocks = table->total;
    heap->free_blocks = table->total;
    heap->used_blocks = 0;

    res = heap_validate_table(ptr, end, table);
    if (res < 0)
    {
        goto out;
    }

    size_t table_size = sizeof(HEAP_BLOCK_TABLE_ENTRY) * table->total;

    // initialize all 100mb of data to zero
    memset(table->entries, HEAP_BLOCK_TABLE_ENTRY_FREE, table_size);

out:
    return res;
}

uintptr_t heap_align_value_to_upper(uintptr_t val)
{
    if ((val % MARROWOS_HEAP_BLOCK_SIZE) == 0)
    {
        return val;
    }
    val = (val - (val % MARROWOS_HEAP_BLOCK_SIZE));
    val += MARROWOS_HEAP_BLOCK_SIZE;

    return val;
}

uintptr_t heap_align_value_to_lower(uintptr_t val)
{
    if ((val % MARROWOS_HEAP_BLOCK_SIZE) == 0)
    {
        return val;
    }

    // subtract the remainder
    val = val - (val % MARROWOS_HEAP_BLOCK_SIZE);
    return val;
}

static int heap_get_entry_type(HEAP_BLOCK_TABLE_ENTRY entry)
{
    // returns last 4 bits
    return entry & 0x0f;
}

bool heap_is_address_within_heap(struct heap *heap, void *ptr)
{
    return (ptr >= heap->saddr && ptr <= heap->eaddr);
}

void heap_callbacks_set(struct heap *heap, HEAP_BLOCK_ALLOCATED_CALLBACK_FUNCTION allocated_func, HEAP_BLOCK_FREE_CALLBACK_FUNCTION free_func)
{
    heap->block_allocated_callback = allocated_func;
    heap->block_free_callback = free_func;
}

int64_t heap_get_start_block(struct heap *heap, uintptr_t total_blocks)
{
    struct heap_table *table = heap->table;
    int64_t block_current = 0;
    int64_t block_start = -1;

    for (size_t i = 0; i < table->total; i++)
    {
        if (heap_get_entry_type(table->entries[i]) != HEAP_BLOCK_TABLE_ENTRY_FREE)
        {
            block_current = 0;
            block_start = -1;
            continue;
        }

        // is it first block ?
        if (block_start == -1)
        {
            block_start = i;
        }
        block_current++;

        // we found enough contigious blocks to give back
        if (block_current == total_blocks)
        {
            break;
        }
    }
    if (block_current != total_blocks)
    {
        return -ENOMEM;
    }
    return block_start;
}

size_t heap_allocation_block_count(struct heap *heap, void *starting_address)
{
    size_t count = 0;
    struct heap_table *heap_table = heap->table;
    int64_t starting_block = heap_address_to_block(heap, starting_address);
    if (starting_block < 0)
    {
        goto out;
    }

    for (int64_t i = starting_block; i < (int64_t)heap_table->total; i++)
    {
        HEAP_BLOCK_TABLE_ENTRY entry = heap_table->entries[i];
        if (entry & HEAP_BLOCK_TABLE_ENTRY_TAKEN)
        {
            count++;
        }

        // End of this block chain?
        if (!(entry & HEAP_BLOCK_HAS_NEXT))
        {
            break;
        }
    }

out:
    return count;
}

void *heap_block_to_address(struct heap *heap, int64_t block)
{
    return heap->saddr + (block * MARROWOS_HEAP_BLOCK_SIZE);
}

void heap_mark_blocks_taken(struct heap *heap, int64_t start_block, int64_t total_blocks)
{
    int64_t end_block = (start_block + total_blocks) - 1;
    HEAP_BLOCK_TABLE_ENTRY entry = HEAP_BLOCK_TABLE_ENTRY_TAKEN | HEAP_BLOCK_IS_FIRST;
    if (total_blocks > 1)
    {
        entry |= HEAP_BLOCK_HAS_NEXT;
    }

    for (int64_t i = start_block; i <= end_block; i++)
    {
        heap->table->entries[i] = entry;
        entry = HEAP_BLOCK_TABLE_ENTRY_TAKEN;
        if (i != end_block)
        {
            entry |= HEAP_BLOCK_HAS_NEXT;
        }
        void *address = heap_block_to_address(heap, i);
        if (heap->block_allocated_callback)
        {
            heap->block_allocated_callback(address, MARROWOS_HEAP_BLOCK_SIZE);
        }
    }
}

void *heap_malloc_blocks(struct heap *heap, uintptr_t total_blocks)
{
    void *address = 0;

    int64_t start_block = heap_get_start_block(heap, total_blocks);
    if (start_block < 0)
    {
        goto out;
    }
    address = heap_block_to_address(heap, start_block);

    // Mark the blocks as taken
    heap_mark_blocks_taken(heap, start_block, total_blocks);

    heap->used_blocks += total_blocks;
    heap->free_blocks -= total_blocks;

out:
    return address;
}

void heap_mark_blocks_free(struct heap *heap, int64_t starting_block)
{
    struct heap_table *table = heap->table;
    size_t total_blocks_freed = 0;
    for (int64_t i = starting_block; i < (int64_t)table->total; i++)
    {
        HEAP_BLOCK_TABLE_ENTRY entry = table->entries[i];
        table->entries[i] = HEAP_BLOCK_TABLE_ENTRY_FREE;

        void *address = heap_block_to_address(heap, i);
        if (heap->block_free_callback)
        {
            heap->block_free_callback(address);
        }
        total_blocks_freed++;
        if (!(entry & HEAP_BLOCK_HAS_NEXT))
        {
            break;
        }
    }

    heap->used_blocks -= total_blocks_freed;
    heap->free_blocks += total_blocks_freed;
}

int64_t heap_address_to_block(struct heap *heap, void *address)
{
    return ((int64_t)(address - heap->saddr)) / MARROWOS_HEAP_BLOCK_SIZE;
}

void *heap_malloc(struct heap *heap, size_t size)
{
    size_t aligned_size = heap_align_value_to_upper(size);
    int64_t total_blocks = aligned_size / MARROWOS_HEAP_BLOCK_SIZE;
    return heap_malloc_blocks(heap, total_blocks);
}

void heap_free(struct heap *heap, void *ptr)
{
    heap_mark_blocks_free(heap, heap_address_to_block(heap, ptr));
}

size_t heap_total_size(struct heap *heap)
{
    return heap->table->total * MARROWOS_HEAP_BLOCK_SIZE;
}

size_t heap_total_used(struct heap *heap)
{
    size_t total = 0;
    struct heap_table *table = heap->table;
    for (size_t i = 0; i < table->total; i++)
    {
        if (heap_get_entry_type(table->entries[i]) == HEAP_BLOCK_TABLE_ENTRY_TAKEN)
        {
            total += MARROWOS_HEAP_BLOCK_SIZE;
        }
    }
    return total;
}

size_t heap_total_available(struct heap *heap)
{
    return heap_total_size(heap) - heap_total_used(heap);
}

void *heap_zalloc(struct heap *heap, size_t size)
{
    void *ptr = heap_malloc(heap, size);
    if (!ptr)
    {
        return 0;
    }
    memset(ptr, 0x00, size);
    return ptr;
}
