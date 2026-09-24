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
#include "image.h"
#include "font.h"

enum
{
    BUTTON_RESTART = 1,
    BUTTON_POWER_OFF = 2,
    FIRST_ROW_ID = 100,
    UPTIME_ID = 198,
    CLOCK_ID = 199,
    TEMP_ID = 197,
    STATUS_ID = 200,
};

#define PADDING 10
#define ROW_HEIGHT 26
#define ROW_PITCH (ROW_HEIGHT + 2)
#define BUTTON_HEIGHT 36
#define MAX_DISK_ROWS 4
#define MODEL_MAX_CHARS 30
#define WINDOW_WIDTH 420

static struct gui_element *status_field = NULL;
static struct gui_element *uptime_field = NULL;
static struct gui_element *clock_field = NULL;
static struct gui_element *temp_field = NULL;

static struct gui_element *row_create(struct gui *gui, int y, int width, int id, const char *text, bool caption)
{
    struct gui_element *field = gui_element_textfield_create(gui, NULL, PADDING, y, width, ROW_HEIGHT, id);
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

GUI_EVENT_HANDLER_RESPONSE button_event_handler(struct gui_event *gui_event)
{
    if (gui_event->type != GUI_EVENT_TYPE_ELEMENT_CLICKED)
    {
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;
    }

    switch (gui_event->element.id)
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

    struct marrowos_system_info info;
    memset(&info, 0, sizeof(info));
    marrowos_system_info(&info);

    int shown_disks = info.disk_count < MAX_DISK_ROWS ? (int)info.disk_count : MAX_DISK_ROWS;
    bool more_disks = (int)info.disk_count > shown_disks;

    char cache_line[64];
    bool has_cache = cache_format(cache_line, &info);

    bool has_temp = info.cpu_temp_valid != 0;

    // Processor (3, +cache, +temperature), OS, clock, memory, screen, uptime, storage caption, disks, optional "more"
    int rows = 9 + (has_cache ? 1 : 0) + (has_temp ? 1 : 0) + shown_disks + (more_disks ? 1 : 0);
    int height = PADDING + rows * ROW_PITCH + PADDING + BUTTON_HEIGHT + PADDING + ROW_HEIGHT + PADDING;

    struct window *main_win = window_create("Settings", WINDOW_WIDTH, height, 0, 558);
    if (!main_win)
    {
        return -1;
    }

    struct gui *gui = gui_bind_to_window(main_win, NULL);
    if (!gui)
    {
        return -1;
    }

    int width = main_win->width - (PADDING * 2);
    int y = PADDING;
    int id = FIRST_ROW_ID;

    row_create(gui, y, width, id++, "Processor", true);
    y += ROW_PITCH;
    row_create(gui, y, width, id++, info.cpu_name[0] ? info.cpu_name : "Unknown", false);
    y += ROW_PITCH;

    char line[128];
    char part[24];
    speed_format(part, info.cpu_mhz);
    sprintf(line, "%i cores, %i threads, %s", (int)info.cpu_cores, (int)info.cpu_threads, part);
    row_create(gui, y, width, id++, line, false);
    y += ROW_PITCH;

    if (has_cache)
    {
        row_create(gui, y, width, id++, cache_line, false);
        y += ROW_PITCH;
    }

    if (has_temp)
    {
        sprintf(line, "Temperature: %i C", (int)info.cpu_temp_c);
        temp_field = row_create(gui, y, width, TEMP_ID, line, false);
        y += ROW_PITCH;
    }

    row_create(gui, y, width, id++, "OS: MarrowOS (64-bit)", false);
    y += ROW_PITCH;

    char clock_line[48];
    clock_format(clock_line, &info);
    sprintf(line, "Date: %s", clock_line);
    clock_field = row_create(gui, y, width, CLOCK_ID, line, false);
    y += ROW_PITCH;

    memory_format(part, info.memory_bytes);
    sprintf(line, "Usable memory: %s", part);
    row_create(gui, y, width, id++, line, false);
    y += ROW_PITCH;

    sprintf(line, "Screen: %i x %i", (int)info.screen_width, (int)info.screen_height);
    row_create(gui, y, width, id++, line, false);
    y += ROW_PITCH;

    uptime_format(line, info.uptime_ms);
    uptime_field = row_create(gui, y, width, UPTIME_ID, line, false);
    y += ROW_PITCH;

    sprintf(line, "Storage devices: %i", (int)info.disk_count);
    row_create(gui, y, width, id++, line, true);
    y += ROW_PITCH;

    for (int i = 0; i < shown_disks; i++)
    {
        struct marrowos_disk_info disk;
        memset(&disk, 0, sizeof(disk));
        marrowos_disk_info(i, &disk);
        disk.name[MODEL_MAX_CHARS] = 0;

        char size[24];
        size_format(size, disk.size_bytes);
        sprintf(line, "%s  %s", disk.name, size);
        row_create(gui, y, width, id++, line, false);
        y += ROW_PITCH;
    }

    if (more_disks)
    {
        sprintf(line, "+ %i more", (int)info.disk_count - shown_disks);
        row_create(gui, y, width, id++, line, true);
        y += ROW_PITCH;
    }

    y += PADDING - (ROW_PITCH - ROW_HEIGHT);
    int button_width = (width - PADDING) / 2;
    struct gui_element *restart_btn = gui_element_button_create(gui, NULL, PADDING, y, button_width, BUTTON_HEIGHT, "Restart", BUTTON_RESTART);
    struct gui_element *power_btn = gui_element_button_create(gui, NULL, PADDING + button_width + PADDING, y, button_width, BUTTON_HEIGHT, "Power off", BUTTON_POWER_OFF);
    if (!restart_btn || !power_btn)
    {
        return -1;
    }
    gui_element_event_handler_set(restart_btn, button_event_handler);
    gui_element_event_handler_set(power_btn, button_event_handler);

    y += BUTTON_HEIGHT + PADDING;
    status_field = row_create(gui, y, width, STATUS_ID, "", true);

    unsigned long shown_seconds = info.uptime_ms / 1000;
    unsigned int shown_clock_second = info.second;
    while (gui_process(gui) >= 0)
    {
        marrowos_system_info(&info);
        if (info.second != shown_clock_second)
        {
            shown_clock_second = info.second;
            clock_format(clock_line, &info);
            sprintf(line, "Date: %s", clock_line);
            gui_element_textfield_text_set(clock_field, line);
        }

        if (info.uptime_ms / 1000 != shown_seconds)
        {
            shown_seconds = info.uptime_ms / 1000;
            uptime_format(line, info.uptime_ms);
            gui_element_textfield_text_set(uptime_field, line);

            if (temp_field && info.cpu_temp_valid)
            {
                sprintf(line, "Temperature: %i C", (int)info.cpu_temp_c);
                gui_element_textfield_text_set(temp_field, line);
            }
        }

        usleep(10);
    }

    return 0;
}
