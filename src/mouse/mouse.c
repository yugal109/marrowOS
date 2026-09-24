#include "mouse/mouse.h"
#include "mouse/ps2mouse.h"
#include "lib/vector/vector.h"
#include "graphics/graphics.h"
#include "graphics/windows.h"
#include "graphics/image/image.h"
#include "kernel.h"
#include "status.h"

// Holds all the loaded mouse drivers
struct vector *mouse_driver_vector = NULL;

// Handlers for "every mouse" (mouse == NULL), so mice registering later get them too
static struct vector *mouse_global_click_handlers = NULL;
static struct vector *mouse_global_move_handlers = NULL;
static struct vector *mouse_global_release_handlers = NULL;

// Cursor arrow image, loaded once and reused for every redraw
static struct image *mouse_cursor_image = NULL;

int mouse_system_load_static_drivers()
{
    int res = 0;
    res = mouse_register(ps2_mouse_get());
    if (res < 0)
    {
        goto out;
    }
out:
    return res;
}

int mouse_system_init()
{
    int res = 0;
    mouse_driver_vector = vector_new(sizeof(struct mouse *), 4, 0);
    mouse_global_click_handlers = vector_new(sizeof(MOUSE_CLICK_EVENT_HANDLER_FUNCTION), 4, 0);
    mouse_global_move_handlers = vector_new(sizeof(MOUSE_MOVE_EVENT_HANDLER_FUNCTION), 4, 0);
    mouse_global_release_handlers = vector_new(sizeof(MOUSE_RELEASE_EVENT_HANDLER_FUNCTION), 4, 0);
    if (!mouse_driver_vector || !mouse_global_click_handlers || !mouse_global_move_handlers || !mouse_global_release_handlers)
    {
        res = -ENOMEM;
        goto out;
    }

out:
    return res;
}

void mouse_draw_default_impl(struct mouse *mouse)
{
    struct terminal *win_term = window_terminal(mouse->graphic.window);

    if (!mouse_cursor_image)
    {
        mouse_cursor_image = graphics_image_load("@:/cursor.bmp");
    }

    if (mouse_cursor_image)
    {
        // White is the cursor image's background; skip it so only the arrow itself draws
        struct framebuffer_pixel white_color = {0};
        white_color.red = 0xff;
        white_color.green = 0xff;
        white_color.blue = 0xff;
        terminal_ignore_color(win_term, white_color);
        terminal_draw_image(win_term, 0, 0, mouse_cursor_image);
        terminal_ignore_color_finish(win_term);
        return;
    }

    // Fallback if the cursor image failed to load
    struct framebuffer_pixel pixel_color = {0};
    pixel_color.red = 0xf3;
    terminal_draw_rect(win_term, 0, 0, win_term->bounds.width, win_term->bounds.height, pixel_color);
}

static void mouse_copy_handlers(struct vector *from, struct vector *to, size_t handler_size)
{
    size_t total = vector_count(from);
    for (size_t i = 0; i < total; i++)
    {
        void *handler = NULL;
        vector_at(from, i, &handler, handler_size);
        if (handler)
        {
            vector_push(to, &handler);
        }
    }
}

int mouse_register(struct mouse *mouse)
{
    int res = 0;
    if (!mouse_driver_vector)
    {
        panic("Mouse system was not initialized yet\n");
    }

    if (!mouse)
    {
        res = -EINVARG;
        goto out;
    }

    mouse->event_handlers.click_handlers = vector_new(sizeof(MOUSE_CLICK_EVENT_HANDLER_FUNCTION), 4, 0);
    if (!mouse->event_handlers.click_handlers)
    {
        res = -ENOMEM;
        goto out;
    }

    mouse->event_handlers.move_handlers = vector_new(sizeof(MOUSE_MOVE_EVENT_HANDLER_FUNCTION), 4, 0);
    if (!mouse->event_handlers.move_handlers)
    {
        res = -ENOMEM;
        goto out;
    }

    mouse->event_handlers.release_handlers = vector_new(sizeof(MOUSE_RELEASE_EVENT_HANDLER_FUNCTION), 4, 0);
    if (!mouse->event_handlers.release_handlers)
    {
        res = -ENOMEM;
        goto out;
    }

    res = mouse->init(mouse);
    if (res < 0)
    {
        goto out;
    }

    struct graphics_info *screen_graphics = graphics_screen_info();
    mouse->coords.x = screen_graphics->width / 2;
    mouse->coords.y = screen_graphics->height / 2;
    if (!mouse->draw)
    {
        mouse->draw = mouse_draw_default_impl;
        mouse->graphic.width = MOUSE_GRAPHIC_DEFAULT_WIDTH;
        mouse->graphic.height = MOUSE_GRAPHIC_DEFAULT_HEIGHT;
    }

    if (mouse->graphic.width <= 0 ||
        mouse->graphic.height <= 0)
    {
        res = -EINVARG;
        goto out;
    }

    if (!mouse->graphic.window)
    {
        mouse->graphic.window = window_create(screen_graphics, NULL, "", mouse->coords.x, mouse->coords.y, mouse->graphic.width, mouse->graphic.height, WINDOW_FLAG_BACKGROUND_TRANSPARENT | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_CLICK_THROUGH, -1);
        window_set_z_index(mouse->graphic.window, MOUSE_GRAPHIC_ZINDEX);
    }

    mouse->draw(mouse);

    mouse_copy_handlers(mouse_global_click_handlers, mouse->event_handlers.click_handlers, sizeof(MOUSE_CLICK_EVENT_HANDLER_FUNCTION));
    mouse_copy_handlers(mouse_global_move_handlers, mouse->event_handlers.move_handlers, sizeof(MOUSE_MOVE_EVENT_HANDLER_FUNCTION));
    mouse_copy_handlers(mouse_global_release_handlers, mouse->event_handlers.release_handlers, sizeof(MOUSE_RELEASE_EVENT_HANDLER_FUNCTION));

    vector_push(mouse_driver_vector, &mouse);
out:
    return res;
}

void mouse_position_set(struct mouse *mouse, size_t x, size_t y)
{
    mouse->coords.x = x;
    mouse->coords.y = y;
    window_position_set(mouse->graphic.window, x, y);
}

void mouse_click(struct mouse *mouse, MOUSE_CLICK_TYPE type)
{
    // Loop through every click handler and invoke it
    size_t total_click_handlers = vector_count(mouse->event_handlers.click_handlers);
    for (size_t i = 0; i < total_click_handlers; i++)
    {
        MOUSE_CLICK_EVENT_HANDLER_FUNCTION click_handler = NULL;
        vector_at(mouse->event_handlers.click_handlers, i, &click_handler, sizeof(click_handler));
        if (click_handler)
        {
            click_handler(mouse, mouse->coords.x, mouse->coords.y, type);
        }
    }
}

void mouse_released(struct mouse *mouse, MOUSE_CLICK_TYPE type)
{
    // Loop through every release handler and invoke it
    size_t total_release_handlers = vector_count(mouse->event_handlers.release_handlers);
    for (size_t i = 0; i < total_release_handlers; i++)
    {
        MOUSE_RELEASE_EVENT_HANDLER_FUNCTION release_handler = NULL;
        vector_at(mouse->event_handlers.release_handlers, i, &release_handler, sizeof(release_handler));
        if (release_handler)
        {
            release_handler(mouse, mouse->coords.x, mouse->coords.y, type);
        }
    }
}

void mouse_moved(struct mouse *mouse)
{
    size_t total_move_handlers = vector_count(mouse->event_handlers.move_handlers);
    for (size_t i = 0; i < total_move_handlers; i++)
    {
        MOUSE_MOVE_EVENT_HANDLER_FUNCTION move_handler = NULL;
        vector_at(mouse->event_handlers.move_handlers, i, &move_handler, sizeof(move_handler));
        if (move_handler)
        {
            move_handler(mouse, mouse->coords.x, mouse->coords.y);
        }
    }
}

void mouse_unregister_move_handler(struct mouse *mouse, MOUSE_MOVE_EVENT_HANDLER_FUNCTION move_handler)
{
    if (mouse)
    {
        vector_pop_element(mouse->event_handlers.move_handlers, &move_handler, sizeof(move_handler));
        return;
    }

    vector_pop_element(mouse_global_move_handlers, &move_handler, sizeof(move_handler));

    size_t total_mice = vector_count(mouse_driver_vector);
    for (size_t i = 0; i < total_mice; i++)
    {
        struct mouse *_mouse = NULL;
        vector_at(mouse_driver_vector, i, &_mouse, sizeof(_mouse));
        if (_mouse)
        {
            mouse_unregister_move_handler(_mouse, move_handler);
        }
    }
}

void mouse_unregister_click_handler(struct mouse *mouse, MOUSE_CLICK_EVENT_HANDLER_FUNCTION click_handler)
{
    if (mouse)
    {
        vector_pop_element(mouse->event_handlers.click_handlers, &click_handler, sizeof(click_handler));
        return;
    }

    vector_pop_element(mouse_global_click_handlers, &click_handler, sizeof(click_handler));

    size_t total_mice = vector_count(mouse_driver_vector);
    for (size_t i = 0; i < total_mice; i++)
    {
        struct mouse *_mouse = NULL;
        vector_at(mouse_driver_vector, i, &_mouse, sizeof(_mouse));
        if (_mouse)
        {
            mouse_unregister_click_handler(_mouse, click_handler);
        }
    }
}

void mouse_register_move_handler(struct mouse *mouse, MOUSE_MOVE_EVENT_HANDLER_FUNCTION move_handler)
{
    if (mouse)
    {
        vector_push(mouse->event_handlers.move_handlers, &move_handler);
        return;
    }

    vector_push(mouse_global_move_handlers, &move_handler);

    size_t total_mice = vector_count(mouse_driver_vector);
    for (size_t i = 0; i < total_mice; i++)
    {
        struct mouse *_mouse = NULL;
        vector_at(mouse_driver_vector, i, &_mouse, sizeof(_mouse));
        if (_mouse)
        {
            mouse_register_move_handler(_mouse, move_handler);
        }
    }
}

void mouse_register_release_handler(struct mouse *mouse, MOUSE_RELEASE_EVENT_HANDLER_FUNCTION release_handler)
{
    if (mouse)
    {
        vector_push(mouse->event_handlers.release_handlers, &release_handler);
        return;
    }

    vector_push(mouse_global_release_handlers, &release_handler);

    size_t total_mice = vector_count(mouse_driver_vector);
    for (size_t i = 0; i < total_mice; i++)
    {
        struct mouse *_mouse = NULL;
        vector_at(mouse_driver_vector, i, &_mouse, sizeof(_mouse));
        if (_mouse)
        {
            mouse_register_release_handler(_mouse, release_handler);
        }
    }
}

void mouse_register_click_handler(struct mouse *mouse, MOUSE_CLICK_EVENT_HANDLER_FUNCTION click_handler)
{
    if (mouse)
    {
        vector_push(mouse->event_handlers.click_handlers, &click_handler);
        return;
    }

    vector_push(mouse_global_click_handlers, &click_handler);

    size_t total_mice = vector_count(mouse_driver_vector);
    for (size_t i = 0; i < total_mice; i++)
    {
        struct mouse *_mouse = NULL;
        vector_at(mouse_driver_vector, i, &_mouse, sizeof(_mouse));
        if (_mouse)
        {
            mouse_register_click_handler(_mouse, click_handler);
        }
    }
}

void mouse_draw(struct mouse *mouse)
{
    mouse->draw(mouse);
}
