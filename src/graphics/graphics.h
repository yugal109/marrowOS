#ifndef KERNEL_GRAPHICS_H
#define KERNEL_GRAPHICS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "lib/vector/vector.h"
#include "graphics/image/image.h"

enum
{
    GRAPHICS_FLAG_ALLOW_OUT_OF_BOUNDS = 0b00000001,                // allow crop/clone past the edge
    GRAPHICS_FLAG_CLONED_FRAMEBUFFER = 0b00000010,                 // framebuffer pointer is shared, not owned
    GRAPHICS_FLAG_CLONED_CHILDREN = 0b00000100,                    // children list is shared, not owned
    GRAPHICS_FLAG_DO_NOT_COPY_PIXELS = 0b00001000,                 // clone shell only, skip pixel data
    GRAPHICS_FLAG_DO_NOT_OVERWRITE_TRANSPARENT_PIXELS = 0b00010000 // paste: don't stomp transparent dest pixels
};

struct graphics_info;

// note: when we make the mouse update the type variable to MOUSE_CLICK_TYPE
typedef void (*GRAPHICS_MOUSE_CLICK_FUNCTION)(struct graphics_info *graphics, size_t rel_x, size_t rel_y, int type);
typedef void (*GRAPHICS_MOUSE_MOVE_FUNCTION)(struct graphics_info *graphics, size_t rel_x, size_t rel_y, size_t abs_x, size_t abs_y);

struct framebuffer_pixel
{
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
};

struct graphics_info
{
    struct framebuffer_pixel *framebuffer;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixels_per_scanline;

    // width*height*sizeof(struct framebuffer_pixel)
    // Write to these pixels, call redraw to draw to the framebuffer
    struct framebuffer_pixel *pixels;

    // Actual width and height
    uint32_t width;
    uint32_t height;

    // The absolute x and y coordinates where the graphic begins
    uint32_t starting_x;
    uint32_t starting_y;

    // Relative x and y from the parent graphics
    uint32_t relative_x;
    uint32_t relative_y;

    struct graphics_info *parent;
    // Vector of struct graphics_info*
    struct vector *children;

    uint32_t flags;

    // Lower number means that this is drawn first
    // Higher number means that this is drawn last
    uint32_t z_index;

    // Ignore color, if this color is encountered
    // it wont be drawn
    struct framebuffer_pixel ignore_color;

    /**
     * The transparency key applies to all previously drawn pixels
     * essentially drawing what is behind them
     *
     * as with the ignore_color BLACK means there is no ignore color
     * BLACK CANNOT BE USED AS THE Key
     */

    struct framebuffer_pixel transparency_key;

    struct
    {
        GRAPHICS_MOUSE_CLICK_FUNCTION mouse_click;
        GRAPHICS_MOUSE_MOVE_FUNCTION mouse_move;
    } event_handlers;
};

void graphics_draw_rect(
    struct graphics_info *graphics_info,
    uint32_t x,
    uint32_t y,
    size_t width,
    size_t height,
    struct framebuffer_pixel pixel_color);
void graphics_ignore_color(struct graphics_info *graphics_info, struct framebuffer_pixel pixel_color);
void graphics_transparency_key_set(struct graphics_info *graphics_info, struct framebuffer_pixel pixel_color);
void graphics_transparency_key_remove(struct graphics_info *graphics_info);
void graphics_ignore_color_finish(struct graphics_info *graphics_info);

void graphics_redraw(struct graphics_info *g);
void graphics_draw_pixel(struct graphics_info *graphics_info, uint32_t x, uint32_t y, struct framebuffer_pixel pixel);
struct graphics_info *graphics_screen_info();
void graphics_setup(struct graphics_info *main_graphics_info);
void graphics_redraw_all();
void graphics_draw_image(struct graphics_info *graphics_info, struct image *image, int x, int y);
void graphics_draw_image_scaled(struct graphics_info *graphics_info, struct image *image,
                                int x, int y, uint32_t dest_w, uint32_t dest_h);
void graphics_redraw_region(struct graphics_info *g, uint32_t local_x, uint32_t local_y, uint32_t width, uint32_t height);
void graphics_redraw_graphics_to_screen(struct graphics_info *relative_graphics, uint32_t rel_x, uint32_t rel_y, uint32_t width, uint32_t height);

#endif
