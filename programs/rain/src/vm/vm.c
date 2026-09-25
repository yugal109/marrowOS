#include <string.h>

#include "object.h"
#include "memory.h"
#include "vm.h"
#include "vm_internal.h"
#include "compiler.h"
#include "natives.h"
#include "builtins.h"

VM vm;

void initVM(void)
{
    vm.frames = (CallFrame *)malloc(sizeof(CallFrame) * FRAMES_MAX);
    vm.stack = (Value *)malloc(sizeof(Value) * STACK_MAX);
    if (vm.frames == NULL || vm.stack == NULL)
    {
        fprintf(stderr, "Not enough memory to start Rain.\n");
        exit(1);
    }
    resetStack();
    vm.objects = NULL;

    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    vm.lastResult = NIL_VAL;

    initTable(&vm.globals);
    initTable(&vm.strings);
    initTable(&vm.preload);
    initTable(&vm.importedModules);
    initTable(&vm.loadingModules);

    vm.initString = NULL;
    vm.initString = copyString(RAIN_NAME_INIT, (int)strlen(RAIN_NAME_INIT));
    registerNatives();
}

void freeVM(void)
{
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeTable(&vm.preload);
    freeTable(&vm.importedModules);
    freeTable(&vm.loadingModules);
    vm.initString = NULL;
    freeObjects();
    free(vm.frames);
    free(vm.stack);
    vm.frames = NULL;
    vm.stack = NULL;
}

InterpretResult interpret(const char *source)
{
    ObjFunction *function = compile(source);
    if (function == NULL)
        return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure *closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    callFunction(closure, 0);

    vm.frames[vm.frameCount - 1].env = &vm.globals;

    return run(0);
}
