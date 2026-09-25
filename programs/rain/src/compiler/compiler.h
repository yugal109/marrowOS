#ifndef rain_compiler_h
#define rain_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction *compile(const char *source); // compile source string to bytecode
void markCompilerRoots(void);

#endif
