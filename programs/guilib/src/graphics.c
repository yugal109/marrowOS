#include "graphics.h"
#include "marrowos.h"
#include "image.h"

void *graphics_create_relative(void *parent_graphics, size_t x, size_t y, size_t width, size_t height)
{
    return marrowos_graphics_create(x, y, width, height, parent_graphics);
}

struct framebuffer_pixel *graphics_get_pixel_buffer(struct graphics *graphics)
{
    // Don't waste CPU calling kernel if we have the buffer
    if (graphics->pixels)
    {
        return graphics->pixels;
    }
    graphics->pixels = (struct framebuffer_pixel *)marrowos_graphic_pixels_get(graphics);
    return graphics->pixels;
}

bool graphics_in_bounds(struct graphics *graphics, size_t x, size_t y)
{
    return x < graphics->width && y < graphics->height;
}

void graphics_draw_pixel(struct graphics *graphics_info, uint32_t x, uint32_t y, struct framebuffer_pixel pixel)
{
    // No ignore or transparency colors available in userland yet
    if (x < graphics_info->width && y < graphics_info->height)
    {
        graphics_info->pixels[y * graphics_info->width + x] = pixel;
    }
}

void graphics_draw_rect(struct graphics *graphics_info, uint32_t x, uint32_t y, uint32_t width, uint32_t height, struct framebuffer_pixel pixel_color)
{
    // Row-major, so the inner loop walks contiguous pixels
    for (uint32_t ly = 0; ly < height; ly++)
    {
        for (uint32_t lx = 0; lx < width; lx++)
        {
            uint32_t ax = x + lx;
            uint32_t ay = y + ly;
            graphics_draw_pixel(graphics_info, ax, ay, pixel_color);
        }
    }
}
