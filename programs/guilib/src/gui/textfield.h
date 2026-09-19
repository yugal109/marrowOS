#ifndef USERLAND_TEXTFIELD_H
#define USERLAND_TEXTFIELD_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "graphics.h"

struct gui;
struct gui_element;
struct font;

#define TEXTFIELD_DEFAULT_BUFFER_SIZE 128

enum
{
    GUI_TEXTFIELD_VERTICAL_ALIGNMENT_TOP = 0,
    GUI_TEXTFIELD_VERTICAL_ALIGNMENT_CENTER = 1
};

enum
{
    GUI_TEXTFIELD_IS_MULTILINE_FLAG = 0b00000001,
    GUI_TEXTFIELD_IS_READ_ONLY_FLAG = 0b00000010
};

typedef int GUI_TEXTFIELD_ALIGNMENT;
#define GUI_KEY_BACKSPACE 0x08

struct textfield_private_data
{
    int flags;
    struct textfield_text_alignment
    {
        GUI_TEXTFIELD_ALIGNMENT vertical;
    } text_alignment;

    struct textfield_text
    {
        char *text;
        // Zero means the text is reallocated once bounds are exceeded
        size_t max_allowed_len;
        size_t current_allocated_len;
        size_t current_len;

        struct framebuffer_pixel color;
    } text;

    // Index where the next character will be written
    int index;

    struct font *font;
};

struct gui_element *gui_element_textfield_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, int id);

void gui_element_textfield_text_alignment_set(struct gui_element *element, GUI_TEXTFIELD_ALIGNMENT alignment);
void gui_element_textfield_flags_set(struct gui_element *element, int flags);
void gui_element_textfield_color_set(struct gui_element *element, int red, int green, int blue);
void gui_element_textfield_read_only_set(struct gui_element *element, bool read_only);
bool gui_element_textfield_read_only(struct gui_element *element);
void gui_element_textfield_cursor_set(struct gui_element *element, int index);
void gui_element_textfield_clear(struct gui_element *element);

const char *gui_element_textfield_text(struct gui_element *element);
void gui_element_textfield_text_set(struct gui_element *element, const char *text);
void gui_element_textfield_put_char(struct gui_element *element, char c);
void gui_element_textfield_backspace(struct gui_element *element);

#endif
