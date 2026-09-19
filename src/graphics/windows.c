#include "graphics/windows.h"
#include "graphics/graphics.h"
#include "lib/vector/vector.h"
#include "memory/heap/kheap.h"
#include "keyboard/keyboard.h"
// include the mouse mouse.h
#include "memory/memory.h"
#include "string/string.h"
#include "graphics/font.h"
#include "task/process.h"
#include "io/tsc.h"
// include tsc.h
#include "status.h"
#include "kernel.h"

// vector of struct window*
struct vector *windows_vector;

// close icon image
struct image *close_icon = NULL;

// Which window is currently moving
struct window *window_moving = NULL;

// Which window currently has focus
struct window *focused_window = NULL;

int window_autoincrement_id_current = 100000;

size_t window_get_largest_zindex();
int window_recalculate_zindexes();

void window_keyboard_event_listener_on_event(struct keyboard *keyboard, struct keyboard_event *event);
struct keyboard_listener window_keyboard_listener = {
    .on_event = window_keyboard_event_listener_on_event};

int window_system_initialize()
{
    int res = 0;

    windows_vector = vector_new(sizeof(struct window *), 10, 0);
    if (!windows_vector)
    {
        res = -ENOMEM;
        goto out;
    }

    close_icon = graphics_image_load("@:/clsicon.bmp");
    if (!close_icon)
    {
        res = -EIO;
        goto out;
    }

    window_moving = NULL;
    focused_window = NULL;
    window_autoincrement_id_current = 100000;
out:
    return res;
}

// PS/2 can deliver movement packets far faster than the screen needs
// repainting; without this a drag runs a full redraw on every single
// packet. Coalescing to this interval keeps drags smooth instead of
// flooding the framebuffer with redundant repaints.
#define WINDOW_DRAG_REDRAW_INTERVAL_MS 8
static TIME_MILISECONDS window_drag_last_redraw_ms = 0;

void window_screen_mouse_move_handler(struct mouse *mouse, int moved_to_x, int moved_to_y)
{
    if (window_moving)
    {
        if (window_moving->title_bar_graphics)
        {
            TIME_MILISECONDS now_ms = tsc_miliseconds();
            if (now_ms - window_drag_last_redraw_ms >= WINDOW_DRAG_REDRAW_INTERVAL_MS)
            {
                window_drag_last_redraw_ms = now_ms;
                size_t abs_x = moved_to_x - (window_moving->title_bar_graphics->width / 2);
                size_t abs_y = moved_to_y - (window_moving->title_bar_graphics->height / 2);
                window_position_set(window_moving, abs_x, abs_y);
            }
        }

        size_t rel_x = moved_to_x - window_moving->root_graphics->starting_x;
        size_t rel_y = moved_to_y - window_moving->root_graphics->starting_y;

        struct window_event event = {0};
        event.type = WINDOW_EVENT_TYPE_MOUSE_MOVE;
        event.data.move.x = rel_x;
        event.data.move.y = rel_y;
        window_event_push(window_moving, &event);
    }
}

struct window *window_get_from_graphics(struct graphics_info *graphics)
{
    struct window *window = NULL;
    size_t total_windows = vector_count(windows_vector);
    for (size_t i = 0; i < total_windows; i++)
    {
        struct window *win = NULL;
        vector_at(windows_vector, i, &win, sizeof(win));
        if (win && window_owns_graphics(win, graphics))
        {
            window = win;
            break;
        }
    }
    return window;
};

struct window *window_get_at_position(size_t abs_x, size_t abs_y, struct window *ignore_window)
{
    size_t total_windows = vector_count(windows_vector);
    for (size_t i = 0; i < total_windows; i++)
    {
        struct window *win = NULL;
        vector_at(windows_vector, i, &win, sizeof(win));

        if (win && win != ignore_window && !win->root_graphics->hidden)
        {
            size_t whole_win_width = win->root_graphics->width;
            size_t whole_win_height = win->root_graphics->height;
            size_t end_abs_x = win->root_graphics->starting_x + whole_win_width;
            size_t end_abs_y = win->root_graphics->starting_y + whole_win_height;
            if (abs_x >= win->x && abs_x < end_abs_x && abs_y >= win->y && abs_y < end_abs_y)
            {
                // This was the window that was clicked
                return win;
            }
        }
    }

    return NULL;
}
void window_click_handler(struct mouse *mouse, int abs_x, int abs_y, MOUSE_CLICK_TYPE type)
{
    struct window *win = window_get_at_position(abs_x, abs_y, mouse->graphic.window);
    if (win)
    {
        int rel_x = abs_x - win->root_graphics->starting_x;
        int rel_y = abs_y - win->root_graphics->starting_y;
        window_click(win, rel_x, rel_y, type);
        window_focus(win);
    }
}

void window_release_handler(struct mouse *mouse, int abs_x, int abs_y, MOUSE_CLICK_TYPE type)
{
    // Whatever button was held has now come back up: whatever window was
    // being dragged (if any) gets dropped right here, wherever that is.
    window_moving = NULL;
}

// Forward decl: defined with the dock below, but hide/show need to refresh it.
void window_dock_icon_redraw();

void window_hide(struct window *window)
{
    if (!window || window->root_graphics->hidden)
    {
        return;
    }

    window->root_graphics->hidden = true;

    if (focused_window == window)
    {
        focused_window = NULL;
    }

    // Redraw its old spot now it's excluded from the tree walk.
    graphics_redraw_region(graphics_screen_info(), window->root_graphics->starting_x, window->root_graphics->starting_y, window->root_graphics->width, window->root_graphics->height);
    window_dock_icon_redraw();
}

void window_show(struct window *window)
{
    if (!window || !window->root_graphics->hidden)
    {
        return;
    }

    window->root_graphics->hidden = false;
    window_focus(window); // brings to top + colors its title bar
    window_redraw(window);
    window_dock_icon_redraw();
}

// --- Dock: bottom bar, always on top. Only the Terminal icon is clickable
// (shows/hides its window); the rest are decorative for now. ---

#define WINDOW_DOCK_HEIGHT 60
#define WINDOW_DOCK_ICON_MARGIN 16
#define WINDOW_DOCK_ICON_GAP 12
#define WINDOW_DOCK_DOT_SIZE 6
#define WINDOW_DOCK_ZINDEX 200000
#define WINDOW_DOCK_TOTAL_ICONS 5

static struct window *dock_window = NULL;
// Window each icon toggles, once its process has launched and created one.
static struct window *dock_target_windows[WINDOW_DOCK_TOTAL_ICONS] = {0};
static struct image *dock_icons[WINDOW_DOCK_TOTAL_ICONS] = {0};
static const char *dock_icon_paths[WINDOW_DOCK_TOTAL_ICONS] = {
    "@:/terminal.bmp",
    "@:/settings.bmp",
    "@:/editor.bmp",
    "@:/calc.bmp",
    "@:/music.bmp",
};
// ELF each icon launches on first click; NULL means the icon does nothing yet.
static const char *dock_program_paths[WINDOW_DOCK_TOTAL_ICONS] = {
    NULL,
    NULL,
    NULL,
    "@:/calc.elf",
    NULL,
};

size_t window_dock_icon_x(int index)
{
    // All icons are the same size, so [0]'s width works for every slot.
    size_t icon_size = dock_icons[0] ? dock_icons[0]->width : 0;
    return WINDOW_DOCK_ICON_MARGIN + (size_t)index * (icon_size + WINDOW_DOCK_ICON_GAP);
}

void window_dock_icon_redraw()
{
    if (!dock_window || !dock_icons[0])
    {
        return;
    }

    struct framebuffer_pixel white = {0};
    white.red = 0xff;
    white.green = 0xff;
    white.blue = 0xff;

    size_t icon_size = dock_icons[0]->width;
    size_t icon_y = (WINDOW_DOCK_HEIGHT - icon_size) / 2;

    terminal_ignore_color(dock_window->terminal, white);
    for (int i = 0; i < WINDOW_DOCK_TOTAL_ICONS; i++)
    {
        if (dock_icons[i])
        {
            terminal_draw_image(dock_window->terminal, window_dock_icon_x(i), icon_y, dock_icons[i]);
        }
    }
    terminal_ignore_color_finish(dock_window->terminal);

    // Dot under each icon: green if that icon's window is visible, else
    // blends into the white background.
    for (int i = 0; i < WINDOW_DOCK_TOTAL_ICONS; i++)
    {
        if (!dock_icons[i])
        {
            continue;
        }

        bool is_visible = dock_target_windows[i] && !dock_target_windows[i]->root_graphics->hidden;
        struct framebuffer_pixel dot_color = white;
        if (is_visible)
        {
            dot_color.red = 0x5b;
            dot_color.green = 0xd6;
            dot_color.blue = 0x7a;
        }

        size_t dot_x = window_dock_icon_x(i) + (icon_size / 2) - (WINDOW_DOCK_DOT_SIZE / 2);
        size_t dot_y = icon_y + icon_size + 4;
        graphics_draw_rect_rounded(dock_window->graphics, dot_x, dot_y, WINDOW_DOCK_DOT_SIZE, WINDOW_DOCK_DOT_SIZE, WINDOW_DOCK_DOT_SIZE / 2, dot_color, white);
    }

    window_redraw(dock_window);
}

void window_dock_body_clicked(struct graphics_info *graphics, size_t rel_x, size_t rel_y, MOUSE_CLICK_TYPE type)
{
    if (!dock_icons[0])
    {
        return;
    }

    size_t icon_size = dock_icons[0]->width;
    size_t icon_y = (WINDOW_DOCK_HEIGHT - icon_size) / 2;

    int clicked_slot = -1;
    for (int i = 0; i < WINDOW_DOCK_TOTAL_ICONS; i++)
    {
        size_t icon_x = window_dock_icon_x(i);
        if (rel_x >= icon_x && rel_x < icon_x + icon_size &&
            rel_y >= icon_y && rel_y < icon_y + icon_size)
        {
            clicked_slot = i;
            break;
        }
    }

    if (clicked_slot < 0)
    {
        // Empty dock space, do nothing.
        return;
    }

    if (dock_target_windows[clicked_slot])
    {
        // Already launched, toggle it.
        if (dock_target_windows[clicked_slot]->root_graphics->hidden)
        {
            window_show(dock_target_windows[clicked_slot]);
        }
        else
        {
            window_hide(dock_target_windows[clicked_slot]);
        }
    }
    else if (dock_program_paths[clicked_slot])
    {
        // First click on this icon, launch its program.
        struct process *process = NULL;
        int res = process_load_switch(dock_program_paths[clicked_slot], &process);
        if (res == MARROWOS_ALL_OK)
        {
            process->dock_slot = clicked_slot;
        }
    }

    window_dock_icon_redraw();
}

void window_dock_initialize()
{
    struct graphics_info *screen = graphics_screen_info();
    dock_window = window_create(screen, NULL, "", 0, screen->height - WINDOW_DOCK_HEIGHT, screen->width, WINDOW_DOCK_HEIGHT, WINDOW_FLAG_BORDERLESS, -1);
    if (!dock_window)
    {
        return;
    }

    for (int i = 0; i < WINDOW_DOCK_TOTAL_ICONS; i++)
    {
        dock_icons[i] = graphics_image_load(dock_icon_paths[i]);
    }

    window_set_z_index(dock_window, WINDOW_DOCK_ZINDEX);
    graphics_click_handler_set(dock_window->graphics, window_dock_body_clicked);
    window_dock_icon_redraw();
}

void window_dock_register_target_slot(int slot, struct window *target)
{
    if (slot < 0 || slot >= WINDOW_DOCK_TOTAL_ICONS)
    {
        return;
    }

    dock_target_windows[slot] = target;
    window_dock_icon_redraw();
}

int window_system_initialize_stage2()
{
    mouse_register_move_handler(NULL, window_screen_mouse_move_handler);
    mouse_register_click_handler(NULL, window_click_handler);
    mouse_register_release_handler(NULL, window_release_handler);
    keyboard_register_handler(NULL, window_keyboard_listener);
    window_dock_initialize();
    return 0;
}

struct terminal *window_terminal(struct window *window)
{
    return window->terminal;
}

// Lays out close/minimize/maximize hit-boxes. Shared by create and resize.
void window_title_bar_layout_icons(struct window *window)
{
    if (!window->title_bar_terminal)
    {
        return;
    }

    size_t bar_width = window->title_bar_terminal->bounds.width;
    size_t icon_w = close_icon->width;
    size_t icon_h = close_icon->height;
    size_t icon_y = (window->title_bar_terminal->bounds.height / 2) - (icon_h / 2);
    size_t slot = icon_w + (icon_w / 2);

    size_t close_x = bar_width - icon_w - (icon_w / 2);
    window->title_bar_components.close_btn.x = close_x;
    window->title_bar_components.close_btn.y = icon_y;
    window->title_bar_components.close_btn.width = icon_w;
    window->title_bar_components.close_btn.height = icon_h;

    // Left to right: minimize, maximize, close.
    size_t maximize_x = close_x - slot;
    window->title_bar_components.maximize_btn.x = maximize_x;
    window->title_bar_components.maximize_btn.y = icon_y;
    window->title_bar_components.maximize_btn.width = icon_w;
    window->title_bar_components.maximize_btn.height = icon_h;

    size_t minimize_x = maximize_x - slot;
    window->title_bar_components.minimize_btn.x = minimize_x;
    window->title_bar_components.minimize_btn.y = icon_y;
    window->title_bar_components.minimize_btn.width = icon_w;
    window->title_bar_components.minimize_btn.height = icon_h;
}

void window_draw_title_bar(struct window *window, struct framebuffer_pixel title_bar_bg_color)
{
    if (!window || !window->title_bar_graphics)
    {
        return;
    }

    size_t total_window_width_bounds = window->title_bar_graphics->width;
    const char *title = window->title;

    // draww the background of the title bar
    terminal_draw_rect(window->title_bar_terminal, 0, 0, total_window_width_bounds, WINDOW_TITLE_BAR_HEIGHT, title_bar_bg_color);

    // Draw the title text
    terminal_cursor_set(window->title_bar_terminal, 0, 0);
    terminal_print(window->title_bar_terminal, title);

    window_title_bar_layout_icons(window);

    struct framebuffer_pixel icon_ink = {0};
    icon_ink.red = 0x96;
    icon_ink.green = 0x96;
    icon_ink.blue = 0x96;

    // Minimize glyph: a short horizontal bar near the bottom of its slot.
    graphics_draw_rect(window->title_bar_graphics,
                       window->title_bar_components.minimize_btn.x + 2,
                       window->title_bar_components.minimize_btn.y + window->title_bar_components.minimize_btn.height - 4,
                       window->title_bar_components.minimize_btn.width - 4, 2, icon_ink);

    // Maximize glyph: a hollow square outline.
    size_t inset = 3;
    size_t ox = window->title_bar_components.maximize_btn.x + inset;
    size_t oy = window->title_bar_components.maximize_btn.y + inset;
    size_t ow = window->title_bar_components.maximize_btn.width - (inset * 2);
    size_t oh = window->title_bar_components.maximize_btn.height - (inset * 2);
    graphics_draw_rect(window->title_bar_graphics, ox, oy, ow, 2, icon_ink);          // top
    graphics_draw_rect(window->title_bar_graphics, ox, oy + oh - 2, ow, 2, icon_ink); // bottom
    graphics_draw_rect(window->title_bar_graphics, ox, oy, 2, oh, icon_ink);          // left
    graphics_draw_rect(window->title_bar_graphics, ox + ow - 2, oy, 2, oh, icon_ink); // right

    // Draw the close icon ignoring white
    struct framebuffer_pixel white_color = {0};
    white_color.red = 0xff;
    white_color.green = 0xff;
    white_color.blue = 0xff;
    terminal_ignore_color(window->title_bar_terminal, white_color);
    terminal_draw_image(window->title_bar_terminal, window->title_bar_components.close_btn.x, window->title_bar_components.close_btn.y, close_icon);
    terminal_ignore_color_finish(window->title_bar_terminal);
}

int window_reorder(void *first_elem, void *second_elem)
{
    struct window *win1 = *(struct window **)(first_elem);
    struct window *win2 = *(struct window **)(second_elem);

    return (win1->zindex < win2->zindex);
}

void window_set_z_index(struct window *window, int zindex)
{
    graphics_set_z_index(window->root_graphics, zindex);

    // We need to reorder the windows vector now that zindex changed
    vector_reorder(windows_vector, window_reorder);
}

void window_unfocus(struct window *old_focused_window)
{
    // Unfocused = muted gray, not flat black.
    struct framebuffer_pixel unfocused_title_bg = {0};
    unfocused_title_bg.red = 0x3a;
    unfocused_title_bg.green = 0x3a;
    unfocused_title_bg.blue = 0x3e;
    window_draw_title_bar(old_focused_window, unfocused_title_bg);
    graphics_redraw_region(graphics_screen_info(), old_focused_window->root_graphics->starting_x, old_focused_window->root_graphics->starting_y, old_focused_window->root_graphics->width, old_focused_window->root_graphics->height);

    struct window_event event = {0};
    event.type = WINDOW_EVENT_TYPE_FOCUS;
    window_event_push(old_focused_window, &event);
}

void window_bring_to_top(struct window *window)
{
    size_t last_index = 0;
    struct graphics_info *screen_graphics = graphics_screen_info();
    size_t child_count = vector_count(screen_graphics->children);
    if (child_count > 0)
    {
        struct graphics_info *child_graphics = NULL;
        size_t child_index = child_count - 1;
        vector_at(screen_graphics->children, child_index, &child_graphics, sizeof(child_graphics));
        if (child_graphics)
        {
            last_index = child_graphics->z_index;
        }
    }

    window_set_z_index(window, last_index + 1);
}

void window_focus(struct window *window)
{
    if (!window)
    {
        return;
    }

    if (focused_window == window)
    {
        return;
    }

    struct window *old_focused_window = focused_window;
    focused_window = window;
    // Focused = slate blue, not alarm-red.
    struct framebuffer_pixel focused_title_bg = {0};
    focused_title_bg.red = 0x3a;
    focused_title_bg.green = 0x5c;
    focused_title_bg.blue = 0x8f;

    if (old_focused_window && old_focused_window->title_bar_graphics)
    {
        window_unfocus(old_focused_window);
    }

    // Bring the new window to the top
    window_bring_to_top(window);

    // Update the new window's title bar to the focused accent color
    if (window->title_bar_graphics)
    {
        window_draw_title_bar(window, focused_title_bg);
    }

    // Force a full redraw of the window
    graphics_redraw_graphics_to_screen(window->root_graphics, 0, 0, window->root_graphics->width, window->root_graphics->height);

    struct window_event event = {0};
    event.type = WINDOW_EVENT_TYPE_FOCUS;
    window_event_push(window, &event);
}

void window_event_handler_unregister(struct window *window, WINDOW_EVENT_HANDLER handler)
{
    vector_pop_element(window->event_handlers.handlers, &handler, sizeof(handler));
}

void window_event_handler_register(struct window *window, WINDOW_EVENT_HANDLER handler)
{
    vector_push(window->event_handlers.handlers, &handler);
}

void window_drop_event_handlers(struct window *window)
{
    WINDOW_EVENT_HANDLER handler = NULL;
    vector_at(window->event_handlers.handlers, 0, &handler, sizeof(handler));
    while (handler)
    {
        // This function will pop from the handler vector
        window_event_handler_unregister(window, handler);

        vector_at(window->event_handlers.handlers, 0, &handler, sizeof(handler));
    }
}

void window_free(struct window *window)
{
    // drop the event handlers
    window_drop_event_handlers(window);
    // free the event handlers vector
    vector_free(window->event_handlers.handlers);

    // Pop the window pointer from the vector
    vector_pop_element(windows_vector, &window, sizeof(window));
    terminal_free(window->terminal);

    // free the title terminal
    terminal_free(window->title_bar_terminal);

    // Free the root graphics which will free aall children
    graphics_info_free(window->root_graphics);
    kfree(window);
}

void window_event_push(struct window *window, struct window_event *event)
{
    event->window = window;
    event->win_id = window->id;

    // Loop through all the event handlers and push the event to them
    size_t total_handlers = vector_count(window->event_handlers.handlers);
    for (size_t i = 0; i < total_handlers; i++)
    {
        WINDOW_EVENT_HANDLER handler = NULL;
        vector_at(window->event_handlers.handlers, i, &handler, sizeof(handler));
        if (handler)
        {
            handler(window, event);
        }
    }
}

void window_click(struct window *window, int rel_x, int rel_y, MOUSE_CLICK_TYPE type)
{
    struct window_event event = {0};
    event.type = WINDOW_EVENT_TYPE_MOUSE_CLICK;
    event.data.click.x = rel_x;
    event.data.click.y = rel_y;
    window_event_push(window, &event);
}

void window_close(struct window *window)
{
    struct window_event event = {0};
    event.type = WINDOW_EVENT_TYPE_WINDOW_CLOSE;
    window_event_push(window, &event);

    window_free(window);
    graphics_redraw_all();
}

int window_event_handler(struct window *window, struct window_event *win_event)
{
    // do nothing for now
    return 0;
}

int window_position_set(struct window *window, size_t new_x, size_t new_y)
{
    int res = 0;

    int x_redraw_x = 0;
    int x_redraw_y = 0;
    int x_redraw_width = 0;
    int x_redraw_height = 0;

    int y_redraw_x = 0;
    int y_redraw_y = 0;
    int y_redraw_width = 0;
    int y_redraw_height = 0;

    struct graphics_info *screen = graphics_screen_info();
    size_t ending_x = new_x + window->width;
    size_t ending_y = new_y + window->height;
    if (ending_x > screen->width)
    {
        new_x = screen->width - window->width - 1;
    }

    if (ending_y > screen->height)
    {
        new_y = screen->height - window->height - 1;
    }

    int old_screen_x = window->root_graphics->starting_x;
    int old_screen_y = window->root_graphics->starting_y;

    window->root_graphics->relative_x = new_x;
    window->root_graphics->relative_y = new_y;
    window->root_graphics->starting_x = new_x;
    window->root_graphics->starting_y = new_y;

    window->x = new_x;
    window->y = new_y;

    graphics_info_recalculate(window->root_graphics);

    int x_gap = old_screen_x - (int)window->root_graphics->starting_x;
    int y_gap = old_screen_y - (int)window->root_graphics->starting_y;
    bool moved_left = x_gap >= 0;
    bool moved_up = y_gap >= 0;
    x_redraw_x = window->root_graphics->starting_x + window->root_graphics->width;
    x_redraw_width = x_gap;
    x_redraw_y = old_screen_y;
    x_redraw_height = window->root_graphics->height;

    if (!moved_left)
    {
        x_redraw_x = window->root_graphics->starting_x + x_gap;
        // negate the x_gap
        x_redraw_width = -x_gap;
    }

    y_redraw_x = old_screen_x;
    y_redraw_y = window->root_graphics->starting_y + window->root_graphics->height;

    y_redraw_width = window->root_graphics->width;
    y_redraw_height = y_gap;
    if (!moved_up)
    {
        y_redraw_y = window->root_graphics->starting_y + y_gap;
        y_redraw_height = -y_gap;
    }

    // The strip-only erase below assumes the window's own redraw at the new
    // position will fully overwrite whatever was left behind in the overlap
    // between old and new position. That's false for a window with a
    // transparency key: its "see-through" pixels don't overwrite anything,
    // so stale pixels in that overlap never get cleared and smear into a
    // trail as the window moves. Such windows always need the full old
    // rectangle erased, not just the exposed strips.
    struct framebuffer_pixel no_transparency_color = {0};
    bool has_transparency_key = memcmp(&window->graphics->transparency_key, &no_transparency_color, sizeof(no_transparency_color)) != 0;

    if (has_transparency_key ||
        (x_redraw_width > window->root_graphics->width) ||
        (x_redraw_height > window->root_graphics->height) ||
        (y_redraw_width > window->root_graphics->width) ||
        (y_redraw_height > window->root_graphics->height))
    {
        graphics_redraw_region(graphics_screen_info(), old_screen_x, old_screen_y, window->root_graphics->width, window->root_graphics->height);
    }
    else
    {
        graphics_redraw_region(graphics_screen_info(), x_redraw_x, x_redraw_y, x_redraw_width, x_redraw_height);
        graphics_redraw_region(graphics_screen_info(), y_redraw_x, y_redraw_y, y_redraw_width, y_redraw_height);
    }

    window_redraw(window);
out:
    return res;
}

void window_redraw(struct window *window)
{
    graphics_redraw(window->root_graphics);
}

void window_redraw_body_region(struct window *window, int x, int y, int width, int height)
{
    graphics_redraw_region(window->graphics, x, y, width, height);
}

void window_redraw_region(struct window *window, int x, int y, int width, int height)
{
    graphics_redraw_region(window->root_graphics, x, y, width, height);
}

void window_title_set(struct window *window, const char *title)
{
    strncpy(window->title, title, sizeof(window->title));

    // Keep its current focus color instead of resetting to black.
    struct framebuffer_pixel title_bar_bg_color = {0};
    if (window == focused_window)
    {
        title_bar_bg_color.red = 0x3a;
        title_bar_bg_color.green = 0x5c;
        title_bar_bg_color.blue = 0x8f;
    }
    else
    {
        title_bar_bg_color.red = 0x3a;
        title_bar_bg_color.green = 0x3a;
        title_bar_bg_color.blue = 0x3e;
    }

    window_draw_title_bar(window, title_bar_bg_color);
    window_redraw(window);
}

size_t window_get_largest_zindex()
{
    size_t z_index = 0;
    size_t total_windows = vector_count(windows_vector);
    if (total_windows > 0)
    {
        struct window *win = NULL;
        vector_at(windows_vector, 0, &win, sizeof(win));
        if (win)
        {
            z_index = win->zindex;
        }
    }

    return z_index;
}

int window_recalculate_zindexes()
{
    size_t total_windows = vector_count(windows_vector);
    size_t last_zindex = 0;
    for (size_t i = 0; i < total_windows; i++)
    {
        struct window *child_window = NULL;
        vector_at(windows_vector, i, &child_window, sizeof(child_window));
        if (child_window)
        {
            size_t z_index = vector_count(child_window->root_graphics->children) + i + 1;
            graphics_set_z_index(child_window->root_graphics, z_index);
            last_zindex = z_index;
        }
    }

    return last_zindex;
}

struct window *window_focused()
{
    return focused_window;
}

bool window_owns_graphics(struct window *win, struct graphics_info *graphics)
{
    if (graphics == win->root_graphics)
        return true;

    return graphics_has_ancestor(graphics, win->root_graphics);
}

void window_title_bar_mouse_moved(struct graphics_info *title_graphics, size_t rel_x, size_t rel_y, size_t abs_x, size_t abs_y)
{
    // do nothing
}

bool window_title_bar_hit_test(size_t btn_x, size_t btn_y, size_t btn_width, size_t btn_height, size_t rel_x, size_t rel_y)
{
    return rel_x >= btn_x && rel_x < btn_x + btn_width &&
           rel_y >= btn_y && rel_y < btn_y + btn_height;
}

// Resizes/repositions a window: reallocates all its buffers, then redraws it.
void window_resize(struct window *window, size_t new_x, size_t new_y, size_t new_width, size_t new_height)
{
    if (!window)
    {
        return;
    }

    size_t old_screen_x = window->root_graphics->starting_x;
    size_t old_screen_y = window->root_graphics->starting_y;
    size_t old_total_width = window->root_graphics->width;
    size_t old_total_height = window->root_graphics->height;

    bool has_chrome = !(window->flags & WINDOW_FLAG_BORDERLESS);

    size_t total_width_bounds = new_width;
    size_t total_height_bounds = new_height;
    size_t body_x_offset = 0;
    size_t body_y_offset = 0;
    if (has_chrome)
    {
        total_width_bounds += WINDOW_BORDER_PIXEL_SIZE * 2;
        total_height_bounds += WINDOW_TITLE_BAR_HEIGHT + WINDOW_BORDER_PIXEL_SIZE;
        body_y_offset = WINDOW_TITLE_BAR_HEIGHT;
        body_x_offset = WINDOW_BORDER_PIXEL_SIZE;
    }

    graphics_info_resize(window->root_graphics, new_x, new_y, total_width_bounds, total_height_bounds);
    window->x = new_x;
    window->y = new_y;
    window->width = new_width;
    window->height = new_height;

    if (has_chrome)
    {
        graphics_info_resize(window->title_bar_graphics, WINDOW_BORDER_PIXEL_SIZE, 0, new_width, WINDOW_TITLE_BAR_HEIGHT);
        graphics_info_resize(window->border_left_graphics, 0, WINDOW_TITLE_BAR_HEIGHT, WINDOW_BORDER_PIXEL_SIZE, new_height);
        graphics_info_resize(window->border_right_graphics, total_width_bounds - WINDOW_BORDER_PIXEL_SIZE, WINDOW_TITLE_BAR_HEIGHT, WINDOW_BORDER_PIXEL_SIZE, new_height);
        graphics_info_resize(window->border_bottom_graphics, 0, total_height_bounds - WINDOW_BORDER_PIXEL_SIZE, new_width, WINDOW_BORDER_PIXEL_SIZE);
    }

    graphics_info_resize(window->graphics, body_x_offset, body_y_offset, new_width, new_height);

    // abs_x/abs_y stay 0 (local offset, not screen position) — only size changes.
    window->terminal->bounds.width = new_width;
    window->terminal->bounds.height = new_height;
    window->terminal->text.row = 0;
    window->terminal->text.col = 0;

    struct framebuffer_pixel bg_color = {0};
    bg_color.red = 0xff;
    bg_color.blue = 0xff;
    bg_color.green = 0xff;
    terminal_draw_rect(window->terminal, 0, 0, new_width, new_height, bg_color);

    if (window->terminal->terminal_background)
    {
        kfree(window->terminal->terminal_background);
        window->terminal->terminal_background = NULL;
    }
    terminal_background_save(window->terminal);

    if (window->flags & WINDOW_FLAG_BACKGROUND_TRANSPARENT)
    {
        terminal_transparency_key_set(window->terminal, bg_color);
    }

    if (has_chrome)
    {
        window->title_bar_terminal->bounds.width = total_width_bounds;
        window->title_bar_terminal->bounds.height = WINDOW_TITLE_BAR_HEIGHT;

        struct framebuffer_pixel title_bar_bg_color = {0};
        if (window == focused_window)
        {
            title_bar_bg_color.red = 0x3a;
            title_bar_bg_color.green = 0x5c;
            title_bar_bg_color.blue = 0x8f;
        }
        else
        {
            title_bar_bg_color.red = 0x3a;
            title_bar_bg_color.green = 0x3a;
            title_bar_bg_color.blue = 0x3e;
        }
        window_draw_title_bar(window, title_bar_bg_color);

        struct framebuffer_pixel border_color = {0};
        border_color.red = 0x20;
        border_color.green = 0x20;
        border_color.blue = 0x24;
        graphics_draw_rect(window->border_left_graphics, 0, 0, window->border_left_graphics->width, window->border_left_graphics->height, border_color);
        graphics_draw_rect(window->border_right_graphics, 0, 0, window->border_right_graphics->width, window->border_right_graphics->height, border_color);
        graphics_draw_rect(window->border_bottom_graphics, 0, 0, window->border_bottom_graphics->width, window->border_bottom_graphics->height, border_color);
    }

    graphics_redraw_region(graphics_screen_info(), old_screen_x, old_screen_y, old_total_width, old_total_height);
    window_redraw(window);
}

void window_maximize_toggle(struct window *window)
{
    if (!window)
    {
        return;
    }

    if (!window->is_maximized)
    {
        window->saved_x = window->x;
        window->saved_y = window->y;
        window->saved_width = window->width;
        window->saved_height = window->height;

        struct graphics_info *screen = graphics_screen_info();
        bool has_chrome = !(window->flags & WINDOW_FLAG_BORDERLESS);
        size_t chrome_extra_width = has_chrome ? (WINDOW_BORDER_PIXEL_SIZE * 2) : 0;
        size_t chrome_extra_height = has_chrome ? (WINDOW_TITLE_BAR_HEIGHT + WINDOW_BORDER_PIXEL_SIZE) : 0;

        size_t available_height = screen->height - WINDOW_DOCK_HEIGHT;
        size_t new_body_width = screen->width - chrome_extra_width;
        size_t new_body_height = available_height - chrome_extra_height;

        window_resize(window, 0, 0, new_body_width, new_body_height);
        window->is_maximized = true;
    }
    else
    {
        window_resize(window, window->saved_x, window->saved_y, window->saved_width, window->saved_height);
        window->is_maximized = false;
    }
}

void window_title_bar_clicked(struct graphics_info *title_graphics, size_t rel_x, size_t rel_y, MOUSE_CLICK_TYPE type)
{
    struct window *win = window_get_from_graphics(title_graphics);
    if (win)
    {
        if (window_title_bar_hit_test(win->title_bar_components.close_btn.x, win->title_bar_components.close_btn.y,
                                      win->title_bar_components.close_btn.width, win->title_bar_components.close_btn.height, rel_x, rel_y))
        {
            window_close(win);
            win = NULL;
        }
        else if (window_title_bar_hit_test(win->title_bar_components.minimize_btn.x, win->title_bar_components.minimize_btn.y,
                                           win->title_bar_components.minimize_btn.width, win->title_bar_components.minimize_btn.height, rel_x, rel_y))
        {
            window_hide(win);
        }
        else if (window_title_bar_hit_test(win->title_bar_components.maximize_btn.x, win->title_bar_components.maximize_btn.y,
                                           win->title_bar_components.maximize_btn.width, win->title_bar_components.maximize_btn.height, rel_x, rel_y))
        {
            window_maximize_toggle(win);
        }
        else
        {
            // No dragging while maximized — nowhere to drag it to.
            window_bring_to_top(win);
            if (!win->is_maximized)
            {
                window_moving = win;
            }
        }
    }
}

struct window *window_create(struct graphics_info *graphics_info, struct font *font, const char *title, size_t x, size_t y, size_t width, size_t height, int flags, int id)
{
    int res = 0;
    if (!windows_vector)
    {
        panic("Window system was not initialized\n");
    }

    if (width < 1 || height < 1)
    {
        res = -EINVARG;
        goto out;
    }

    if (!font)
    {
        // No font default to the system font
        font = font_get_system_font();
    }

    struct window *window = kzalloc(sizeof(struct window));
    if (!window)
    {
        res = -ENOMEM;
        goto out;
    }

    if (id == -1)
    {
        id = window_autoincrement_id_current;
        window_autoincrement_id_current++;
    }

    strncpy(window->title, title, sizeof(window->title));
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->flags = flags;
    window->id = id;

    // setup event handler function pointer vector
    window->event_handlers.handlers = vector_new(sizeof(WINDOW_EVENT_HANDLER), 4, 0);

    size_t total_window_width_bounds = width;
    size_t total_window_height_bounds = height;
    size_t window_body_height_offset = 0;
    size_t window_body_width_offset = 0;

    struct graphics_info *title_bar_graphics_info = NULL;
    struct graphics_info *border_left_graphics_info = NULL;
    struct graphics_info *border_right_graphics_info = NULL;
    struct graphics_info *border_bottom_graphics_info = NULL;

    if (!(flags & WINDOW_FLAG_BORDERLESS))
    {
        total_window_width_bounds += (WINDOW_BORDER_PIXEL_SIZE * 2);
        total_window_height_bounds += WINDOW_TITLE_BAR_HEIGHT + WINDOW_BORDER_PIXEL_SIZE;
        window_body_height_offset = WINDOW_TITLE_BAR_HEIGHT;
        window_body_width_offset = WINDOW_BORDER_PIXEL_SIZE;
    }

    struct graphics_info *root_graphics_info = graphics_info_create_relative(graphics_info, x, y, total_window_width_bounds, total_window_height_bounds, GRAPHICS_FLAG_DO_NOT_COPY_PIXELS);
    if (!root_graphics_info)
    {
        res = -ENOMEM;
        goto out;
    }

    if (flags & WINDOW_FLAG_BACKGROUND_TRANSPARENT)
    {
        struct framebuffer_pixel transparency_key = {0};
        transparency_key.blue = 0xff;
        transparency_key.green = 0xff;
        transparency_key.red = 0xff;
        graphics_transparency_key_set(root_graphics_info, transparency_key);
        graphics_draw_rect(root_graphics_info, 0, 0, root_graphics_info->width, root_graphics_info->height, transparency_key);
    }

    window->root_graphics = root_graphics_info;

    if (!(flags & WINDOW_FLAG_BORDERLESS))
    {
        title_bar_graphics_info =
            graphics_info_create_relative(root_graphics_info, WINDOW_BORDER_PIXEL_SIZE, 0, width, WINDOW_TITLE_BAR_HEIGHT, 0);
        if (!title_bar_graphics_info)
        {
            res = -ENOMEM;
            goto out;
        }

        // click handler
        graphics_click_handler_set(title_bar_graphics_info, window_title_bar_clicked);

        // move handler
        graphics_move_handler_set(title_bar_graphics_info, window_title_bar_mouse_moved);

        window->title_bar_graphics = title_bar_graphics_info;

        border_left_graphics_info =
            graphics_info_create_relative(root_graphics_info, 0, WINDOW_TITLE_BAR_HEIGHT, WINDOW_BORDER_PIXEL_SIZE, height, 0);
        if (!border_left_graphics_info)
        {
            res = -ENOMEM;
            goto out;
        }

        border_right_graphics_info =
            graphics_info_create_relative(root_graphics_info, total_window_width_bounds - WINDOW_BORDER_PIXEL_SIZE, WINDOW_TITLE_BAR_HEIGHT, WINDOW_BORDER_PIXEL_SIZE, height, 0);
        if (!border_right_graphics_info)
        {
            res = -ENOMEM;
            goto out;
        }

        border_bottom_graphics_info =
            graphics_info_create_relative(root_graphics_info, 0, total_window_height_bounds - WINDOW_BORDER_PIXEL_SIZE, width, WINDOW_BORDER_PIXEL_SIZE, 0);
        if (!border_bottom_graphics_info)
        {
            res = -ENOMEM;
            goto out;
        }

        window->border_left_graphics = border_left_graphics_info;
        window->border_right_graphics = border_right_graphics_info;
        window->border_bottom_graphics = border_bottom_graphics_info;
    }

    struct graphics_info *window_graphics_info = graphics_info_create_relative(root_graphics_info, window_body_width_offset, window_body_height_offset, width, height, 0);
    if (!window_graphics_info)
    {
        res = -ENOMEM;
        goto out;
    }

    window->graphics = window_graphics_info;

    if (!(flags & WINDOW_FLAG_BORDERLESS))
    {
        struct framebuffer_pixel title_bar_font_color = {0};
        title_bar_font_color.red = 0xff;
        title_bar_font_color.blue = 0xff;
        title_bar_font_color.green = 0xff;

        window->title_bar_terminal = terminal_create(title_bar_graphics_info, 0, 0, total_window_width_bounds, WINDOW_TITLE_BAR_HEIGHT, font, title_bar_font_color, 0);
        if (!window->title_bar_terminal)
        {
            res = -ENOMEM;
            goto out;
        }
    }

    // Body text: dark neutral, not red.
    struct framebuffer_pixel pixel_color = {0};
    pixel_color.red = 0x2a;
    pixel_color.green = 0x2a;
    pixel_color.blue = 0x2e;
    window->terminal = terminal_create(window_graphics_info, 0, 0, width, height, font, pixel_color, TERMINAL_FLAG_BACKSPACE_ALLOWED);
    if (!window->terminal)
    {
        res = -ENOMEM;
        goto out;
    }

    struct framebuffer_pixel bg_color = {0};
    bg_color.red = 0xff;
    bg_color.blue = 0xff;
    bg_color.green = 0xff;
    terminal_draw_rect(window->terminal, 0, 0, width, height, bg_color);

    // Save the background of the terminal incase of backspaces
    terminal_background_save(window->terminal);

    if (flags & WINDOW_FLAG_BACKGROUND_TRANSPARENT)
    {
        terminal_transparency_key_set(window->terminal, bg_color);
    }

    if (!(flags & WINDOW_FLAG_BORDERLESS))
    {
        // Starts unfocused-colored; window_draw_title_bar() lays out the icons.
        struct framebuffer_pixel title_bar_bg_color = {0};
        title_bar_bg_color.red = 0x3a;
        title_bar_bg_color.blue = 0x3e;
        title_bar_bg_color.green = 0x3a;

        window_draw_title_bar(window, title_bar_bg_color);

        // Soft dark border instead of harsh pure black.
        struct framebuffer_pixel border_color = {0};
        border_color.red = 0x20;
        border_color.green = 0x20;
        border_color.blue = 0x24;
        graphics_draw_rect(border_left_graphics_info, 0, 0, border_left_graphics_info->width, border_left_graphics_info->height, border_color);
        graphics_draw_rect(border_right_graphics_info, 0, 0, border_right_graphics_info->width, border_right_graphics_info->height, border_color);
        graphics_draw_rect(border_bottom_graphics_info, 0, 0, border_bottom_graphics_info->width, border_bottom_graphics_info->height, border_color);
    }

    // Push to the windows vector
    vector_push(windows_vector, &window);

    size_t child_count = vector_count(window->root_graphics->children);
    window_set_z_index(window, child_count + 1);

    // Register the window event handler
    window_event_handler_register(window, window_event_handler);

    window_focus(window);

    // Redraw all graphics including our window
    graphics_redraw_all();

out:
    if (res < 0)
    {
        if (window)
        {
            if (window->terminal)
            {
                terminal_free(window->terminal);
                window->terminal = NULL;
            }
            if (window->title_bar_terminal)
            {
                terminal_free(window->title_bar_terminal);
                window->title_bar_terminal = NULL;
            }

            vector_pop_element(windows_vector, &window, sizeof(struct window *));
            kfree(window);
            window = NULL;
        }
    }

    return window;
}

void window_event_to_userland(struct window_event *kernel_win_event_in, struct window_event_userland *userland_win_event_out)
{
    userland_win_event_out->type = kernel_win_event_in->type;
    if (sizeof(userland_win_event_out->data) != sizeof(kernel_win_event_in->data))
    {
        panic("The userland win event and kernel win event data regions differ failed to copy\n");
    }
    memcpy(&userland_win_event_out->data, &kernel_win_event_in->data, sizeof(userland_win_event_out->data));
}

void window_keyboard_event_listener_on_event_keypress(struct window *win, struct keyboard *keyboard, struct keyboard_event *event)
{
    struct window_event win_event = {0};
    win_event.type = WINDOW_EVENT_TYPE_KEY_PRESS;
    win_event.data.keypress.key = event->data.key_press.key;
    window_event_push(win, &win_event);
}

void window_keyboard_event_listener_on_event_capslock_change(struct window *win, struct keyboard *keyboard, struct keyboard_event *event)
{
    // do nothing
}
void window_keyboard_event_listener_on_event(struct keyboard *keyboard, struct keyboard_event *event)
{
    struct window *focused_win = window_focused();
    if (focused_win)
    {
        switch (event->type)
        {
        case KEYBOARD_EVENT_KEY_PRESS:
            window_keyboard_event_listener_on_event_keypress(focused_win, keyboard, event);
            break;

        case KEYBOARD_EVENT_CAPS_LOCK_CHANGE:
            window_keyboard_event_listener_on_event_capslock_change(focused_win, keyboard, event);
            break;
        }
    }
}
