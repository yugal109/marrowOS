#include "stdlib.h"
#include "marrowos.h"

void *yreserve(size_t size)
{
    return marrowos_malloc(size);
};

void yfree(void *ptr)
{
    marrowos_free(ptr);
};
