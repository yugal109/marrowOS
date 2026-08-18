#ifndef VGA13_H
#define VGA13_H

#include <stdint.h>

#define VGA13_WIDTH 320
#define VGA13_HEIGHT 200
#define VGA13_FRAMEBUFFER ((uint8_t *)0xA0000)

void vga13_enter();
void vga13_put_pixel(int x, int y, uint8_t color);
void vga13_clear(uint8_t color);

#endif
