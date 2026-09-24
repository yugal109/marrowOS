#ifndef DRAW_PRINT_H
#define DRAW_PRINT_H

struct framebuffer_pixel;

// Called with 0-100 while data is being sent
typedef void (*print_progress_fn)(int percent);

// Sends the canvas rows from first_row down to the serial adapter as a
// run-length compressed RGB565 image. Returns bytes sent, or -1 on failure.
int print_canvas(const struct framebuffer_pixel *pixels, int width, int height, int first_row, print_progress_fn progress);

#endif
