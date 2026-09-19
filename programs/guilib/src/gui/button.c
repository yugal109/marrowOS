#include "button.h"
#include "element.h"
#include "plane.h"
#include "stdlib.h"
#include "memory.h"
#include "string.h"
#include "font.h"
#include "stdio.h"

GUI_EVENT_HANDLER_RESPONSE gui_element_button_event_handler(struct gui_event *gui_event)
{
    // Mark as finished so children don't also get the response.
    return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_AND_FINISHED;
}

void gui_element_button_draw(struct gui_element *button_element)
{
    struct gui_button_element_private *private = (struct gui_button_element_private *)button_element->private;

    int x_pos = button_element->width / 2;
    int y_pos = button_element->height / 2;

    struct font *font = private->font;
    if (!font)
        return;

    // Account for text length + font size for accurate centering
    int total_characters = strlen(private->button_text);
    int text_pixel_size_width = font->bits_width_per_character * total_characters;
    int text_pixel_half_width = text_pixel_size_width / 2;
    x_pos -= text_pixel_half_width;

    if (x_pos < 0)
    {
        x_pos = 0;
    }

    struct framebuffer_pixel black = {0};
    font_draw_text(button_element->graphics, NULL, x_pos, y_pos, private->button_text, black);
}

void gui_element_button_free(struct gui_element *button_element)
{
    if (button_element->private)
    {
        free(button_element->private);
    }
}

struct gui_element *gui_element_button_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, const char *text, int id)
{
    struct gui_element *plane_element = gui_element_plane_create(gui, parent, x, y, width, height, id);
    if (!plane_element)
    {
        return NULL;
    }

    gui_element_plane_bg_color_set(plane_element, 0xAA, 0xAA, 0xAA);

    // Darken briefly on click to give a "pressed" feel
    gui_element_plane_flag_set(plane_element, USERLAND_PLANE_FLAG_DARKEN_ON_CLICK);

    // Relative (0,0) since it's positioned relative to the plane parent
    struct gui_element *btn_element = gui_element_create(gui, plane_element, 0, 0, width, height, gui_element_button_draw, gui_element_button_free, id);
    if (!btn_element)
    {
        return NULL;
    }

    struct gui_button_element_private *button_element_private_data = calloc(1, sizeof(struct gui_button_element_private));
    if (!button_element_private_data)
    {
        return NULL;
    }

    // System font by default for all buttons
    button_element_private_data->font = font_get_system_font();

    if (text)
    {
        strncpy(button_element_private_data->button_text, text, sizeof(button_element_private_data->button_text));
    }
    gui_element_private_set(btn_element, button_element_private_data);

    gui_element_event_handler_set(btn_element, gui_element_button_event_handler);

    // A button is composed of a plane and a button element
    return btn_element;
}
