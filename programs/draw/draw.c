#include "stdlib.h"
#include "stdio.h"
#include "memory.h"
#include "window.h"
#include "graphics.h"
#include "image.h"
#include "font.h"

// The eraser is wider so it clears a useful area, not one pen stroke
#define DRAW_BRUSH_SIZE 3
#define ERASER_BRUSH_SIZE 14

#define TOOLBAR_HEIGHT 30
#define TOOLBAR_MARGIN 8
#define TOOLBAR_BTN_SIZE 20
#define TOOLBAR_BTN_GAP 8
// Button glyphs scale with the button size
#define TOOLBAR_BTN_INSET (TOOLBAR_BTN_SIZE / 4)
#define TOOLBAR_RING 2
#define TOOLBAR_TOTAL_BTNS 7

// Keeps strokes off the window edge, where they leave line artifacts
#define CANVAS_MARGIN 10

enum
{
    TOOLBAR_BTN_RED = 0,
    TOOLBAR_BTN_GREEN,
    TOOLBAR_BTN_BLUE,
    TOOLBAR_BTN_BLACK,
    TOOLBAR_BTN_ERASER,
    TOOLBAR_BTN_UNDO,
    TOOLBAR_BTN_CLEAR,
};

struct framebuffer_pixel color_white = {.red = 0xff, .green = 0xff, .blue = 0xff, .reserved = 0};
struct framebuffer_pixel color_ink = {.red = 0x20, .green = 0x20, .blue = 0x20, .reserved = 0};
struct framebuffer_pixel color_red = {.red = 0xd9, .green = 0x3b, .blue = 0x3b, .reserved = 0};
struct framebuffer_pixel color_green = {.red = 0x3b, .green = 0xb5, .blue = 0x5c, .reserved = 0};
struct framebuffer_pixel color_blue = {.red = 0x3b, .green = 0x6c, .blue = 0xd9, .reserved = 0};
struct framebuffer_pixel toolbar_bg = {.red = 0xe0, .green = 0xe0, .blue = 0xe0, .reserved = 0};
struct framebuffer_pixel toolbar_border = {.red = 0x40, .green = 0x40, .blue = 0x40, .reserved = 0};
struct framebuffer_pixel eraser_mark = {.red = 0xe8, .green = 0x8a, .blue = 0x8a, .reserved = 0};

bool colors_equal(struct framebuffer_pixel a, struct framebuffer_pixel b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

int toolbar_btn_x(int index)
{
    return TOOLBAR_MARGIN + index * (TOOLBAR_BTN_SIZE + TOOLBAR_BTN_GAP);
}

int toolbar_btn_y()
{
    return (TOOLBAR_HEIGHT - TOOLBAR_BTN_SIZE) / 2;
}

// -1 if the click missed every button, margins included
int toolbar_hit_test(int x, int y)
{
    int by = toolbar_btn_y();
    if (y < by || y >= by + TOOLBAR_BTN_SIZE)
    {
        return -1;
    }

    for (int i = 0; i < TOOLBAR_TOTAL_BTNS; i++)
    {
        int bx = toolbar_btn_x(i);
        if (x >= bx && x < bx + TOOLBAR_BTN_SIZE)
        {
            return i;
        }
    }

    return -1;
}

int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

// Pulls a point inside the drawable rect, clear of the toolbar and edges
void clamp_to_canvas(struct window *win, int *x, int *y)
{
    *x = clamp_int(*x, CANVAS_MARGIN, win->width - CANVAS_MARGIN - 1);
    *y = clamp_int(*y, TOOLBAR_HEIGHT + CANVAS_MARGIN, win->height - CANVAS_MARGIN - 1);
}

// Redraw rects reaching past the window seem to cause the edge artifacts,
// so clamp before they get to window_redraw_region.
void redraw_region_clamped(struct window *win, int x, int y, int width, int height)
{
    if (x < 0)
    {
        width += x;
        x = 0;
    }
    if (y < 0)
    {
        height += y;
        y = 0;
    }
    if (x + width > win->width)
    {
        width = win->width - x;
    }
    if (y + height > win->height)
    {
        height = win->height - y;
    }
    if (width <= 0 || height <= 0)
    {
        return;
    }

    window_redraw_region(win, x, y, width, height);
}

void draw_dot(struct graphics *canvas, int x, int y, int brush_size, struct framebuffer_pixel color)
{
    graphics_draw_rect(canvas, x - brush_size / 2, y - brush_size / 2, brush_size, brush_size, color);
}

// Bresenham, stamping a dot per step so fast drags stay continuous
void draw_line(struct graphics *canvas, int x0, int y0, int x1, int y1, int brush_size, struct framebuffer_pixel color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        draw_dot(canvas, x0, y0, brush_size, color);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void toolbar_draw(struct graphics *canvas, int window_width, struct framebuffer_pixel current_color)
{
    graphics_draw_rect(canvas, 0, 0, window_width, TOOLBAR_HEIGHT, toolbar_bg);

    struct framebuffer_pixel color_undo_bg = {.red = 0xa8, .green = 0xa8, .blue = 0xa8, .reserved = 0};
    struct framebuffer_pixel color_clear_bg = {.red = 0xe6, .green = 0xb8, .blue = 0x8a, .reserved = 0};
    struct framebuffer_pixel swatch_colors[TOOLBAR_TOTAL_BTNS] = {
        color_red, color_green, color_blue, color_ink, color_white, color_undo_bg, color_clear_bg};

    int y = toolbar_btn_y();
    for (int i = 0; i < TOOLBAR_TOTAL_BTNS; i++)
    {
        int x = toolbar_btn_x(i);
        graphics_draw_rect(canvas, x, y, TOOLBAR_BTN_SIZE, TOOLBAR_BTN_SIZE, swatch_colors[i]);
        graphics_draw_rect(canvas, x, y, TOOLBAR_BTN_SIZE, 2, toolbar_border);
        graphics_draw_rect(canvas, x, y + TOOLBAR_BTN_SIZE - 2, TOOLBAR_BTN_SIZE, 2, toolbar_border);
        graphics_draw_rect(canvas, x, y, 2, TOOLBAR_BTN_SIZE, toolbar_border);
        graphics_draw_rect(canvas, x + TOOLBAR_BTN_SIZE - 2, y, 2, TOOLBAR_BTN_SIZE, toolbar_border);
    }

    // Marks the white swatch as the eraser, not "no color"
    int eraser_x = toolbar_btn_x(TOOLBAR_BTN_ERASER);
    int mark_h = TOOLBAR_BTN_SIZE / 4;
    graphics_draw_rect(canvas, eraser_x + TOOLBAR_BTN_INSET, y + (TOOLBAR_BTN_SIZE - mark_h) / 2,
                       TOOLBAR_BTN_SIZE - (TOOLBAR_BTN_INSET * 2), mark_h, eraser_mark);

    // Undo arrow: a shaft with a small back-pointing arrowhead.
    int undo_x = toolbar_btn_x(TOOLBAR_BTN_UNDO);
    int cy = y + TOOLBAR_BTN_SIZE / 2;
    int head = TOOLBAR_BTN_SIZE / 4;
    int tail = undo_x + TOOLBAR_BTN_INSET;
    draw_line(canvas, tail, cy, undo_x + TOOLBAR_BTN_SIZE - TOOLBAR_BTN_INSET, cy, 2, toolbar_border);
    draw_line(canvas, tail, cy, tail + head, cy - head, 2, toolbar_border);
    draw_line(canvas, tail, cy, tail + head, cy + head, 2, toolbar_border);

    // Clear "X" mark.
    int clear_x = toolbar_btn_x(TOOLBAR_BTN_CLEAR);
    int far = TOOLBAR_BTN_SIZE - TOOLBAR_BTN_INSET;
    draw_line(canvas, clear_x + TOOLBAR_BTN_INSET, y + TOOLBAR_BTN_INSET, clear_x + far, y + far, 2, toolbar_border);
    draw_line(canvas, clear_x + far, y + TOOLBAR_BTN_INSET, clear_x + TOOLBAR_BTN_INSET, y + far, 2, toolbar_border);

    // Highlight whichever color/eraser swatch is currently active.
    for (int i = 0; i < TOOLBAR_BTN_UNDO; i++)
    {
        if (colors_equal(swatch_colors[i], current_color))
        {
            int x = toolbar_btn_x(i);
            int ring_span = TOOLBAR_BTN_SIZE + (TOOLBAR_RING * 2);
            graphics_draw_rect(canvas, x - TOOLBAR_RING, y - TOOLBAR_RING, ring_span, TOOLBAR_RING, toolbar_border);
            graphics_draw_rect(canvas, x - TOOLBAR_RING, y + TOOLBAR_BTN_SIZE, ring_span, TOOLBAR_RING, toolbar_border);
            graphics_draw_rect(canvas, x - TOOLBAR_RING, y - TOOLBAR_RING, TOOLBAR_RING, ring_span, toolbar_border);
            graphics_draw_rect(canvas, x + TOOLBAR_BTN_SIZE, y - TOOLBAR_RING, TOOLBAR_RING, ring_span, toolbar_border);
            break;
        }
    }
}

int main(int argc, char **argv)
{
    graphics_image_formats_init();
    font_system_init();

    struct window *main_win = window_create("Draw", 560, 400, 0, 556);
    if (!main_win)
    {
        return -1;
    }

    struct graphics *canvas = (struct graphics *)window_graphics(main_win);
    if (!canvas || !graphics_get_pixel_buffer(canvas))
    {
        printf("Failed to access canvas pixels\n");
        return -1;
    }

    struct framebuffer_pixel current_color = color_ink;
    int current_brush = DRAW_BRUSH_SIZE;

    graphics_draw_rect(canvas, 0, 0, main_win->width, main_win->height, color_white);
    toolbar_draw(canvas, main_win->width, current_color);
    window_redraw(main_win);

    // One level of undo: a whole-canvas snapshot taken at stroke start
    struct framebuffer_pixel *undo_buffer = NULL;
    int undo_width = 0;
    int undo_height = 0;
    bool undo_available = false;

    bool mouse_down = false;
    bool press_on_toolbar = false;
    int prev_x = 0;
    int prev_y = 0;

    struct window_event event = {0};
    while (1)
    {
        int res = window_get_event(&event);
        if (res < 0)
        {
            usleep(10);
            continue;
        }

        switch (event.type)
        {
        case WINDOW_EVENT_TYPE_MOUSE_CLICK:
        {
            int cur_x = event.data.click.x;
            int cur_y = event.data.click.y;

            if (!mouse_down)
            {
                mouse_down = true;
                press_on_toolbar = cur_y < TOOLBAR_HEIGHT;

                if (press_on_toolbar)
                {
                    int hit = toolbar_hit_test(cur_x, cur_y);
                    switch (hit)
                    {
                    case TOOLBAR_BTN_RED:
                        current_color = color_red;
                        current_brush = DRAW_BRUSH_SIZE;
                        break;

                    case TOOLBAR_BTN_GREEN:
                        current_color = color_green;
                        current_brush = DRAW_BRUSH_SIZE;
                        break;

                    case TOOLBAR_BTN_BLUE:
                        current_color = color_blue;
                        current_brush = DRAW_BRUSH_SIZE;
                        break;

                    case TOOLBAR_BTN_BLACK:
                        current_color = color_ink;
                        current_brush = DRAW_BRUSH_SIZE;
                        break;

                    case TOOLBAR_BTN_ERASER:
                        current_color = color_white;
                        current_brush = ERASER_BRUSH_SIZE;
                        break;

                    case TOOLBAR_BTN_UNDO:
                        if (undo_available && undo_buffer)
                        {
                            int pixel_count = undo_width * undo_height;
                            memcpy(canvas->pixels, undo_buffer, pixel_count * sizeof(struct framebuffer_pixel));
                            undo_available = false;
                        }
                        break;

                    case TOOLBAR_BTN_CLEAR:
                    {
                        // Clearing counts as an undoable action too.
                        int pixel_count = canvas->width * canvas->height;
                        if (undo_width != canvas->width || undo_height != canvas->height)
                        {
                            free(undo_buffer);
                            undo_buffer = malloc(pixel_count * sizeof(struct framebuffer_pixel));
                            undo_width = canvas->width;
                            undo_height = canvas->height;
                        }
                        if (undo_buffer)
                        {
                            memcpy(undo_buffer, canvas->pixels, pixel_count * sizeof(struct framebuffer_pixel));
                            undo_available = true;
                        }

                        graphics_draw_rect(canvas, 0, TOOLBAR_HEIGHT, main_win->width, main_win->height - TOOLBAR_HEIGHT, color_white);
                        break;
                    }
                    }

                    toolbar_draw(canvas, main_win->width, current_color);
                    window_redraw_region(main_win, 0, 0, main_win->width, TOOLBAR_HEIGHT);
                    if (hit == TOOLBAR_BTN_UNDO || hit == TOOLBAR_BTN_CLEAR)
                    {
                        window_redraw_region(main_win, 0, TOOLBAR_HEIGHT, main_win->width, main_win->height - TOOLBAR_HEIGHT);
                    }
                    break;
                }

                clamp_to_canvas(main_win, &cur_x, &cur_y);

                // Snapshot for undo before the stroke changes anything
                int pixel_count = canvas->width * canvas->height;
                if (undo_width != canvas->width || undo_height != canvas->height)
                {
                    free(undo_buffer);
                    undo_buffer = malloc(pixel_count * sizeof(struct framebuffer_pixel));
                    undo_width = canvas->width;
                    undo_height = canvas->height;
                }
                if (undo_buffer)
                {
                    memcpy(undo_buffer, canvas->pixels, pixel_count * sizeof(struct framebuffer_pixel));
                    undo_available = true;
                }

                draw_dot(canvas, cur_x, cur_y, current_brush, current_color);
                prev_x = cur_x;
                prev_y = cur_y;

                int margin = current_brush + 2;
                redraw_region_clamped(main_win, cur_x - margin, cur_y - margin, margin * 2, margin * 2);
                break;
            }

            if (press_on_toolbar)
            {
                // Toolbar actions are one-shot; ignore repeats while still held.
                break;
            }

            clamp_to_canvas(main_win, &cur_x, &cur_y);

            draw_line(canvas, prev_x, prev_y, cur_x, cur_y, current_brush, current_color);

            int margin = current_brush + 2;
            int min_x = (prev_x < cur_x ? prev_x : cur_x) - margin;
            int min_y = (prev_y < cur_y ? prev_y : cur_y) - margin;
            int max_x = (prev_x > cur_x ? prev_x : cur_x) + margin;
            int max_y = (prev_y > cur_y ? prev_y : cur_y) + margin;
            redraw_region_clamped(main_win, min_x, min_y, max_x - min_x, max_y - min_y);

            prev_x = cur_x;
            prev_y = cur_y;
            break;
        }

        case WINDOW_EVENT_TYPE_MOUSE_RELEASE:
            mouse_down = false;
            break;

        case WINDOW_EVENT_TYPE_RESIZE:
            // Resize reallocates the pixel buffer elsewhere, so the old
            // mapping now points at memory that isn't the canvas.
            main_win->width = event.data.resize.width;
            main_win->height = event.data.resize.height;
            canvas->width = event.data.resize.width;
            canvas->height = event.data.resize.height;
            canvas->pixels = NULL;
            if (!graphics_get_pixel_buffer(canvas))
            {
                printf("Failed to remap canvas after resize\n");
                return -1;
            }
            mouse_down = false;
            undo_available = false;
            graphics_draw_rect(canvas, 0, 0, main_win->width, main_win->height, color_white);
            toolbar_draw(canvas, main_win->width, current_color);
            window_redraw(main_win);
            break;

        case WINDOW_EVENT_TYPE_WINDOW_CLOSE:
            return 0;
        }
    }

    return 0;
}
