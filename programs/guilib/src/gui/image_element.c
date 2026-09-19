#include "image_element.h"
#include "../image.h"
#include "../graphics.h"
#include "element.h"
#include "gui.h"
#include "memory.h"
#include "stdlib.h"
#include "stdio.h"

void gui_element_image_draw(struct gui_element *element)
{
    struct userland_gui_image_element_private *private_data = (struct userland_gui_image_element_private *)element->private;
    graphics_draw_image(element->graphics, private_data->loaded_image, element->x, element->y);
}

void gui_element_image_free(struct gui_element *element)
{
    if (element->private)
        free(element->private);
    // gui_element_free() (element.c) frees "element" itself right after
    // this callback returns — freeing it here too would double-free.
}

struct gui_element *gui_element_image_create(struct gui *gui, struct image *img, struct gui_element *parent, int x, int y, int width, int height)
{
    int res = 0;
    struct gui_element *element = gui_element_create(gui, NULL, x, y, width, height, gui_element_image_draw, gui_element_image_free, 0);
    if (!element)
    {
        res = -1;
        goto out;
    }

    struct userland_gui_image_element_private *private_data = calloc(1, sizeof(struct userland_gui_image_element_private));
    if (!private_data)
    {
        res = -1;
        goto out;
    }

    private_data->element = element;
    private_data->loaded_image = img;

    element->private = private_data;
out:
    if (res < 0)
    {
        if (element)
        {
            gui_element_free(element);
            element = NULL;
        }
    }
    return element;
}
