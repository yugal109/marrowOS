#ifndef MARROWOS_H
#define MARROWOS_H
#include <stddef.h>
#include <stdbool.h>

// temporary: until we implement the GUI SDK
enum
{
    WINDOW_EVENT_TYPE_NULL,
    WINDOW_EVENT_TYPE_FOCUS,
    WINDOW_EVENT_TYPE_LOST_FOCUS,
    WINDOW_EVENT_TYPE_MOUSE_MOVE,
    WINDOW_EVENT_TYPE_MOUSE_CLICK,
    WINDOW_EVENT_TYPE_WINDOW_CLOSE,
    WINDOW_EVENT_TYPE_KEY_PRESS,
    WINDOW_EVENT_TYPE_MOUSE_RELEASE,
    WINDOW_EVENT_TYPE_RESIZE
};

struct window_event
{
    int type;
    int win_id;
    void *window;

    union
    {
        struct
        {
            // empty no properties
        } focus;

        // positions are relative to the window body
        struct
        {
            int x;
            int y;
        } move;

        // relative to the window body.
        struct
        {
            int x;
            int y;
        } click;

        struct
        {
            int key;
        } keypress;

        struct
        {
            int x;
            int y;
        } release;

        struct
        {
            int width;
            int height;
        } resize;

    } data;
};

struct command_argument
{
    char argument[512];
    struct command_argument *next;
};

struct process_arguments
{
    int argc;
    char **argv;
};

// forward declare file stat
struct file_stat;
struct window;

void print(const char *message);
int marrowos_getkey();
void *marrowos_malloc(size_t size);
void marrowos_free(void *ptr);
void marrowos_putchar(char c);
int marrowos_getkey_block();
void marrowos_terminal_readline(char *out, int max, bool output_while_typing);
void marrowos_process_load_start(const char *filename);
struct command_argument *marrowos_parse_command(const char *command, int max);
void marrowos_process_get_arguments(struct process_arguments *arguments);
int marrowos_system(struct command_argument *arguments);
int marrowos_system_run(const char *command);
void marrowos_exit();
int marrowos_fopen(const char *filename, const char *mode);
void marrowos_fclose(size_t fd);
long marrowos_fread(void *buffer, size_t size, size_t count, long fd);
long marrowos_fseek(long fd, long offset, long whence);
long marrowos_fstat(long fd, struct file_stat *file_stat_out);
void *marrowos_realloc(void *old_ptr, size_t new_size);
struct window *marrowos_window_create(const char *title, long width, long height, long flags, long id);
void marrowos_divert_stdout_to_window(struct window *window);
int marrowos_process_get_window_event(struct window_event *event);
void *marrowos_window_get_graphics(struct window *window);
void *marrowos_graphic_pixels_get(void *graphics);
void *marrowos_graphics_create(size_t x, size_t y, size_t width, size_t height, void *parent_graphics);
void marrowos_window_redraw(struct window *window);
void marrowos_window_title_set(struct window *window, const char *title);
void marrowos_window_cursor_set(struct window *window, long rel_x, long rel_y);
void marrowos_window_redraw_region(long rel_x, long rel_y, long rel_width, long rel_height, struct window *window);
void marrowos_udelay(unsigned long microseconds);

// Mirror the kernel's system_info / system_disk_info
struct marrowos_system_info
{
    char cpu_name[64];
    unsigned long memory_bytes;
    unsigned long uptime_ms;
    unsigned int disk_count;
    unsigned int cpu_mhz;
    unsigned int cpu_cores;
    unsigned int cpu_threads;
    unsigned int screen_width;
    unsigned int screen_height;
    // Per core for L1 and L2, shared for L3; 0 if unknown
    unsigned int cache_l1_kb;
    unsigned int cache_l2_kb;
    unsigned int cache_l3_kb;
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int cpu_temp_valid;
    unsigned int cpu_temp_c;
};

struct marrowos_disk_info
{
    char name[48];
    unsigned long size_bytes;
};

enum
{
    MARROWOS_POWER_RESTART,
    MARROWOS_POWER_OFF
};

int marrowos_system_info(struct marrowos_system_info *info);
int marrowos_disk_info(int index, struct marrowos_disk_info *info);
// Restart does not return; power off returns only if the hardware ignored it
int marrowos_power(int action);

#endif
