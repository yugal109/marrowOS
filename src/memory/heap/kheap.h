#ifndef KHEAP_H
#define KHEAP_H
#include <stdint.h>
#include <stddef.h>

// Build minimal heap + multiheap from E820. Call before paging_switch.
void kheap_init();
// After paging_switch: lock multiheap and create paging shadows (calls multiheap_ready).
void kheap_post_paging();
// First-pass only. Contiguous physical or NULL. Never panics.
void *kmalloc(size_t size);
void *kzalloc(size_t size);
// First pass, then second-pass defrag. Panics if both fail.
void *kpalloc(size_t size);
void *kpzalloc(size_t size);
void kfree(void *ptr);
struct heap *kheap_get();
void *krealloc(void *old_ptr, size_t new_size);

#endif
