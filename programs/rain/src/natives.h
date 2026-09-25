#ifndef rain_natives_h
#define rain_natives_h

#include "vm.h"

void defineNative(const char *name, NativeFn function);
void registerNatives(void);

#endif
