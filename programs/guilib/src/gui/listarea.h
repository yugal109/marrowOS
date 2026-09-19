#ifndef USERLAND_GUI_LISTAREA_H
#define USERLAND_GUI_LISTAREA_H
#include "vector.h"
#include "../graphics.h"

#define LISTAREA_MAX_VALUE_SIZE 256
#define LISTAREA_BORDER_WIDTH_PX 3

struct gui;

struct listarea_list_element
{
    char text[LISTAREA_MAX_VALUE_SIZE];
    void *private;
};

struct font;
struct listarea_private_data
{
    // vector of struct listarea_list_element*
    struct vector *elements;

    int selected_index;

    struct framebuffer_pixel selected_bg_color;
    struct framebuffer_pixel border_color;
    struct framebuffer_pixel font_color;

    struct font *font;
};

struct gui_element *gui_element_listarea_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, int id);
int gui_element_listarea_list_add(struct gui_element *gui_element, const char *title, void *private);
void gui_element_listarea_list_remove(struct gui_element *gui_element, int index);
size_t gui_element_listarea_height_per_element(struct gui_element *element);
struct framebuffer_pixel gui_element_listarea_selected_color(struct gui_element *element);
struct framebuffer_pixel gui_element_listarea_background_color(struct gui_element *element);
struct listarea_list_element *gui_element_listarea_list_element(struct gui_element *element, int index);
size_t gui_element_listarea_total_elements(struct gui_element *listarea_element);
struct font *gui_element_listarea_font(struct gui_element *listarea_element);
int gui_element_listarea_select(struct gui_element *listarea_element, int index);
int gui_element_listarea_selected_index(struct gui_element *listarea_element);

#endif
