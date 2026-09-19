#include "scrollable.h"
#include "memory.h"
#include "stdlib.h"
#include "gui/element.h"
#include "status.h"

void gui_element_scrollable_draw(struct gui_element *elem)
{
    // TODO: draw
}

void gui_element_scrollable_free(struct gui_element *elem)
{
    struct gui_scrollable_element_private *private_data = elem->private;
    if (!private_data)
        return;

    free(private_data);
}

struct gui_element *gui_element_scrollable_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, int id)
{
    int res = 0;
    struct gui_element *element = gui_element_create(gui, parent, x, y, width, height, gui_element_scrollable_draw, gui_element_scrollable_free, id);
    if (!element)
    {
        res = -ENOMEM;
        goto out;
    }

    struct gui_scrollable_element_private *private_data = calloc(1, sizeof(struct gui_scrollable_element_private));
    if (!private_data)
    {
        res = -ENOMEM;
        goto out;
    }

    private_data->scroll_y = 0;

out:
    if (res < 0)
    {
        if (element->private)
        {
            free(element->private);
            element->private = NULL;
        }
        gui_element_free(element);
        element = NULL;
    }
    return element;
}
