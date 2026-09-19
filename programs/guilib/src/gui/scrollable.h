#ifndef GUI_SCROLLABLE_ELEMENT_H
#define GUI_SCROLLABLE_ELEMENT_H

struct gui_element;
struct gui_scrollable_element_private
{
    int scroll_y;
};

struct gui;
struct gui_element *gui_element_scrollable_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, int id);

#endif
