#ifndef GUILIB_PROPERTY_H
#define GUILIB_PROPERTY_H

struct vector;
struct gui_element;
struct gui_event;

typedef int (*GUI_ELEMENT_PROPERTY_CLICK_HANDLER)(struct gui_element *element, int x, int y);
typedef int (*GUI_ELEMENT_PROPERTY_MOVE_HANDLER)(struct gui_element *element, int x, int y);
typedef int (*GUI_ELEMENT_PROPERTY_ATTACHED)(struct gui_element *element);
typedef int (*GUI_ELEMENT_PROPERTY_UNATTACHED)(struct gui_element *element);
typedef int (*GUI_ELEMENT_PROPERTY_EVENT_HANDLER)(struct gui_element *element, struct gui_event *event);

/**
 * e.g. a scrollable property lets an element be scrolled — captures all
 * actions taken on a given element. Elements can have multiple properties.
 */
struct gui_element_property
{
    struct gui_element *element;

    struct
    {
        GUI_ELEMENT_PROPERTY_CLICK_HANDLER click;
        GUI_ELEMENT_PROPERTY_MOVE_HANDLER move;
        GUI_ELEMENT_PROPERTY_ATTACHED attached;
        GUI_ELEMENT_PROPERTY_UNATTACHED unattached;
        GUI_ELEMENT_PROPERTY_EVENT_HANDLER event;
    } listeners;
};

int gui_element_property_attach(struct gui_element *element, struct gui_element_property *property);
int gui_element_property_unattach(struct gui_element *element, struct gui_element_property *property);

void gui_element_property_listen_click(struct gui_element_property *property, GUI_ELEMENT_PROPERTY_CLICK_HANDLER click_handler);
void gui_element_property_listen_move(struct gui_element_property *property, GUI_ELEMENT_PROPERTY_MOVE_HANDLER move_handler);
void gui_element_property_listen_event(struct gui_element_property *property, GUI_ELEMENT_PROPERTY_EVENT_HANDLER event_handler);

#endif
