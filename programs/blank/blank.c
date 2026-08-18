#include "marrowos.h"
#include "stdlib.h"
#include "stdio.h"

int main(int argc, char **argv)
{
    print("Hello how are you!\n");
    void *ptr = yreserve(16);
    print(itoa(8763));
    yfree(ptr);
    putchar('Z');
    while (1)
    {
        if (getkey() != 0)
        {
            print("key was pressed\n");
        }
    };
    return 0;
}
