#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

void *reallocate(void *pointer, size_t oldSize, size_t newSize)
{
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize)
    {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
        if (vm.bytesAllocated > vm.nextGC)
        {
            collectGarbage();
        }
    }
    if (newSize == 0)
    {
        free(pointer);
        return NULL;
    }
    void *result = realloc(pointer, newSize); // resize or allocate fresh block
    return result;
}

void markObject(Obj *object)
{
    if (object == NULL)
        return;
    if (object->isMarked)
        return;
#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1)
    {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);
        if (vm.grayStack == NULL)
            exit(1);
    }
    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value)
{
    if (!IS_OBJ(value))
        return;
    markObject(AS_OBJ(value));
}

static void markArray(ValueArray *array)
{
    for (int i = 0; i < array->count; i++)
    {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void *)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif
    switch (object->type)
    {
    case OBJ_ARRAY:
    {
        ObjArray *array = (ObjArray *)object;
        for (int i = 0; i < array->count; i++)
        {
            markValue(array->values[i]);
        }
        break;
    }
    case OBJ_MATRIX:
        // no references — data is plain double* not GC managed
        break;
    case OBJ_MODULE:
    {
        ObjModule *module = (ObjModule *)object;
        markObject((Obj *)module->name);
        markTable(&module->fields);
        if (module->env != NULL)
            markTable(module->env);
        break;
    }
    case OBJ_BOUND_METHOD:
    {
        ObjBoundMethod *bound = (ObjBoundMethod *)object;
        markValue(bound->receiver);
        markObject((Obj *)bound->method);
        break;
    }
    case OBJ_CLASS:
    {
        ObjClass *klass = (ObjClass *)object;
        markObject((Obj *)klass->name);
        markTable(&klass->methods);
        break;
    }
    case OBJ_MAP:
    {
        ObjMap *map = (ObjMap *)object;
        markTable(&map->entries);
        break;
    }
    case OBJ_CLOSURE:
    {
        ObjClosure *closure = (ObjClosure *)object;
        markObject((Obj *)closure->function);
        for (int i = 0; i < closure->upvalueCount; i++)
        {
            markObject((Obj *)closure->upvalues[i]);
        }
        if (closure->env != NULL && closure->env != &vm.globals)
            markTable(closure->env); // ← keep moduleEnv alive
        break;
    }
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)object;
        markObject((Obj *)function->name);
        markArray(&function->chunk.constants);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)object;
        markObject((Obj *)instance->klass);
        markTable(&instance->fields);
        break;
    }
    case OBJ_UPVALUE:
        markValue(((ObjUpvalue *)object)->closed);
        break;
    case OBJ_NATIVE:
    case OBJ_STRING:
        break;
    }
}

static void freeObject(Obj *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *)object, object->type);
#endif

    switch (object->type)
    {
    case OBJ_ARRAY:
    {
        ObjArray *array = (ObjArray *)object;
        FREE_ARRAY(Value, array->values, array->capacity);
        FREE(ObjArray, object);
        break;
    }
    case OBJ_MAP:
    {
        ObjMap *map = (ObjMap *)object;
        freeTable(&map->entries);
        FREE(ObjMap, object);
        break;
    }
    case OBJ_MATRIX:
    {
        ObjMatrix *matrix = (ObjMatrix *)object;
        if (matrix->data != NULL)
        {
            size_t dataBytes =
                (size_t)matrix->rows * (size_t)matrix->cols * sizeof(double);
            vm.bytesAllocated -= dataBytes;
            free(matrix->data);
        }
        FREE(ObjMatrix, object);
        break;
    }
    case OBJ_MODULE:
    {
        ObjModule *module = (ObjModule *)object;
        freeTable(&module->fields);
        if (module->env != NULL)
        {
            freeTable(module->env);
            FREE(Table, module->env);
        }
        FREE(ObjModule, object);
        break;
    }
    case OBJ_BOUND_METHOD:
        FREE(ObjBoundMethod, object);
        break;
    case OBJ_CLASS:
    {
        ObjClass *klass = (ObjClass *)object;
        freeTable(&klass->methods);
        FREE(ObjClass, object);
        break;
    }
    case OBJ_CLOSURE:
    {
        ObjClosure *closure = (ObjClosure *)object;
        FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalueCount);
        FREE(ObjClosure, object);
        break;
    }
    case OBJ_FUNCTION:
    {
        ObjFunction *function = (ObjFunction *)object;
        freeChunk(&function->chunk);
        FREE(ObjFunction, object);
        break;
    }
    case OBJ_INSTANCE:
    {
        ObjInstance *instance = (ObjInstance *)object;
        freeTable(&instance->fields);
        FREE(ObjInstance, object);
        break;
    }
    case OBJ_NATIVE:
    {
        FREE(ObjNative, object);
        break;
    }
    case OBJ_STRING:
    {
        ObjString *string = (ObjString *)object;
        FREE_ARRAY(char, string->chars, string->length + 1);
        FREE(ObjString, object);
        break;
    }
    case OBJ_UPVALUE:
    {
        FREE(ObjUpvalue, object);
        break;
    }
    }
}

static void markRoots(void)
{
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++)
    {
        markValue(*slot);
    }

    for (int i = 0; i < vm.frameCount; i++)
    {
        markObject((Obj *)vm.frames[i].closure);
        if (vm.frames[i].env != NULL && vm.frames[i].env != &vm.globals)
        {
            markTable(vm.frames[i].env);
        }
    }

    for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        markObject((Obj *)upvalue);
    }
    markTable(&vm.globals);
    markTable(&vm.preload);
    markTable(&vm.importedModules);
    markTable(&vm.loadingModules);
    markCompilerRoots();
    markObject((Obj *)vm.initString);
}

static void traceReferences(void)
{
    while (vm.grayCount > 0)
    {
        Obj *object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep(void)
{
    Obj *previous = NULL;
    Obj *object = vm.objects;

    while (object != NULL)
    {
        if (object->isMarked)
        {
            object->isMarked = false;
            previous = object;
            object = object->next;
        }
        else
        {
            Obj *unreached = object;
            object = object->next;
            if (previous != NULL)
            {
                previous->next = object;
            }
            else
            {
                vm.objects = object;
            }
            freeObject(unreached);
        }
    }
}

void freeObjects(void)
{
    Obj *object = vm.objects;
    while (object != NULL)
    {
        Obj *next = object->next;
        freeObject(object);
        object = next;
    }
    free(vm.grayStack);
}

void collectGarbage(void)
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytesAllocated;
#endif
    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    sweep();
    {
        size_t grown = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
        size_t floor = vm.bytesAllocated + GC_HEAP_MIN_HEADROOM;
        vm.nextGC = grown > floor ? grown : floor;
    }
#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %ld bytes (from %ld to %ld) next at %ld\n", before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}
