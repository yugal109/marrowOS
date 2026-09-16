#include "marrowos.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "file.h"

struct window
{
    char title[128];
    int width;
    int height;
};

struct framebuffer_pixel
{
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
};

struct userland_graphics
{
    size_t x;
    size_t y;
    size_t width;
    size_t height;

    void *pixels;

    void *userland_ptr;
};

int main(int argc, char **argv)
{
    struct window *win = marrowos_window_create("Hello world", 600, 500, 0, 0);
    if (win)
    {
        printf("all okay\n");
    }

    marrowos_divert_stdout_to_window(win);

    while (1)
    {
        struct window_event window_event = {0};
        int res = marrowos_process_get_window_event(&window_event);
        if (res >= 0 && window_event.type == WINDOW_EVENT_TYPE_KEY_PRESS)
        {
            printf("%c", (char)window_event.data.keypress.key);
        }
        else if (res >= 0 && window_event.type == WINDOW_EVENT_TYPE_MOUSE_CLICK)
        {
            marrowos_window_cursor_set(win, window_event.data.click.x, window_event.data.click.y);
        }
    }

    return 0;
}
