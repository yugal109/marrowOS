#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include "table.h"

// allocate raw heap object of given size and type
static Obj *allocateObject(size_t size, ObjType type)
{
    Obj *object = (Obj *)reallocate(NULL, 0, size); // allocate raw memory
    object->type = type;
    object->isMarked = false;
    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %ld for %d\n", (void *)object, size, type);
#endif

    return object;
}

ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method)
{
    ObjBoundMethod *bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjMatrix *newMatrix(int rows, int cols)
{
    ObjMatrix *matrix = ALLOCATE_OBJ(ObjMatrix, OBJ_MATRIX);
    matrix->rows = rows;
    matrix->cols = cols;
    size_t dataBytes = (size_t)rows * (size_t)cols * sizeof(double);
    matrix->data = (double *)malloc(dataBytes);
    if (matrix->data != NULL)
    {
        memset(matrix->data, 0, dataBytes);
        // Payload is malloc'd outside reallocate — still count it so GC can keep up.
        vm.bytesAllocated += dataBytes;
    }
    return matrix;
}

ObjArray *newArray(void)
{
    ObjArray *array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    array->count = 0;
    array->capacity = 0;
    array->values = NULL;
    return array;
}

ObjClass *newClass(ObjString *name)
{
    push(OBJ_VAL(name));
    ObjClass *klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    initTable(&klass->methods);
    pop();
    return klass;
}

ObjMap *newMap(void)
{
    ObjMap *map = ALLOCATE_OBJ(ObjMap, OBJ_MAP);
    initTable(&map->entries);
    return map;
}

ObjClosure *newClosure(ObjFunction *function)
{
    push(OBJ_VAL(function));
    ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++)
    {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    closure->env = NULL;
    pop();
    return closure;
}

ObjFunction *newFunction(void)
{
    ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->isVariadic = false;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjInstance *newInstance(ObjClass *klass)
{
    push(OBJ_VAL(klass));
    ObjInstance *instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    pop();
    return instance;
}

ObjNative *newNative(NativeFn function)
{
    ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjModule *newModule(ObjString *name)
{
    push(OBJ_VAL(name));
    ObjModule *module = ALLOCATE_OBJ(ObjModule, OBJ_MODULE);
    module->name = name;
    module->env = NULL;
    initTable(&module->fields);
    pop();
    return module;
}

static uint32_t hashString(const char *key, int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}
// create ObjString from already-allocated char array
static ObjString *allocateString(char *chars, int length, uint32_t hash)
{
    ObjString *string = ALLOCATE_OBJ(ObjString, OBJ_STRING); // allocate ObjString struct
    string->length = length;                                 // store length
    string->chars = chars;                                   // store pointer to char array
    string->hash = hash;

    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();

    return string;
}

ObjString *takeString(char *chars, int length)
{
    uint32_t hash = hashString(chars, length); // compute hash

    // check if already interned
    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL)
    {
        FREE_ARRAY(char, chars, length + 1); // free - won't use it
        return interned;
    }

    // allocateString already interns into vm.strings
    return allocateString(chars, length, hash);
}

// copy chars from source into new heap allocation, create ObjString
ObjString *copyString(const char *chars, int length)
{
    uint32_t hash = hashString(chars, length);

    ObjString *interned = tableFindString(&vm.strings, chars, length, hash);

    if (interned != NULL)
        return interned;

    char *heapChars = ALLOCATE(char, length + 1);   // allocate fresh char array
    memcpy(heapChars, chars, length);               // copy chars from source
    heapChars[length] = '\0';                       // null terminate
    return allocateString(heapChars, length, hash); // create ObjString
}

ObjUpvalue *newUpvalue(Value *slot)
{
    ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

static void printFunction(ObjFunction *function)
{
    if (function->name == NULL)
    {
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void printObject(Value value)
{
    switch (OBJ_TYPE(value))
    {
    case OBJ_CLASS:
    {
        printf("%s", AS_CLASS(value)->name->chars);
        break;
    }
    case OBJ_MATRIX:
    {
        ObjMatrix *m = AS_MATRIX(value);
        printf("<matrix %dx%d>", m->rows, m->cols);
        break;
    }
    case OBJ_ARRAY:
    {
        ObjArray *array = AS_ARRAY(value);
        printf("#[");
        for (int i = 0; i < array->count; i++)
        {
            if (IS_STRING(array->values[i]))
            {
                printf("\"%s\"", AS_CSTRING(array->values[i]));
            }
            else
            {
                printValue(array->values[i]);
            }
            if (i < array->count - 1)
                printf(", ");
        }
        printf("]");
        break;
    }
    case OBJ_MAP:
    {
        ObjMap *map = AS_MAP(value);
        printf("{");
        bool first = true;
        for (int i = 0; i <= map->entries.capacity; i++)
        {
            Entry *entry = &map->entries.entries[i];
            if (entry->key == NULL)
                continue;
            if (!first)
                printf(", ");
            printf("\"%s\":", entry->key->chars);
            printValue(entry->value);
            first = false;
        }
        printf("}");
        break;
    }
    case OBJ_BOUND_METHOD:
        printFunction(AS_BOUND_METHOD(value)->method->function);
        break;
    case OBJ_CLOSURE:
        printFunction(AS_CLOSURE(value)->function);
        break;
    case OBJ_FUNCTION:
    {
        printFunction(AS_FUNCTION(value));
        break;
    }
    case OBJ_INSTANCE:
    {
        printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
        break;
    }
    case OBJ_MODULE:
    {
        printf("<module %s>", AS_MODULE(value)->name->chars);
        break;
    }
    case OBJ_NATIVE:
    {
        printf("<native fn>");
        break;
    }
    case OBJ_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    case OBJ_UPVALUE:
        printf("upvalue");
        break;
    }
}
