// Stand-ins for the MarrowOS system functions Rain's platform code calls
#include <stdio.h>
#include <stdlib.h>

void *yreserve(unsigned long size)
{
    return calloc(1, size);
}

void yfree(void *pointer)
{
    free(pointer);
}

void marrowos_exit(void)
{
    exit(99);
}

int marrowos_system_info(void *info)
{
    (void)info;
    return 0;
}

void *rain_malloc(unsigned long size);

// Lets the host tests use `benutzen` on real files
char *rain_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *buffer = rain_malloc((unsigned long)size + 1);
    size_t read = fread(buffer, 1, (size_t)size, file);
    buffer[read] = 0;
    fclose(file);
    return buffer;
}
