#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include "lib/vector/vector.h"
#define MOUSE_GRAPHIC_DEFAULT_WIDTH 16
#define MOUSE_GRAPHIC_DEFAULT_HEIGHT 16
#define MOUSE_GRAPHIC_ZINDEX 100000

enum
{
    MOUSE_NO_CLICK,
    MOUSE_LEFT_BUTTON_CLICKED,
    MOUSE_RIGHT_BUTTON_CLICKED,
    MOUSE_MIDDLE_BUTTON_CLICKED,
};

typedef int MOUSE_CLICK_TYPE;

struct mouse;
typedef int (*MOUSE_INIT_FUNCTION)(struct mouse *mouse);
typedef void (*MOUSE_DRAW_FUNCTION)(struct mouse *mouse);

typedef void (*MOUSE_CLICK_EVENT_HANDLER_FUNCTION)(struct mouse *mouse, int clicked_x, int clicked_y, MOUSE_CLICK_TYPE type);
typedef void (*MOUSE_MOVE_EVENT_HANDLER_FUNCTION)(struct mouse *mouse, int moved_to_x, int moved_to_y);
// Fired once when a button that was held goes up (the opposite edge of a click)
typedef void (*MOUSE_RELEASE_EVENT_HANDLER_FUNCTION)(struct mouse *mouse, int released_x, int released_y, MOUSE_CLICK_TYPE type);
// delta > 0 is wheel up, delta < 0 is wheel down
typedef void (*MOUSE_SCROLL_EVENT_HANDLER_FUNCTION)(struct mouse *mouse, int scrolled_at_x, int scrolled_at_y, int delta);

struct window;
struct mouse
{
    MOUSE_INIT_FUNCTION init;
    MOUSE_DRAW_FUNCTION draw;
    char name[20];
    struct
    {
        // Current coordinates where the mouse graphic is on the screen
        int x;
        int y;
    } coords;

    /// mouse graphics
    struct
    {
        struct window *window;
        int width;
        int height;
    } graphic;

    struct
    {
        // vector of MOUSE_CLICK_EVENT_HANDLER_FUNCTION
        struct vector *click_handlers;
        // Vector of MOUSE_MOVE_EVENT_HANDLER_FUNCTION
        struct vector *move_handlers;
        // Vector of MOUSE_RELEASE_EVENT_HANDLER_FUNCTION
        struct vector *release_handlers;
        // Vector of MOUSE_SCROLL_EVENT_HANDLER_FUNCTION
        struct vector *scroll_handlers;
    } event_handlers;

    // this is the pirvate data for the mouse instance
    void *private;
};

int mouse_system_load_static_drivers();
void mouse_draw(struct mouse *mouse);
void mouse_register_click_handler(struct mouse *mouse, MOUSE_CLICK_EVENT_HANDLER_FUNCTION click_handler);
void mouse_register_move_handler(struct mouse *mouse, MOUSE_MOVE_EVENT_HANDLER_FUNCTION move_handler);
void mouse_register_release_handler(struct mouse *mouse, MOUSE_RELEASE_EVENT_HANDLER_FUNCTION release_handler);
void mouse_register_scroll_handler(struct mouse *mouse, MOUSE_SCROLL_EVENT_HANDLER_FUNCTION scroll_handler);
void mouse_unregister_click_handler(struct mouse *mouse, MOUSE_CLICK_EVENT_HANDLER_FUNCTION click_handler);
void mouse_unregister_move_handler(struct mouse *mouse, MOUSE_MOVE_EVENT_HANDLER_FUNCTION move_handler);
void mouse_unregister_scroll_handler(struct mouse *mouse, MOUSE_SCROLL_EVENT_HANDLER_FUNCTION scroll_handler);
void mouse_moved(struct mouse *mouse);
void mouse_click(struct mouse *mouse, MOUSE_CLICK_TYPE type);
void mouse_released(struct mouse *mouse, MOUSE_CLICK_TYPE type);
void mouse_scrolled(struct mouse *mouse, int delta);
void mouse_position_set(struct mouse *mouse, size_t x, size_t y);
int mouse_register(struct mouse *mouse);
int mouse_system_init();

#endif
