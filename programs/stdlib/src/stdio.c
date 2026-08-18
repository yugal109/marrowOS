#include "stdio.h"
#include "marrowos.h"

int putchar(int c)
{
    marrowos_putchar(c);
    return 0;
}
