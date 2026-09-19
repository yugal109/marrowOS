#ifndef USERLAND_VECTOR_H
#define USERLAND_VECTOR_H
#include <stddef.h>
#include <stdint.h>

typedef long off_t;
typedef int (*VECTOR_REORDER_FUNCTION)(void *first_element, void *second_element);

enum
{
    VECTOR_NO_FLAGS = 0
};

struct vector
{
    // Vector memory
    void *memory;

    // Any flags associated with the vector
    int flags;

    // Size of a single element in the vector
    size_t e_size;

    // Total elements in this vector
    size_t t_elems;

    // Total elements that can fit in the vector currently
    // if t_elems == tm_elems then we need to resize the vector
    size_t tm_elems;

    // Total reserved elements per resize
    size_t t_reserved_elements;
};

struct vector *vector_new(size_t element_size, size_t total_reserved_elements_per_resize, int flags);
void vector_free(struct vector *vec);
int vector_push(struct vector *vec, void *elem);
int vector_has(struct vector *vec, void *elem_val_ptr, size_t elem_size, size_t *index_out);
int vector_pop(struct vector *vec);
void vector_reorder(struct vector *vec, VECTOR_REORDER_FUNCTION reorder_function);
int vector_overwrite(struct vector *vec, int index, void *elem, size_t elem_size);
int vector_back(struct vector *vec, void *data_out, size_t size);
int vector_at(struct vector *vec, size_t index, void *data_out, size_t size);
size_t vector_count(struct vector *vec);
int vector_pop_element(struct vector *vec, void *mem_val, size_t size);

#endif
