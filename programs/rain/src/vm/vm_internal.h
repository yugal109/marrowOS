#ifndef rain_vm_internal_h
#define rain_vm_internal_h

#include <stdarg.h>

#include "vm.h"

void resetStack(void);
void push(Value value);
Value pop(void);
Value peek(int distance);
bool isFalsey(Value value);
void concatenate(void);
bool runtimeError(const char *format, ...);

ObjUpvalue *captureUpvalue(Value *local);
void closeUpvalues(Value *last);
bool callValue(Value callee, int argCount);
bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount);
bool invoke(ObjString *name, int argCount);
bool bindMethod(ObjClass *klass, ObjString *name);
void defineMethod(ObjString *name);

#endif
