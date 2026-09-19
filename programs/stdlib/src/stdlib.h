#ifndef MARROWOS_STDLIB_H
#define MARROWOS_STDLIB_H
#include <stddef.h>

void *calloc(size_t n_memb, size_t size);
void *realloc(void *ptr, size_t new_size);
void *yreserve(size_t size);
void yfree(void *ptr);
char *itoa(int i);
void udelay(unsigned long microseconds);
void usleep(unsigned long miliseconds);
void *malloc(size_t size);
void free(void *ptr);
int atoi(const char *str);

#endif
