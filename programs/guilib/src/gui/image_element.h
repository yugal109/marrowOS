#ifndef USERLAND_GUI_IMAGE_ELEMENT_H
#define USERLAND_GUI_IMAGE_ELEMENT_H

struct gui;
struct gui_element;
struct image;
struct userland_gui_image_element_private
{
    struct image *loaded_image;
    struct gui_element *element;
};

struct gui_element *gui_element_image_create(struct gui *gui, struct image *img, struct gui_element *parent, int x, int y, int width, int height);

#endif
