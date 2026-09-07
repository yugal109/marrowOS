#include "file.h"
#include "marrowos.h"

int fopen(const char* filename,const char* mode)
{
    return (int)marrowos_fopen(filename,mode);
}
