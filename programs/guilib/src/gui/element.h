#ifndef USERLAND_GUI_ELEMENT_H
#define USERLAND_GUI_ELEMENT_H
#include "vector.h"

// Forward declaration cycle with event.h — keep this include order.
#include "event.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct gui;
struct gui_element;
struct framebuffer_pixel;
struct graphics;

typedef void (*GUI_ELEMENT_DRAW)(struct gui_element *elem);
typedef void (*GUI_ELEMENT_FREE)(struct gui_element *elem);

enum
{
    GUI_ELEMENT_REDRAW_REQUIRED = 0b00000001,
    GUI_ELEMENT_IS_ROOT_ELEMENT = 0b00000010,
    // Pixels allowed to be larger than the element's own dimensions —
    // a scrollable section exists when set.
    GUI_ELEMENT_IS_SCROLLABLE = 0b00000100,
};

struct gui_element
{
    int id;
    int flags;

    // Absolute, but relative to the window body
    int x;
    int y;

    // Relative to the parent gui_element
    int rel_x;
    int rel_y;

    // Relative to the oldest grandparent (the root element registered
    // with the gui struct)
    int rel_root_x;
    int rel_root_y;

    int width;
    int height;

    // Max width/height from the kernel's perspective — no element may
    // overflow this or it crashes the process
    int real_width;
    int real_height;

    struct
    {
        GUI_ELEMENT_DRAW draw;
        GUI_ELEMENT_FREE free;
        GUI_EVENT_HANDLER_FUNCTION event_handler;
    } functions;

    // The graphics entity for this element
    struct graphics *graphics;

    struct framebuffer_pixel *pixels;

    struct gui_element *parent;

    // vector of struct gui_element*
    struct vector *children;

    // vector of struct gui_element_property*
    struct vector *properties;

    struct gui *gui;

    void *private;
};

bool gui_element_should_redraw(struct gui_element *element);
void gui_element_mark_for_redraw(struct gui_element *element);
void gui_element_redrawn(struct gui_element *element);

void gui_element_draw(struct gui_element *element);
struct gui_element *gui_element_parent(struct gui_element *element);

/**
 * Gets the top most element given a nested element of children — if you
 * are the top most element, you are returned.
 */
struct gui_element *gui_element_root(struct gui_element *element);

struct gui_element *gui_element_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, GUI_ELEMENT_DRAW draw_function, GUI_ELEMENT_FREE free_function, int id);

bool gui_element_bounds_check(struct gui_element *element, int x, int y);
void gui_element_draw_rect(struct gui_element *element, int x, int y, int width, int height, struct framebuffer_pixel *pixel_color);
void gui_element_draw_pixel(struct gui_element *element, int x, int y, struct framebuffer_pixel *pixel);
int gui_element_pixel_get(struct gui_element *element, int x, int y, struct framebuffer_pixel *pixel_out);

/**
 * Only call from within a draw function.
 */
void gui_element_draw_border(struct gui_element *gui_element, int border_width, struct framebuffer_pixel *color);

/**
 * Once added to the gui, ownership belongs to the gui — do not free after
 * that point.
 */
void gui_element_free(struct gui_element *element);

void gui_element_private_set(struct gui_element *element, void *private);

/**
 * Checks whether a gui element exists at the given position, relative to
 * the window position.
 */
bool gui_element_exists_in_position(struct gui_element *element, int x, int y);
void gui_element_event_handler_set(struct gui_element *element, GUI_EVENT_HANDLER_FUNCTION handler_func);

// Public since callers might want to invoke the parent handler
GUI_EVENT_HANDLER_RESPONSE gui_element_event_handler(struct gui_event *gui_event);

#endif
