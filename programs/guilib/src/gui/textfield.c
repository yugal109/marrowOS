#include "textfield.h"
#include "element.h"
#include "plane.h"
#include "gui.h"
#include "font.h"
#include "status.h"
#include "memory.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"

int gui_element_textfield_realloc_text(struct gui_element *element, size_t new_size);

void gui_element_textfield_put_char(struct gui_element *element, char c)
{
    struct textfield_private_data *private_data = element->private;

    // -1 since indexes start at zero
    if (private_data->index >= private_data->text.max_allowed_len - 1)
    {
        // Reached the maximum allowed length
        return;
    }
    if (private_data->index >= private_data->text.current_allocated_len - 1)
    {
        size_t new_size = private_data->text.current_allocated_len + TEXTFIELD_DEFAULT_BUFFER_SIZE;
        int res = gui_element_textfield_realloc_text(element, new_size);
        if (res < 0)
        {
            return;
        }
    }
    private_data->text.text[private_data->index] = c;
    private_data->index++;
    if (private_data->index >= private_data->text.current_len - 1)
    {
        private_data->text.current_len = private_data->index + 1;
    }

    gui_element_mark_for_redraw(element);
}

void gui_element_textfield_clear(struct gui_element *element)
{
    struct textfield_private_data *private_data = element->private;
    private_data->index = 0;
    private_data->text.current_len = 0;
    memset(private_data->text.text, 0x00, private_data->text.current_allocated_len);
}

void gui_element_textfield_cursor_set(struct gui_element *element, int index)
{
    struct textfield_private_data *private_data = element->private;
    private_data->index = index;
}

void gui_element_textfield_text_set(struct gui_element *element, const char *text)
{
    gui_element_textfield_clear(element);
    const char *ptr = text;
    while (*ptr)
    {
        gui_element_textfield_put_char(element, (char)*ptr);
        ptr++;
    }
}

const char *gui_element_textfield_text(struct gui_element *element)
{
    struct textfield_private_data *private_data = element->private;
    return private_data->text.text;
}

void gui_element_textfield_backspace(struct gui_element *element)
{
    struct textfield_private_data *private_data = element->private;
    if (private_data->index == 0)
    {
        return;
    }

    private_data->text.text[private_data->index - 1] = 0x00;
    private_data->index--;

    gui_element_mark_for_redraw(element);
}

GUI_EVENT_HANDLER_RESPONSE gui_element_textfield_event_handler(struct gui_event *gui_event)
{
    if (gui_event->type != GUI_EVENT_TYPE_KEYSTROKE)
    {
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;
    }

    struct gui_element *element = gui_event->element.ptr;
    if (gui_element_textfield_read_only(element))
    {
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;
    }

    char key = (char)gui_event->data.keystroke.key;
    if (key == GUI_KEY_BACKSPACE)
    {
        gui_element_textfield_backspace(element);
        return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN;
    }

    gui_element_textfield_put_char(element, key);
    return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN;
}

void gui_element_textfield_draw(struct gui_element *gui_element)
{
    struct textfield_private_data *private = gui_element->private;

    struct font *font = private->font;
    if (!font)
        return;

    int x_pos = gui_element->width / 2;

    // Default alignment is TOP, with y_pos zero
    int y_pos = TEXTFIELD_PADDING;
    if (private->text_alignment.vertical == GUI_TEXTFIELD_VERTICAL_ALIGNMENT_CENTER)
    {
        y_pos = gui_element->height / 2;
    }

    // Read-only textfields draw a slightly darker background
    if (gui_element_textfield_read_only(gui_element))
    {
        struct framebuffer_pixel bg_color = {0};
        struct gui_element *plane_parent = gui_element->parent;
        bg_color = gui_element_plane_bg_color_get(plane_parent);
        bg_color.red -= 10;
        bg_color.blue -= 10;
        bg_color.green -= 10;

        gui_element_draw_rect(gui_element, 0, 0, gui_element->width, gui_element->height, &bg_color);
    }

    x_pos = TEXTFIELD_PADDING;

    struct framebuffer_pixel colour = private->text.color;

    if (private->flags & GUI_TEXTFIELD_IS_MULTILINE_FLAG)
    {
        font_draw_text_wrap(gui_element->graphics, NULL, x_pos, y_pos, gui_element->width - TEXTFIELD_PADDING, gui_element->height, private->text.text, colour);
    }
    else
    {
        font_draw_text(gui_element->graphics, NULL, x_pos, y_pos, private->text.text, colour);
    }
}

void gui_element_textfield_free(struct gui_element *gui_element)
{
    free(gui_element->private);
}

bool gui_element_textfield_read_only(struct gui_element *element)
{
    struct textfield_private_data *private_data = element->private;
    return private_data->flags & GUI_TEXTFIELD_IS_READ_ONLY_FLAG;
}

void gui_element_textfield_read_only_set(struct gui_element *element, bool read_only)
{
    struct textfield_private_data *private_data = element->private;
    private_data->flags &= ~GUI_TEXTFIELD_IS_READ_ONLY_FLAG;
    if (read_only)
    {
        private_data->flags |= GUI_TEXTFIELD_IS_READ_ONLY_FLAG;
    }
    gui_element_mark_for_redraw(element);
}

void gui_element_textfield_text_vertical_alignment_set(struct gui_element *element, GUI_TEXTFIELD_ALIGNMENT alignment)
{
    struct textfield_private_data *private_data = element->private;
    private_data->text_alignment.vertical = alignment;
}

int gui_element_textfield_realloc_text(struct gui_element *element, size_t new_size)
{
    int res = 0;
    struct textfield_private_data *private_data = element->private;
    // Reallocating to a smaller size is allowed too; the max-size check
    // happens elsewhere, keeping this a plain resize.
    if (private_data->text.text == NULL)
    {
        private_data->text.text = calloc(1, new_size);
        private_data->text.current_len = 0;
        private_data->text.current_allocated_len = new_size;
    }
    else if (private_data->text.current_allocated_len != new_size)
    {
        private_data->text.text = realloc(private_data->text.text, new_size);
        private_data->text.current_allocated_len = new_size;
        if (private_data->text.current_len >= private_data->text.current_allocated_len)
        {
            private_data->text.current_len = private_data->text.current_allocated_len - 1;
            private_data->index = private_data->text.current_len - 1;
        }
    }

    gui_element_mark_for_redraw(element);

    return res;
}

void gui_element_text_field_max_characters_set(struct gui_element *element, size_t max_characters_allowed)
{
    struct textfield_private_data *private_data = element->private;
    private_data->text.max_allowed_len = max_characters_allowed;
}

void gui_element_textfield_color_set(struct gui_element *element, int red, int green, int blue)
{
    struct textfield_private_data *private_data = element->private;
    struct framebuffer_pixel color = {0};
    color.red = red;
    color.green = green;
    color.blue = blue;
    private_data->text.color = color;
}

void gui_element_textfield_flags_set(struct gui_element *element, int flags)
{
    struct textfield_private_data *private_data = element->private;
    private_data->flags = flags;

    gui_element_mark_for_redraw(element);
}

struct gui_element *gui_element_textfield_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, int id)
{
    int res = 0;
    struct gui_element *element = NULL;
    struct textfield_private_data *private_data = NULL;
    struct gui_element *plane_bg_element = gui_element_plane_create(gui, NULL, x, y, width, height, id);
    if (!plane_bg_element)
    {
        res = -ENOMEM;
        goto out;
    }

    element = gui_element_create(gui, plane_bg_element, 0, 0, width, height, gui_element_textfield_draw, gui_element_textfield_free, id);
    if (!element)
    {
        res = -ENOMEM;
        goto out;
    }

    // Light gray background (#d3d3d3)
    gui_element_plane_bg_color_set(plane_bg_element, 0xd3, 0xd3, 0xd3);

    private_data = calloc(1, sizeof(struct textfield_private_data));
    if (!private_data)
    {
        res = -ENOMEM;
        goto out;
    }

    private_data->font = font_get_system_font();

    // On error below, gui_element_free() also frees this private data
    gui_element_private_set(element, private_data);

    // If the element is too short, don't center the text vertically —
    // it could get clipped.
    size_t max_acceptable_font_height_for_center_alignment = private_data->font->bits_height_per_character * 2;
    if (height < max_acceptable_font_height_for_center_alignment)
    {
        gui_element_textfield_text_vertical_alignment_set(element, GUI_TEXTFIELD_VERTICAL_ALIGNMENT_TOP);
    }

    gui_element_text_field_max_characters_set(element, TEXTFIELD_DEFAULT_BUFFER_SIZE);

    // Text starts NULL, so this allocates it
    gui_element_textfield_realloc_text(element, TEXTFIELD_DEFAULT_BUFFER_SIZE);

    gui_element_event_handler_set(element, gui_element_textfield_event_handler);
out:
    if (res < 0)
    {
        if (plane_bg_element)
        {
            gui_element_free(plane_bg_element);
        }
        if (element)
        {
            gui_element_free(element);
        }
    }
    return element;
}
