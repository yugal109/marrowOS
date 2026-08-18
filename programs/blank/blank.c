#include "marrowos.h"
#include "stdlib.h"
#include "stdio.h"

int main(int argc, char **argv)
{
    print("Hello how are you!\n");
    void *ptr = yreserve(16);
    printf("age %d hex %x ptr %p ch %c str %s\n", 24, 255, ptr, 'Z', "hi");
    printf("%% ull %llu\n", 123ULL);
    print(itoa(8763));
    yfree(ptr);
    putchar('Z');
    printf("My age is %i\n", 24);
    while (1)
    {
        if (getkey() != 0)
        {
            print("key was pressed\n");
        }
    };
    return 0;
}
