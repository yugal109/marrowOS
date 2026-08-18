#ifndef MARROWOS_H
#define MARROWOS_H
#include <stddef.h>
#include <stdbool.h>

void print(const char *message);
int marrowos_getkey();
void *marrowos_malloc(size_t size);
void marrowos_free(void *ptr);
void marrowos_putchar(char c);
int marrowos_getkey_block();
void marrowos_terminal_readline(char *out, int max, bool output_while_typing);

#endif
