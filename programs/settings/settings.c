#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "window.h"
#include "memory.h"
#include "marrowos.h"
#include "graphics.h"
#include "gui/gui.h"
#include "gui/element.h"
#include "gui/button.h"
#include "gui/textfield.h"
#include "gui/plane.h"
#include "image.h"
#include "font.h"

enum
{
    BUTTON_RESTART = 1,
    BUTTON_POWER_OFF = 2,
    BUTTON_PAGE_FIRST = 10,
    TITLE_ID = 99,
    FIRST_ROW_ID = 100,
    STATUS_ID = 200,
    DIVIDER_ID = 300,
};

enum
{
    PAGE_ABOUT = 0,
    PAGE_PROCESSOR,
    PAGE_MEMORY,
    PAGE_STORAGE,
    PAGE_SERIAL,
    PAGE_COUNT,
};

static const char *page_names[PAGE_COUNT] = {"About", "Processor", "Memory", "Storage", "Serial"};

#define PADDING 10
#define ROW_HEIGHT 26
#define ROW_PITCH (ROW_HEIGHT + 2)
#define BUTTON_HEIGHT 36
#define BUTTON_GAP 6
#define MAX_DISK_ROWS 4
#define MAX_DETAIL_ROWS 8
#define DETAIL_LINE_MAX 96
#define MODEL_MAX_CHARS 30
#define WINDOW_WIDTH 860
#define WINDOW_HEIGHT 460
#define DIVIDER_WIDTH 2
#define SELECTED_BORDER_WIDTH 3

static struct gui_element *status_field = NULL;
static struct gui_element *title_field = NULL;
static struct gui_element *detail_rows[MAX_DETAIL_ROWS];
static struct gui_element *page_buttons[PAGE_COUNT];
static int selected_page = PAGE_ABOUT;
// Syscall buffers must be on the stack or the heap: the kernel terminates a process that
// passes a static one, so this is malloc'd
static struct marrowos_system_info *current_info = NULL;

static struct gui_element *row_create(struct gui *gui, int x, int y, int width, int id, const char *text, bool caption)
{
    struct gui_element *field = gui_element_textfield_create(gui, NULL, x, y, width, ROW_HEIGHT, id);
    if (!field)
    {
        return NULL;
    }

    if (caption)
    {
        gui_element_textfield_color_set(field, 0x70, 0x70, 0x70);
    }

    // Text first: a read-only field ignores input
    gui_element_textfield_text_set(field, text);
    gui_element_textfield_read_only_set(field, true);
    return field;
}

// Decimal units, as drive makers quote them
static void size_format(char *out, unsigned long bytes)
{
    if (bytes == 0)
    {
        sprintf(out, "unknown size");
        return;
    }

    unsigned long mb = bytes / 1000000UL;
    if (mb >= 1000000UL)
    {
        unsigned long tenths = mb / 100000UL;
        sprintf(out, "%i.%i TB", (int)(tenths / 10), (int)(tenths % 10));
    }
    else if (mb >= 1000UL)
    {
        unsigned long tenths = mb / 100UL;
        sprintf(out, "%i.%i GB", (int)(tenths / 10), (int)(tenths % 10));
    }
    else
    {
        sprintf(out, "%i MB", (int)mb);
    }
}

// Memory is quoted in binary units, as installed RAM is
static void memory_format(char *out, unsigned long bytes)
{
    unsigned long mb = bytes >> 20;
    if (mb >= 1024)
    {
        unsigned long tenths = (mb * 10) / 1024;
        sprintf(out, "%i.%i GB", (int)(tenths / 10), (int)(tenths % 10));
    }
    else
    {
        sprintf(out, "%i MB", (int)mb);
    }
}

static void speed_format(char *out, unsigned int mhz)
{
    if (mhz >= 1000)
    {
        int hundredths = (int)((mhz % 1000) / 10);
        sprintf(out, "%i.%s%i GHz", (int)(mhz / 1000), hundredths < 10 ? "0" : "", hundredths);
    }
    else
    {
        sprintf(out, "%i MHz", (int)mhz);
    }
}

static void cache_size_format(char *out, unsigned int kb)
{
    if (kb >= 1024 && kb % 1024 == 0)
    {
        sprintf(out, "%i MB", (int)(kb / 1024));
    }
    else
    {
        sprintf(out, "%i KB", (int)kb);
    }
}

// L1 and L2 are per core, L3 is shared
static bool cache_format(char *out, const struct marrowos_system_info *info)
{
    if (!info->cache_l1_kb && !info->cache_l2_kb && !info->cache_l3_kb)
    {
        return false;
    }

    char l1[16], l2[16], l3[16];
    cache_size_format(l1, info->cache_l1_kb);
    cache_size_format(l2, info->cache_l2_kb);
    cache_size_format(l3, info->cache_l3_kb);
    sprintf(out, "Cache: L1 %s, L2 %s, L3 %s", l1, l2, l3);
    return true;
}

#define PAD(v) ((v) < 10 ? "0" : "")

static void clock_format(char *out, const struct marrowos_system_info *info)
{
    sprintf(out, "%i-%s%i-%s%i  %s%i:%s%i:%s%i", (int)info->year, PAD(info->month), (int)info->month,
            PAD(info->day), (int)info->day, PAD(info->hour), (int)info->hour,
            PAD(info->minute), (int)info->minute, PAD(info->second), (int)info->second);
}

static void uptime_format(char *out, unsigned long uptime_ms)
{
    unsigned long seconds = uptime_ms / 1000;
    int hours = (int)(seconds / 3600);
    int minutes = (int)((seconds % 3600) / 60);
    if (hours > 0)
    {
        sprintf(out, "Uptime: %i h %i min", hours, minutes);
    }
    else if (minutes > 0)
    {
        sprintf(out, "Uptime: %i min %i s", minutes, (int)(seconds % 60));
    }
    else
    {
        sprintf(out, "Uptime: %i s", (int)seconds);
    }
}

// Fills the detail pane for a page. Rows the page does not need are blanked, since
// elements cannot be hidden.
static void page_show(int page, const struct marrowos_system_info *info)
{
    char lines[MAX_DETAIL_ROWS][DETAIL_LINE_MAX];
    int count = 0;
#define ADD(...)                                        \
    do                                                  \
    {                                                   \
        if (count < MAX_DETAIL_ROWS)                    \
        {                                               \
            sprintf(lines[count++], __VA_ARGS__);       \
        }                                               \
    } while (0)

    char part[48];
    char extra[64];

    switch (page)
    {
    case PAGE_ABOUT:
        ADD("OS: MarrowOS (64-bit)");
        clock_format(extra, info);
        ADD("Date: %s", extra);
        uptime_format(extra, info->uptime_ms);
        ADD("%s", extra);
        ADD("Screen: %i x %i", (int)info->screen_width, (int)info->screen_height);
        break;

    case PAGE_PROCESSOR:
        ADD("%s", info->cpu_name[0] ? info->cpu_name : "Unknown");
        speed_format(part, info->cpu_mhz);
        ADD("%i cores, %i threads, %s", (int)info->cpu_cores, (int)info->cpu_threads, part);
        if (cache_format(extra, info))
        {
            ADD("%s", extra);
        }
        if (info->cpu_temp_valid)
        {
            ADD("Temperature: %i C", (int)info->cpu_temp_c);
        }
        break;

    case PAGE_MEMORY:
        memory_format(part, info->memory_bytes);
        ADD("Usable memory: %s", part);
        break;

    case PAGE_STORAGE:
    {
        int shown = info->disk_count < MAX_DISK_ROWS ? (int)info->disk_count : MAX_DISK_ROWS;
        ADD("Storage devices: %i", (int)info->disk_count);
        for (int i = 0; i < shown; i++)
        {
            struct marrowos_disk_info disk;
            memset(&disk, 0, sizeof(disk));
            marrowos_disk_info(i, &disk);
            disk.name[MODEL_MAX_CHARS] = 0;
            size_format(part, disk.size_bytes);
            ADD("%s  %s", disk.name, part);
        }
        if ((int)info->disk_count > shown)
        {
            ADD("+ %i more", (int)info->disk_count - shown);
        }
        break;
    }

    case PAGE_SERIAL:
        if (info->serial_state == 1)
        {
            ADD("Adapter: ready");
            ADD("Baud rate: %i", (int)info->serial_baud);
        }
        else
        {
            ADD("Adapter: %s", info->serial_state == 2 ? "error" : "not found");
        }
        break;
    }
#undef ADD

    gui_element_textfield_text_set(title_field, page_names[page]);
    for (int i = 0; i < MAX_DETAIL_ROWS; i++)
    {
        gui_element_textfield_text_set(detail_rows[i], i < count ? lines[i] : "");
    }
}

static void page_select(int page)
{
    selected_page = page;
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        // A button is a plane holding the label element, and the plane carries the color
        struct gui_element *plane = page_buttons[i]->parent;
        bool selected = i == page;
        if (selected)
        {
            gui_element_plane_bg_color_set(plane, 0x7A, 0xA8, 0xE0);
            gui_element_plane_set_border_color(plane, 0x10, 0x30, 0x80);
            gui_element_plane_set_border_width(plane, SELECTED_BORDER_WIDTH);
        }
        else
        {
            gui_element_plane_bg_color_set(plane, 0xAA, 0xAA, 0xAA);
            gui_element_plane_set_border_color(plane, 0x00, 0x00, 0x00);
            gui_element_plane_set_border_width(plane, 1);
        }
        // The focus border would otherwise replace this one with a thin red line
        gui_element_plane_set_focused_border_color(plane, selected ? 0x10 : 0x00, selected ? 0x30 : 0x00, selected ? 0x80 : 0x00);
        gui_element_plane_focused_border_width_set(plane, selected ? SELECTED_BORDER_WIDTH : 1);
        gui_element_mark_for_redraw(plane);
        gui_element_mark_for_redraw(page_buttons[i]);
    }
    page_show(page, current_info);
}

GUI_EVENT_HANDLER_RESPONSE button_event_handler(struct gui_event *gui_event)
{
    if (gui_event->type != GUI_EVENT_TYPE_ELEMENT_CLICKED)
    {
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;
    }

    int id = gui_event->element.id;
    if (id >= BUTTON_PAGE_FIRST && id < BUTTON_PAGE_FIRST + PAGE_COUNT)
    {
        page_select(id - BUTTON_PAGE_FIRST);
        return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN;
    }

    switch (id)
    {
    case BUTTON_RESTART:
        marrowos_power(MARROWOS_POWER_RESTART);
        break;

    case BUTTON_POWER_OFF:
        // Only returns if the hardware ignored the request
        marrowos_power(MARROWOS_POWER_OFF);
        gui_element_textfield_text_set(status_field, "Power off is not supported here");
        break;
    }

    return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN;
}

int main(int argc, char **argv)
{
    graphics_image_formats_init();
    font_system_init();

    current_info = malloc(sizeof(struct marrowos_system_info));
    if (!current_info)
    {
        return -1;
    }
    memset(current_info, 0, sizeof(*current_info));
    marrowos_system_info(current_info);

    struct window *main_win = window_create("Settings", WINDOW_WIDTH, WINDOW_HEIGHT, 0, 558);
    if (!main_win)
    {
        return -1;
    }

    struct gui *gui = gui_bind_to_window(main_win, NULL);
    if (!gui)
    {
        return -1;
    }

    // The kernel shrinks a window that does not fit the screen, so lay out from the size it
    // actually got. Elements reaching past the window crash the process.
    int win_width = main_win->width;
    int win_height = main_win->height;

    // The left column is 30% of the window, the detail pane gets the rest
    int sidebar_width = win_width * 3 / 10;

    // Left column: one button per page, then the power buttons
    int side_x = PADDING;
    int side_width = sidebar_width - (PADDING * 2);
    int y = PADDING;
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        page_buttons[i] = gui_element_button_create(gui, NULL, side_x, y, side_width, BUTTON_HEIGHT, page_names[i], BUTTON_PAGE_FIRST + i);
        if (!page_buttons[i])
        {
            return -1;
        }
        gui_element_event_handler_set(page_buttons[i], button_event_handler);
        y += BUTTON_HEIGHT + BUTTON_GAP;
    }

    y += PADDING;
    struct gui_element *restart_btn = gui_element_button_create(gui, NULL, side_x, y, side_width, BUTTON_HEIGHT, "Restart", BUTTON_RESTART);
    y += BUTTON_HEIGHT + BUTTON_GAP;
    struct gui_element *power_btn = gui_element_button_create(gui, NULL, side_x, y, side_width, BUTTON_HEIGHT, "Power off", BUTTON_POWER_OFF);
    if (!restart_btn || !power_btn)
    {
        return -1;
    }
    gui_element_event_handler_set(restart_btn, button_event_handler);
    gui_element_event_handler_set(power_btn, button_event_handler);

    struct gui_element *divider = gui_element_plane_create(gui, NULL, sidebar_width, 0, DIVIDER_WIDTH, win_height, DIVIDER_ID);
    if (divider)
    {
        gui_element_plane_bg_color_set(divider, 0x90, 0x90, 0x90);
    }

    // Right column: a title, a pool of rows the pages fill in, and a status line
    int detail_x = sidebar_width + DIVIDER_WIDTH + PADDING;
    int detail_width = win_width - detail_x - PADDING;
    y = PADDING;
    title_field = row_create(gui, detail_x, y, detail_width, TITLE_ID, "", true);
    y += ROW_PITCH + PADDING;
    for (int i = 0; i < MAX_DETAIL_ROWS; i++)
    {
        detail_rows[i] = row_create(gui, detail_x, y, detail_width, FIRST_ROW_ID + i, "", false);
        y += ROW_PITCH;
    }
    status_field = row_create(gui, detail_x, win_height - PADDING - ROW_HEIGHT, detail_width, STATUS_ID, "", true);

    if (!title_field || !status_field)
    {
        return -1;
    }
    for (int i = 0; i < MAX_DETAIL_ROWS; i++)
    {
        if (!detail_rows[i])
        {
            return -1;
        }
    }

    page_select(PAGE_ABOUT);

    unsigned long shown_seconds = current_info->uptime_ms / 1000;
    unsigned int shown_clock_second = current_info->second;
    while (gui_process(gui) >= 0)
    {
        marrowos_system_info(current_info);

        // Clock, uptime and temperature change over time, so the open page is refreshed each second
        if (current_info->second != shown_clock_second || current_info->uptime_ms / 1000 != shown_seconds)
        {
            shown_clock_second = current_info->second;
            shown_seconds = current_info->uptime_ms / 1000;
            page_show(selected_page, current_info);
        }

        usleep(10);
    }

    return 0;
}
