#ifndef MARROWOS_STDLIB_H
#define MARROWOS_STDLIB_H
#include <stddef.h>

void *yreserve(size_t size);
void yfree(void *ptr);
char *itoa(int i);

#endif
