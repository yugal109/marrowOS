#include "vector.h"
#include "stdlib.h"
#include "memory.h"
#include <stddef.h>
#include <stdint.h>

static off_t vector_offset(struct vector *vec, off_t index)
{
    return (off_t)vec->e_size * index;
}

static int vector_valid_bounds(struct vector *vec, size_t index)
{
    return (index < vec->t_elems) ? 0 : -1;
}

static void *vector_memory_at_index(struct vector *vec, size_t index)
{
    off_t offset = vector_offset(vec, index);
    return (void *)((uintptr_t)vec->memory + offset);
}

struct vector *vector_new(size_t element_size, size_t total_reserved_elements_per_resize, int flags)
{
    struct vector *vec = calloc(1, sizeof(struct vector));
    if (!vec)
    {
        return NULL;
    }
    vec->e_size = element_size;
    vec->flags = flags;
    vec->t_elems = 0;
    vec->t_reserved_elements = total_reserved_elements_per_resize;
    vec->tm_elems = 0;
    vec->memory = NULL;

    return vec;
}

int vector_resize(struct vector *vec, size_t total_needed_elements)
{
    int res = 0;
    size_t total_elements_required = vec->t_elems + total_needed_elements;
    if (vec->tm_elems > total_elements_required)
    {
        return 0;
    }

    size_t final_total_elements_required = vec->t_reserved_elements + total_elements_required;
    size_t total_bytes_required = final_total_elements_required * vec->e_size;
    vec->memory = realloc(vec->memory, total_bytes_required);
    if (!vec->memory)
    {
        res = -1;
        goto out;
    }

    vec->tm_elems = final_total_elements_required;
    // never shrinks, only grows
out:
    return res;
}

int vector_has(struct vector *vec, void *elem_val_ptr, size_t elem_size, size_t *index_out)
{
    if (vec->e_size != elem_size)
    {
        return -1;
    }

    int res = -1;
    char tmp_buf[elem_size];
    size_t total_elems = vector_count(vec);
    size_t i = 0;
    for (i = 0; i < total_elems; i++)
    {
        vector_at(vec, i, tmp_buf, elem_size);
        if (memcmp(elem_val_ptr, tmp_buf, elem_size) == 0)
        {
            res = 0;
            break;
        }
    }
    *index_out = i;
    return res;
}

int vector_push(struct vector *vec, void *elem)
{
    int res = vector_resize(vec, 1);
    if (res < 0)
    {
        goto out;
    }

    res = vec->t_elems;
    memcpy(vector_memory_at_index(vec, vec->t_elems), elem, vec->e_size);
    vec->t_elems++;

out:
    return res;
}

int vector_overwrite(struct vector *vec, int index, void *elem, size_t elem_size)
{
    int res = vector_valid_bounds(vec, index);
    if (res < 0)
    {
        goto out;
    }

    if (elem_size < vec->e_size)
    {
        res = -1;
        goto out;
    }

    memcpy(vector_memory_at_index(vec, index), elem, vec->e_size);

out:
    return res;
}

int vector_pop(struct vector *vec)
{
    if (vec->t_elems == 0)
    {
        return -1;
    }

    vec->t_elems--;
    return 0;
}

int vector_back(struct vector *vec, void *data_out, size_t size)
{
    return vector_at(vec, vec->t_elems - 1, data_out, size);
}

void vector_reorder(struct vector *vec, VECTOR_REORDER_FUNCTION reorder_function)
{
    if (!vec || !reorder_function)
        return;

    size_t count = vector_count(vec);
    if (count < 2)
        return;

    // bubble sort, using two swap buffers
    uint8_t *elem1 = yreserve(vec->e_size);
    uint8_t *elem2 = yreserve(vec->e_size);

    if (!elem1 || !elem2)
    {
        if (elem1)
            yfree(elem1);
        if (elem2)
            yfree(elem2);
        return;
    }

    for (size_t i = 0; i < count - 1; i++)
    {
        for (size_t j = 0; j < count - i - 1; j++)
        {
            if (vector_at(vec, j, elem1, vec->e_size) < 0)
                goto cleanup;

            if (vector_at(vec, j + 1, elem2, vec->e_size) < 0)
                goto cleanup;

            if (reorder_function(elem1, elem2) > 0)
            {
                if (vector_overwrite(vec, j, elem2, vec->e_size) < 0)
                    goto cleanup;
                if (vector_overwrite(vec, j + 1, elem1, vec->e_size) < 0)
                    goto cleanup;
            }
        }
    }

cleanup:
    yfree(elem1);
    yfree(elem2);
}

int vector_at(struct vector *vec, size_t index, void *data_out, size_t size)
{
    int res = 0;

    if (size < vec->e_size)
    {
        res = -1;
        goto out;
    }

    if (!data_out)
    {
        res = -1;
        goto out;
    }

    res = vector_valid_bounds(vec, index);
    if (res < 0)
    {
        goto out;
    }

    memcpy(data_out, vector_memory_at_index(vec, index), vec->e_size);

out:
    if (res < 0)
    {
        memset(data_out, 0, size);
    }
    return res;
}

int vector_pop_element(struct vector *vec, void *mem_val, size_t size)
{
    int res = 0;

    if (size != vec->e_size)
    {
        return -1;
    }

    size_t total_elements = vector_count(vec);
    long index_to_remove = -1;

    void *tmp_mem = calloc(1, size);
    if (!tmp_mem)
    {
        return -1;
    }

    for (size_t i = 0; i < total_elements; i++)
    {
        res = vector_at(vec, i, tmp_mem, size);
        if (res < 0)
        {
            goto out;
        }

        if (memcmp(tmp_mem, mem_val, size) == 0)
        {
            index_to_remove = i;
            break;
        }
    }

    if (index_to_remove == -1)
    {
        res = -1;
        goto out;
    }

    if ((size_t)index_to_remove == total_elements - 1)
    {
        res = vector_pop(vec);
        goto out;
    }

    {
        uint8_t *ptr_to_element_to_delete = (uint8_t *)vec->memory + (index_to_remove * size);
        uint8_t *ptr_to_next_element = ptr_to_element_to_delete + size;

        size_t total_elements_to_copy = (vec->t_elems - index_to_remove) - 1;
        size_t total_bytes_to_copy = total_elements_to_copy * size;

        memcpy(ptr_to_element_to_delete, ptr_to_next_element, total_bytes_to_copy);
        vec->t_elems--;
    }

out:
    yfree(tmp_mem);
    return res;
}

size_t vector_count(struct vector *vec)
{
    return vec->t_elems;
}

void vector_free(struct vector *vec)
{
    if (vec->memory)
    {
        yfree(vec->memory);
    }
    yfree(vec);
}
