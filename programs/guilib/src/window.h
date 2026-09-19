#ifndef GUILIB_WINDOW_H
#define GUILIB_WINDOW_H

// struct window_event and the WINDOW_EVENT_TYPE_* enum are defined once in
// marrowos.h — reuse that definition instead of duplicating it here.
#include "marrowos.h"

#define WINDOW_MAX_TITLE 128

// Userspace window handle — much simpler than the kernel-side struct window.
struct window
{
    char title[WINDOW_MAX_TITLE];
    int width;
    int height;
};

void *window_graphics(struct window *window);
struct window *window_create(const char *title, int width, int height, int flags, int id);
int window_get_event(struct window_event *event_out);
void window_title_set(struct window *window, const char *title);
void window_set_to_receive_stdout(struct window *win);
void window_redraw(struct window *window);
void window_redraw_region(struct window *window, int rect_x, int rect_y, int rect_width, int rect_height);
struct window *window_focused();

#endif
