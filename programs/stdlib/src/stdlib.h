#ifndef MARROWOS_STDLIB_H
#define MARROWOS_STDLIB_H
#include <stddef.h>

void *calloc(size_t n_memb, size_t size);
void *realloc(void *ptr, size_t new_size);
void *yreserve(size_t size);
void yfree(void *ptr);
char *itoa(int i);

#endif
