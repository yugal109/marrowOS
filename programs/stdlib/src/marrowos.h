#ifndef MARROWOS_H
#define MARROWOS_H
#include <stddef.h>
#include <stdbool.h>

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

#endif
