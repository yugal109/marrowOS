#include "exampleproperty.h"
#include "property.h"
#include "event.h"
#include "stdio.h"

int gui_element_property_example_property_click(struct gui_element *element, int x, int y);
int gui_element_property_example_property_unattached(struct gui_element *element);
int gui_element_property_example_property_attached(struct gui_element *element);
int gui_element_property_example_property_event(struct gui_element *element, struct gui_event *event);

struct gui_element_property example_property = {
    .listeners.attached = gui_element_property_example_property_attached,
    .listeners.click = gui_element_property_example_property_click,
    .listeners.move = 0,
    .listeners.unattached = gui_element_property_example_property_unattached,
    .listeners.event = gui_element_property_example_property_event};

int gui_element_property_example_property_event(struct gui_element *element, struct gui_event *event)
{
    switch (event->type)
    {
    case GUI_EVENT_TYPE_LISTAREA_ITEM_SELECTED:
        printf("Listarea selection: element %i selected\n", event->data.listarea_item_selected.selected_index);
        break;
    }

    return 0;
}

int gui_element_property_example_property_click(struct gui_element *element, int x, int y)
{
    printf("The element was clicked at %i, %i\n", x, y);
    return 0;
}

int gui_element_property_example_property_unattached(struct gui_element *element)
{
    printf("Example property was unattached\n");
    return 0;
}

int gui_element_property_example_property_attached(struct gui_element *element)
{
    printf("Example property was attached to element\n");
    return 0;
}

int gui_element_property_example_property_attach(struct gui_element *element)
{
    return gui_element_property_attach(element, &example_property);
}
