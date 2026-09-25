#ifndef rain_vm_h
#define rain_vm_h

#include "chunk.h" // VM executes this Chunk
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct
{
    ObjClosure *closure; // which fuction is running
    uint8_t *ip;         // where we are in its bytecode
    Value *slots;        // where its locals live on stack
    Table *env;
} CallFrame;

typedef enum
{
    INTERPRET_OK,            // execution succeeded
    INTERPRET_COMPILE_ERROR, // compiler found a static error
    INTERPRET_RUNTIME_ERROR  // VM hit an error at runtime
} InterpretResult;

#define MAX_EXCEPTION_HANDLERS 64

typedef struct
{
    int frameCount;   // saved frame count
    Value *stackTop;  // saved stack top
    uint8_t *catchIp; // pointer to fang block
} RainExceptionHandler;

typedef struct
{
    // Allocated in initVM: as static arrays they made a 260 KB .bss, more than the MarrowOS ELF loader reserves
    CallFrame *frames;
    int frameCount;
    Value *stack;

    Value *stackTop;
    Table strings;
    ObjString *initString;

    Table globals;
    Table preload; // built-in module loaders
    Table importedModules;
    Table loadingModules;

    ObjUpvalue *openUpvalues;

    size_t bytesAllocated;
    size_t nextGC;

    Obj *objects;

    int grayCount;
    int grayCapacity;

    Obj **grayStack;
    Value lastResult;

    RainExceptionHandler handlerStack[MAX_EXCEPTION_HANDLERS];
    int handlerCount;
    bool exceptionCaught;

} VM;
extern VM vm;

void initVM(void);                             // initialize the VM
void freeVM(void);                             // free the VM
InterpretResult interpret(const char *source); // takes in source string, not chunk
void push(Value value);                        // push a value onto the stack
Value pop(void);                               // pop the top value off the stack
InterpretResult run(int frameBase);
bool callFunction(ObjClosure *closure, int argCount);

#endif
