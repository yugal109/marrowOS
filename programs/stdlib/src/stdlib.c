#include "stdlib.h"
#include "marrowos.h"

char *itoa(int i)
{
    static char text[12];
    int loc = 11;
    text[11] = 0;
    char neg = 1;
    if (i >= 0)
    {
        neg = 0;
        i = -i;
    }

    while (i)
    {
        text[--loc] = '0' - (i % 10);
        i /= 10;
    }
    if (loc == 11)
    {
        text[--loc] = '0';
    }
    if (neg)
    {
        text[--loc] = '-';
    }
    return &text[loc];
}

void *yreserve(size_t size)
{
    return marrowos_malloc(size);
};

void yfree(void *ptr)
{
    marrowos_free(ptr);
};
