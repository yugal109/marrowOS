#ifndef MARROWOS_H
#define MARROWOS_H
#include <stddef.h>
#include <stdbool.h>

// temporary: until we implement the GUI SDK
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

#endif
