#ifndef MARROWOS_H
#define MARROWOS_H
#include <stddef.h>

void print(const char *message);
int getkey();
void *marrowos_malloc(size_t size);
void marrowos_free(void *ptr);

#endif
