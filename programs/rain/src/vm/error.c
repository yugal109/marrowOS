#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "vm_internal.h"
#include "memory.h"

bool runtimeError(const char *format, ...)
{
    if (vm.handlerCount > 0)
    {
        RainExceptionHandler handler = vm.handlerStack[--vm.handlerCount];

        va_list args;
        va_start(args, format);
        char message[256];
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);

        vm.frameCount = handler.frameCount;
        vm.stackTop = handler.stackTop;

        CallFrame *frame = &vm.frames[vm.frameCount - 1];
        frame->ip = handler.catchIp;

        push(OBJ_VAL(copyString(message, (int)strlen(message))));
        vm.exceptionCaught = true;
        return true;
    }

    vm.exceptionCaught = false;

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--)
    {
        CallFrame *frame = &vm.frames[i];
        ObjFunction *function = frame->closure->function;

        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);
        if (function->name == NULL)
        {
            fprintf(stderr, "script\n");
        }
        else
        {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
    return false;
}
