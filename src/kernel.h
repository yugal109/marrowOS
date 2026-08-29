#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 20
#define MARROWOS_MAX_PATH 108

void kernel_main();
void print(const char *str);
void panic(const char *msg);
void kernel_page();
void kernel_registers();
void terminal_writechar(char c, char color);

#define ERROR(value) ((void *)(intptr_t)(value))
#define ERROR_I(value) ((int)(intptr_t)(value))
#define ISERR(value) ((int)(intptr_t)(value) < 0)

#endif
