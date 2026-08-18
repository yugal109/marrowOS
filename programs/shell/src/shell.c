#include "shell.h"
#include "stdio.h"
#include "stdlib.h"
#include "marrowos.h"

int main(int argc, char **argv)
{
    print("MarrowOS v1.0.0\n");
    while (1)
    {
        print("> ");
        char buf[1024];
        marrowos_terminal_readline(buf, sizeof(buf), true);
        print("\n");
    }
    return 0;
}
