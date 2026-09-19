#include "window.h"
#include "marrowos.h"

void *window_graphics(struct window *window)
{
    return marrowos_window_get_graphics(window);
}

void window_redraw(struct window *window)
{
    marrowos_window_redraw(window);
}

void window_redraw_region(struct window *window, int rect_x, int rect_y, int rect_width, int rect_height)
{
    marrowos_window_redraw_region(rect_x, rect_y, rect_width, rect_height, window);
}

struct window *window_create(const char *title, int width, int height, int flags, int id)
{
    struct window *win = marrowos_window_create(title, width, height, flags, id);
    return win;
}

void window_title_set(struct window *window, const char *title)
{
    marrowos_window_title_set(window, title);
}

int window_get_event(struct window_event *event_out)
{
    return marrowos_process_get_window_event(event_out);
}

void window_set_to_receive_stdout(struct window *win)
{
    marrowos_divert_stdout_to_window(win);
}
