#ifndef USERLAND_ELEMENT_PLANE_H
#define USERLAND_ELEMENT_PLANE_H
#include "../graphics.h"

struct gui;
struct gui_element;

#define GUI_PLANE_DEFAULT_FOCUSED_BORDER_WIDTH 5

enum
{
    USERLAND_PLANE_FLAG_DRAW_BORDER_ON_FOCUS = 0b00000001,
    USERLAND_PLANE_FLAG_DRAW_STANDARD_BORDER = 0b00000010,

    // When set, clicking the plane darkens it briefly then returns to normal
    USERLAND_PLANE_FLAG_DARKEN_ON_CLICK = 0b00000100
};

// Can be a parent to other objects that want to share properties.
struct userland_element_plane_private
{
    int flags;

    struct
    {
        int red;
        int blue;
        int green;
    } bg;

    // Border color when focused
    struct
    {
        struct framebuffer_pixel color;
        int width;
    } focused_border;

    // Normal, non-focused border
    struct
    {
        struct framebuffer_pixel color;
        int width;
    } border;

    struct gui_element *element;
};

struct framebuffer_pixel gui_element_plane_bg_color_get(struct gui_element *gui_element);

struct gui_element *gui_element_plane_create(struct gui *gui, struct gui_element *parent, int x, int y, int width, int height, int id);
void gui_element_plane_set_focused_border_color(struct gui_element *gui_element, int red, int green, int blue);
void gui_element_plane_bg_color_set(struct gui_element *gui_element, int red, int green, int blue);
void gui_element_plane_flag_set(struct gui_element *gui_element, int flag);
void gui_element_plane_focused_border_width_set(struct gui_element *gui_element, int pixel_width);
void gui_element_plane_set_border_color(struct gui_element *gui_element, int red, int green, int blue);
void gui_element_plane_set_border_width(struct gui_element *gui_element, int width);
bool gui_element_plane_should_draw_border(struct gui_element *element);

#endif
