#ifndef rain_memory_h
#define rain_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(type, count) \
    (type *)reallocate(NULL, 0, sizeof(type) * (count));

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2) // double capacity , minimum 8

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type *)reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount)) // resize array to newCount elements

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0) // free array by resizing to 0

#define GC_HEAP_GROW_FACTOR 2
/* After a collection, allow at least this much extra headroom before the next
 * GC. Stops matrix-heavy workloads (MNIST) from collecting every few batches. */
#define GC_HEAP_MIN_HEADROOM ((size_t)64 * 1024 * 1024)

void *reallocate(void *pointer, size_t oldSize, size_t newSize); // core memory function, all allocations go through here
void markObject(Obj *object);
void markValue(Value value);
void collectGarbage(void);
void freeObjects(void);

#endif
