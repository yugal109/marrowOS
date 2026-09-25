#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm_internal.h"
#include "memory.h"
#include "errors_vm.h"
#include "array_methods.h"

bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount);
static int compareValues(const void *a, const void *b);
static bool invokeArray(ObjArray *array, ObjString *name, int argCount);
bool invokeMap(ObjMap *map, ObjString *name, int argCount);

bool callFunction(ObjClosure *closure, int argCount)
{
    if (closure->function->isVariadic)
    {
        // must have at least arity-1 args (all required params before ...)
        int required = closure->function->arity - 1;
        if (argCount < required)
        {
            runtimeError(RAIN_ERR_CALL_ARITY, required, argCount);
            return false;
        }
    }
    else
    {
        if (argCount != closure->function->arity)
        {
            runtimeError(RAIN_ERR_CALL_ARITY, closure->function->arity, argCount);
            return false;
        }
    }

    if (vm.frameCount == FRAMES_MAX)
    {
        runtimeError(RAIN_ERR_STACK_OVERFLOW);
        return false;
    }

    if (closure->function->isVariadic)
    {
        int required = closure->function->arity - 1;
        int extraCount = argCount - required;

        // collect extra args into array
        ObjArray *varArray = newArray();
        push(OBJ_VAL(varArray)); // GC safety

        varArray->capacity = extraCount;
        varArray->values = ALLOCATE(Value, extraCount);
        varArray->count = extraCount;

        // extra args are on stack at: stackTop - extraCount ... stackTop - 1
        for (int i = 0; i < extraCount; i++)
        {
            varArray->values[i] = peek(extraCount - i);
        }

        pop(); // GC guard

        // pop extra args off stack
        vm.stackTop -= extraCount;

        // push the array as the variadic argument
        push(OBJ_VAL(varArray));
    }

    CallFrame *frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - closure->function->arity - 1;

    if (vm.frameCount > 1)
    {
        frame->env = closure->env != NULL ? closure->env : &vm.globals;
    }
    else
    {
        frame->env = &vm.globals;
    }

    return true;
}

ObjUpvalue *captureUpvalue(Value *local)
{
    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm.openUpvalues;

    while (upvalue != NULL && upvalue->location > local)
    {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local)
    {
        return upvalue;
    }

    ObjUpvalue *createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL)
    {
        vm.openUpvalues = createdUpvalue;
    }
    else
    {
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

void closeUpvalues(Value *last)
{
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last)
    {
        ObjUpvalue *upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

void defineMethod(ObjString *name)
{
    Value method = peek(0);
    ObjClass *klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

bool callValue(Value callee, int argCount)
{
    if (IS_OBJ(callee))
    {
        switch (OBJ_TYPE(callee))
        {
        case OBJ_BOUND_METHOD:
        {
            ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
            vm.stackTop[-argCount - 1] = bound->receiver;
            return callFunction(bound->method, argCount);
        }
        case OBJ_CLASS:
        {
            ObjClass *klass = AS_CLASS(callee);
            vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
            Value initializer;
            if (tableGet(&klass->methods, vm.initString, &initializer))
            {
                return callFunction(AS_CLOSURE(initializer), argCount);
            }
            else if (argCount != 0)
            {
                runtimeError(RAIN_ERR_CALL_ZERO_ARGS, argCount);
                return false;
            }
            return true;
        }
        case OBJ_CLOSURE:
            return callFunction(AS_CLOSURE(callee), argCount);
        case OBJ_NATIVE:
        {
            NativeFn native = AS_NATIVE(callee);
            Value result = native(argCount, vm.stackTop - argCount);
            if (vm.exceptionCaught || vm.frameCount == 0)
                return false;
            vm.stackTop -= argCount + 1;
            push(result);
            return true;
        }
        default:
            // non-callable object type
            break;
        }
    }
    runtimeError(RAIN_ERR_NOT_CALLABLE);
    return false;
}

bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount)
{
    Value method;
    if (!tableGet(&klass->methods, name, &method))
    {
        runtimeError(RAIN_ERR_UNDEFINED_PROPERTY, name->chars);
        return false;
    }
    return callFunction(AS_CLOSURE(method), argCount);
}

static int compareValues(const void *a, const void *b)
{
    Value va = *(Value *)a;
    Value vb = *(Value *)b;

    if (IS_NUMBER(va) && IS_NUMBER(vb))
    {
        double da = AS_NUMBER(va);
        double db = AS_NUMBER(vb);
        if (da < db)
            return -1;
        if (da > db)
            return 1;
        return 0;
    }

    if (IS_STRING(va) && IS_STRING(vb))
    {
        return strcmp(AS_CSTRING(va), AS_CSTRING(vb));
    }

    return 0;
}

static bool invokeArray(ObjArray *array, ObjString *name, int argCount)
{

    if (strcmp(name->chars, RAIN_ARR_LEN) == 0)
    {
        if (argCount != 0)
        {
            runtimeError(RAIN_ERR_ARR_LEN_ARGS);
            return false;
        }
        vm.stackTop -= argCount + 1;
        push(NUMBER_VAL(array->count));
        return true;
    }

    if (strcmp(name->chars, RAIN_ARR_PUSH) == 0)
    {
        if (argCount != 1)
        {
            runtimeError(RAIN_ERR_ARR_PUSH_ARGS);
            return false;
        }
        Value value = peek(0);
        if (array->count == array->capacity)
        {
            int oldCapacity = array->capacity;
            array->capacity = GROW_CAPACITY(oldCapacity);
            array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
        }
        array->values[array->count++] = value;
        vm.stackTop -= argCount + 1;
        push(OBJ_VAL(array));
        return true;
    }

    if (strcmp(name->chars, RAIN_ARR_POP) == 0)
    {
        if (argCount != 0)
        {
            runtimeError(RAIN_ERR_ARR_POP_ARGS);
            return false;
        }
        if (array->count == 0)
        {
            runtimeError(RAIN_ERR_ARR_POP_EMPTY);
            return false;
        }
        Value popped = array->values[--array->count];
        vm.stackTop -= argCount + 1;
        push(popped);
        return true;
    }

    if (strcmp(name->chars, RAIN_ARR_CONTAINS) == 0)
    {
        if (argCount != 1)
        {
            runtimeError(RAIN_ERR_ARR_CONTAINS_ARGS);
            return false;
        }
        Value target = peek(0);
        bool found = false;
        for (int i = 0; i < array->count; i++)
        {
            if (valuesEqual(array->values[i], target))
            {
                found = true;
                break;
            }
        }
        vm.stackTop -= argCount + 1;
        push(BOOL_VAL(found));
        return true;
    }

    if (strcmp(name->chars, RAIN_ARR_REVERSE) == 0)
    {
        if (argCount != 0)
        {
            runtimeError(RAIN_ERR_ARR_REVERSE_ARGS);
            return false;
        }
        int left = 0;
        int right = array->count - 1;
        while (left < right)
        {
            Value temp = array->values[left];
            array->values[left] = array->values[right];
            array->values[right] = temp;
            left++;
            right--;
        }
        vm.stackTop -= argCount + 1;
        push(OBJ_VAL(array));
        return true;
    }

    if (strcmp(name->chars, RAIN_ARR_SORT) == 0)
    {
        if (argCount != 0)
        {
            runtimeError(RAIN_ERR_ARR_SORT_ARGS);
            return false;
        }
        qsort(array->values, array->count, sizeof(Value), compareValues);
        vm.stackTop -= argCount + 1;
        push(OBJ_VAL(array));
        return true;
    }

    if (name->length == 5 && memcmp(name->chars, RAIN_ARR_SLICE, 5) == 0)
    {
        if (argCount != 2)
        {
            runtimeError(RAIN_ERR_ARR_SLICE_ARGS);
            return false;
        }
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1)))
        {
            runtimeError(RAIN_ERR_ARR_SLICE_NUMBERS);
            return false;
        }

        int end = (int)AS_NUMBER(peek(0));
        int start = (int)AS_NUMBER(peek(1));

        if (start < 0 || end > array->count || start > end)
        {
            runtimeError(RAIN_ERR_ARR_SLICE_BOUNDS);
            return false;
        }

        ObjArray *result = newArray();
        push(OBJ_VAL(result)); // GC safety

        int length = end - start;
        result->capacity = length;
        result->values = ALLOCATE(Value, length); // count stays 0 during alloc so GC won't trace uninit slots
        for (int i = 0; i < length; i++)
            result->values[i] = array->values[start + i];
        result->count = length; // set count only after all slots are valid

        pop(); // pop GC guard
        vm.stackTop -= argCount + 1;
        push(OBJ_VAL(result));
        return true;
    }

    runtimeError(RAIN_ERR_ARR_UNKNOWN_METHOD, name->chars);
    return false;
}

bool invoke(ObjString *name, int argCount)
{
    Value receiver = peek(argCount);

    if (IS_ARRAY(receiver))
    {
        ObjArray *array = AS_ARRAY(receiver);
        return invokeArray(array, name, argCount);
    }

    if (IS_MAP(receiver))
    {
        ObjMap *map = AS_MAP(receiver);
        return invokeMap(map, name, argCount);
    }

    if (!IS_INSTANCE(receiver))
    {
        runtimeError(RAIN_ERR_ONLY_INSTANCES_METHODS);
        return false;
    }

    ObjInstance *instance = AS_INSTANCE(receiver);
    Value value;
    if (tableGet(&instance->fields, name, &value))
    {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }
    return invokeFromClass(instance->klass, name, argCount);
}

bool bindMethod(ObjClass *klass, ObjString *name)
{
    Value method;
    if (!tableGet(&klass->methods, name, &method))
    {
        runtimeError(RAIN_ERR_UNDEFINED_PROPERTY, name->chars);
        return false;
    }

    ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));

    pop();
    push(OBJ_VAL(bound));
    return true;
}
