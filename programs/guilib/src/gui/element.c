#include "element.h"
#include "stdlib.h"
#include "memory.h"
#include "gui.h"
#include "vector.h"
#include "../graphics.h"
#include "stdio.h"
#include "property.h"
#include "status.h"

struct framebuffer_pixel;

void gui_element_focus(struct gui_element *element);

GUI_EVENT_HANDLER_RESPONSE gui_element_event_handler(struct gui_event *gui_event)
{
    int res = 0;
    struct gui_element *element = gui_event->element.ptr;
    struct gui *gui = gui_event->gui;
    if (!element)
    {
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;
    }

    if (gui_event->type == GUI_EVENT_TYPE_ELEMENT_CLICKED)
    {
        // Default behavior: clicking also focuses, unless already focused.
        if (gui_focused_element(gui) != element)
        {
            gui_element_focus(element);
        }
    }

    return res;
}

void gui_element_draw_children(struct gui_element *element)
{
    size_t total_children = vector_count(element->children);
    for (size_t i = 0; i < total_children; i++)
    {
        struct gui_element *child_elem = NULL;
        vector_at(element->children, i, &child_elem, sizeof(child_elem));
        if (child_elem)
        {
            gui_element_draw(child_elem);
        }
    }
}

void gui_element_draw(struct gui_element *element)
{
    if (!element || !element->functions.draw)
    {
        return;
    }

    element->functions.draw(element);
    gui_element_draw_children(element);
}

void gui_element_draw_border(struct gui_element *gui_element, int border_width, struct framebuffer_pixel *color)
{
    // Top
    gui_element_draw_rect(gui_element, 0, 0, gui_element->width, border_width, color);
    // Left
    gui_element_draw_rect(gui_element, 0, 0, border_width, gui_element->height, color);
    // Bottom
    gui_element_draw_rect(gui_element, 0, gui_element->height - border_width, gui_element->width, border_width, color);
    // Right
    gui_element_draw_rect(gui_element, gui_element->width - border_width, 0, border_width, gui_element->height, color);
}

bool gui_element_bounds_check(struct gui_element *element, int x, int y)
{
    if (x < 0 || x >= element->width ||
        y < 0 || y >= element->height)
    {
        return false;
    }

    return true;
}

void gui_element_focus(struct gui_element *element)
{
    gui_focus_on_element(element->gui, element);
}

// Checks if a gui element exists at the given position, relative to the
// window position.
bool gui_element_exists_in_position(struct gui_element *element, int x, int y)
{
    if (x < 0 || y < 0)
    {
        return false;
    }

    int starting_x = element->x;
    int starting_y = element->y;
    int ending_x = element->x + element->width;
    int ending_y = element->y + element->height;
    if (x >= starting_x && x <= ending_x &&
        y >= starting_y && y <= ending_y)
    {
        return true;
    }

    return false;
}

bool gui_element_should_redraw(struct gui_element *element)
{
    return element->flags & GUI_ELEMENT_REDRAW_REQUIRED;
}

void gui_element_mark_for_redraw(struct gui_element *element)
{
    element->flags |= GUI_ELEMENT_REDRAW_REQUIRED;
    gui_mark_for_redraw(element->gui);
}

void gui_element_redrawn(struct gui_element *element)
{
    element->flags &= ~GUI_ELEMENT_REDRAW_REQUIRED;
}

void gui_element_draw_rect(struct gui_element *element, int x, int y, int width, int height, struct framebuffer_pixel *pixel_color)
{
    for (int lx = 0; lx < width; lx++)
    {
        for (int ly = 0; ly < height; ly++)
        {
            gui_element_draw_pixel(element, x + lx, y + ly, pixel_color);
        }
    }
}

void gui_element_draw_pixel(struct gui_element *element, int x, int y, struct framebuffer_pixel *pixel)
{
    if (!gui_element_bounds_check(element, x, y))
    {
        return;
    }

    int true_x = x;
    int true_y = y;

    // Children need their true root-relative offset, not element->x/y
    // (which would overflow past the shared parent pixel buffer).
    if (element->parent)
    {
        true_x += element->rel_root_x;
        true_y += element->rel_root_y;
    }

    struct framebuffer_pixel *pixel_out = &(element->pixels[true_y * element->real_width + true_x]);
    memcpy(pixel_out, pixel, sizeof(struct framebuffer_pixel));
}

int gui_element_pixel_get(struct gui_element *element, int x, int y, struct framebuffer_pixel *pixel_out)
{
    if (!gui_element_bounds_check(element, x, y))
    {
        return -1;
    }

    int true_x = element->x + x;
    int true_y = element->y + y;
    struct framebuffer_pixel *pixel_in = &element->pixels[true_y * element->real_width + true_x];
    *pixel_out = *pixel_in;

    return 0;
}

// One handler per element — use a property for anything more custom.
void gui_element_event_handler_set(struct gui_element *element, GUI_EVENT_HANDLER_FUNCTION handler_func)
{
    element->functions.event_handler = handler_func;
}

struct gui_element *gui_element_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, GUI_ELEMENT_DRAW draw_function, GUI_ELEMENT_FREE free_function, int id)
{
    int res = 0;
    struct gui_element *element = calloc(1, sizeof(struct gui_element));
    if (!element)
    {
        res = -ENOMEM;
        goto out;
    }
    element->id = id;
    element->parent = parent;
    element->x = x;
    element->y = y;
    element->rel_x = x;
    element->rel_y = y;
    element->rel_root_x = 0;
    element->rel_root_y = 0;
    element->width = width;
    element->height = height;
    element->real_width = width;
    element->real_height = height;
    element->functions.draw = draw_function;
    element->functions.free = free_function;
    gui_element_event_handler_set(element, gui_element_event_handler);

    element->children = vector_new(sizeof(struct gui_element *), 8, 0);
    element->properties = vector_new(sizeof(struct gui_element_property *), 4, 0);

    element->gui = gui;

    // Only create a kernel graphics entity if we have no parent — children
    // share the parent's graphical entity instead.
    if (!parent)
    {
        element->graphics = graphics_create_relative(gui->win_graphics, x, y, width, height);
        if (!element->graphics)
        {
            res = -EIO;
            goto out;
        }
        element->pixels = graphics_get_pixel_buffer(element->graphics);
        element->flags |= GUI_ELEMENT_IS_ROOT_ELEMENT;

        vector_push(gui->elements, &element);
    }
    else
    {
        // Parent and child share the same pixel buffer.
        element->graphics = parent->graphics;
        element->pixels = parent->pixels;
        // x/y are relative to the parent's position
        element->x = parent->x + element->rel_x;
        element->y = parent->y + element->rel_y;
        element->rel_root_x = parent->rel_root_x + element->rel_x;
        element->rel_root_y = parent->rel_root_y + element->rel_y;

        // Use the parent's real dimensions for bounds safety
        element->real_width = parent->real_width;
        element->real_height = parent->real_height;

        vector_push(parent->children, &element);
    }

    if (!element->pixels)
    {
        res = -ENOMEM;
        goto out;
    }

    gui_element_mark_for_redraw(element);

out:
    if (res < 0)
    {
        if (element)
        {
            if (parent)
            {
                vector_pop_element(parent->children, &element, sizeof(element));
            }
            else
            {
                vector_pop_element(gui->elements, &element, sizeof(element));
            }
            free(element);
            element = NULL;
        }
    }
    return element;
}

void gui_element_free(struct gui_element *element)
{
    if (element->functions.free)
    {
        element->functions.free(element);
    }

    free(element);
}

void gui_element_private_set(struct gui_element *element, void *private)
{
    element->private = private;
}

struct gui_element *gui_element_parent(struct gui_element *element)
{
    return element->parent;
}

struct gui_element *gui_element_root(struct gui_element *element)
{
    struct gui_element *current_element = element;
    struct gui_element *last_element = element;
    while (current_element)
    {
        last_element = current_element;
        current_element = gui_element_parent(current_element);
    }

    return last_element;
}
