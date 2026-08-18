#ifndef STDLIB_H
#define STDLIB_H
#include <stddef.h>

void *yreserve(size_t size);
void yfree(void *ptr);

#endif
