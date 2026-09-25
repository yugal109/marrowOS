// Runs the MarrowOS build of Rain on the host, so the platform code (number formatting, allocator,
// strtod) can be tested in seconds without booting the OS. See run_host_tests.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rain.h"

static void host_write(const char *text, int length, int is_error)
{
    fwrite(text, 1, (size_t)length, is_error ? stderr : stdout);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: host_rain script.rn\n");
        return 64;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file)
    {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 74;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *source = malloc((size_t)size + 1);
    size_t read = fread(source, 1, (size_t)size, file);
    source[read] = 0;
    fclose(file);

    // Two runs in a row, to check that nothing leaks from one Run click into the next
    int result = rain_run(source, host_write);
    if (getenv("RAIN_TWICE"))
    {
        fprintf(stdout, "---- second run ----\n");
        result = rain_run(source, host_write);
    }
    free(source);
    return result;
}
