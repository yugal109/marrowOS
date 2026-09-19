#ifndef USERLAND_GUI_ELEMENT_BUTTON
#define USERLAND_GUI_ELEMENT_BUTTON
struct gui;
struct gui_element;
struct font;

// A button is a child of a plane element.
struct gui_button_element_private
{
    char button_text[120];
    struct gui_element *element;
    struct font *font;
};

struct gui_element *gui_element_button_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, const char *text, int id);

#endif
