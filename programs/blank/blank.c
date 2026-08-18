#include "marrowos.h"
#include "stdlib.h"

int main(int argc, char **argv)
{
    print("Hello how are you!\n");
    void *ptr = yreserve(16);
    yfree(ptr);
    while (1)
    {
        if (getkey() != 0)
        {
            print("key was pressed\n");
        }
    };
    return 0;
}
