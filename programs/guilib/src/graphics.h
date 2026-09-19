#ifndef GUILIB_GRAPHICS_H
#define GUILIB_GRAPHICS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Use this structure for accessing pixels
struct framebuffer_pixel
{
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
};

struct graphics
{
    // Metadata about the graphics
    size_t x;
    size_t y;
    size_t width;
    size_t height;

    // NULL until you call get_pixels
    struct framebuffer_pixel *pixels;

    // Pointer to the userland graphics pointer kernel internal representation
    void *userland_ptr;
};

/**
 * Creates relative graphics from a parent.
 */
void *graphics_create_relative(void *parent_graphics, size_t x, size_t y, size_t width, size_t height);
struct framebuffer_pixel *graphics_get_pixel_buffer(struct graphics *graphics);
bool graphics_in_bounds(struct graphics *graphics, size_t x, size_t y);
void graphics_draw_pixel(struct graphics *graphics_info, uint32_t x, uint32_t y, struct framebuffer_pixel pixel);
void graphics_draw_rect(struct graphics *graphics_info, uint32_t x, uint32_t y, uint32_t width, uint32_t height, struct framebuffer_pixel pixel_color);

#endif
