#ifndef USERSPACE_GUI_EVENT_H
#define USERSPACE_GUI_EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

struct gui_event;
enum
{
    GUI_EVENT_TYPE_ELEMENT_CLICKED,
    GUI_EVENT_TYPE_ELEMENT_FOCUSED,
    GUI_EVENT_TYPE_ELEMENT_UNFOCUSED,
    GUI_EVENT_TYPE_KEYSTROKE,
    GUI_EVENT_EXIT,
    GUI_EVENT_TYPE_LISTAREA_ITEM_SELECTED,
};

enum
{
    GUI_LEFT_CLICK,
    GUI_RIGHT_CLICK,
    GUI_MIDDLE_CLICK
};

enum
{
    // Returned by event handlers that didn't handle this event
    GUI_EVENT_HANDLER_RESPONSE_ERROR = -1,
    GUI_EVENT_HANDLER_RESPONSE_IGNORED = 0,
    // Handled, but children should still receive the event
    GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN,
    // Handled, children should not receive the event
    GUI_EVENT_HANDLER_RESPONSE_PROCESSED_AND_FINISHED
};

struct gui;
struct gui_element;
struct gui_event;

typedef int GUI_EVENT_HANDLER_RESPONSE;

typedef GUI_EVENT_HANDLER_RESPONSE (*GUI_EVENT_HANDLER_FUNCTION)(struct gui_event *gui_event);
typedef bool (*GUI_EVENT_CONDITION_CHECK)(struct gui_event *gui_event, struct gui_element *recv_element, va_list va_list);

struct gui_event
{
    struct gui *gui;
    int type;
    struct
    {
        int id;
        struct gui_element *ptr;
    } element;
    union
    {
        struct gui_event_element_click
        {
            // left, right, middle mouse button
            int type;

            // Absolute window coordinates, or relative-to-element
            // coordinates if an element is set
            struct
            {
                int x;
                int y;
            } coords;
        } click;

        struct gui_event_element_focus
        {
            struct gui_element *element;
        } element_focus;

        struct gui_event_element_unfocus
        {
            struct gui_element *element;
        } element_unfocus;

        struct gui_event_element_keystroke
        {
            int key;
        } keystroke;

        struct gui_event_listarea_item_selected
        {
            int selected_index;
        } listarea_item_selected;
    } data;
};

struct gui_event *gui_event_new();
void gui_event_free(struct gui_event *gui_event);
struct gui_event *gui_event_clone(struct gui_event *event);
int gui_event_push_event_element_focus(struct gui *gui, struct gui_element *element);
int gui_event_push_event_element_unfocus(struct gui *gui, struct gui_element *element);

int gui_event_push_event_mouse_click(struct gui *gui, int window_rel_click_x, int window_rel_click_y, int type);
int gui_event_push(struct gui *gui, struct gui_event *event, GUI_EVENT_CONDITION_CHECK chck_func, ...);
void gui_event_propergate_through_children(struct gui_event *gui_event);
bool gui_event_should_continue_propergating_childern(GUI_EVENT_HANDLER_RESPONSE res);
void gui_element_event_push(struct gui_element *element, struct gui_event *event);
int gui_element_event_clone_push(struct gui_element *element, struct gui_event *event);

void gui_element_events_process(struct gui *gui);

#endif
