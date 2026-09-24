#ifndef USERSPACE_GUI_H
#define USERSPACE_GUI_H

#include "window.h"
#include "vector.h"

// Separate header since these function pointers may be needed elsewhere
// where forward declarations are required.
#include "event.h"

enum
{
    // Set when the GUI must redraw itself fully
    GUI_FLAG_MUST_DRAW = 0b00000001,
};

struct gui_event;
struct gui_element;
struct window;
struct graphics;

struct gui
{
    int flags;

    // vector of gui_element* on this gui interface (buttons, textareas, ...)
    struct vector *elements;

    // The element in focus, taking priority for events
    struct gui_element *focused_element;

    // vector of struct gui_event*
    struct vector *gui_events;

    struct
    {
        GUI_EVENT_HANDLER_FUNCTION event;
    } handlers;

    struct graphics *win_graphics;
    struct window *window;

    // The kernel repeats clicks while held; only the first after a release is a press
    bool mouse_down;
};

/**
 * Binds a window to GUI-related actions (buttons, etc.)
 */
struct gui *gui_bind_to_window(struct window *window, GUI_EVENT_HANDLER_FUNCTION event_handler);

int gui_process(struct gui *gui);
int gui_redraw(struct gui *gui);
void gui_element_private_set(struct gui_element *gui_element, void *private_data);
void gui_mark_for_redraw(struct gui *gui);

void gui_focus_on_element(struct gui *gui, struct gui_element *element);
struct gui_element *gui_focused_element(struct gui *gui);

#endif
