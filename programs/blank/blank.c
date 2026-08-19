#include "marrowos.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char **argv)
{
    char *ptr = yreserve(20);
    strcpy(ptr, "hello world");
    print(ptr);
    yfree(ptr);
    ptr[0] = 'B';
    print("abc\n");
    while (1)
    {
    };
    return 0;
}
