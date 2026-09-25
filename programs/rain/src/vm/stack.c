#include "vm_internal.h"

void resetStack(void)
{
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
    vm.handlerCount = 0;
    vm.exceptionCaught = false;
}

void push(Value value)
{
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop(void)
{
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance)
{
    return vm.stackTop[-1 - distance];
}
