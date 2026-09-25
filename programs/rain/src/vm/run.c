#include <stdio.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"
#include "vm_internal.h"
#include "errors_vm.h"

InterpretResult run(int frameBase)
{
    CallFrame *frame = &vm.frames[vm.frameCount - 1];
// macros for the run loop
#define READ_BYTE() (*frame->ip++) // read current byte and advance ip
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_SHORT()]) // read next bye as constant index, look up value
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op)                          \
    do                                                    \
    {                                                     \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1)))   \
        {                                                 \
            if (!runtimeError(RAIN_ERR_OPERANDS_NUMBERS)) \
                return INTERPRET_RUNTIME_ERROR;           \
            frame = &vm.frames[vm.frameCount - 1];        \
            goto resume_after_catch;                      \
        }                                                 \
        double b = AS_NUMBER(pop());                      \
        double a = AS_NUMBER(pop());                      \
        push(valueType(a op b));                          \
    } while (false)
#define HANDLE_CALL_ERROR()                        \
    do                                             \
    {                                              \
        if (vm.exceptionCaught)                    \
        {                                          \
            vm.exceptionCaught = false;            \
            frame = &vm.frames[vm.frameCount - 1]; \
            goto resume_after_catch;               \
        }                                          \
        return INTERPRET_RUNTIME_ERROR;            \
    } while (false)

    for (;;)
    {

#ifdef DEBUG_TRACE_EXECUTION
        printf("                 ");
        for (Value *slot = vm.stack; slot < vm.stackTop; slot++)
        {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        disassembleInstruction(&frame->closure->function->chunk, (int)(frame->ip - frame->closure->function->chunk.code));
#endif
        uint8_t instruction = READ_BYTE();
        switch (instruction)
        {
        case OP_CONSTANT:
        {
            Value constant = READ_CONSTANT(); // look up constant by index
            push(constant);
            break;
        }
        case OP_NIL:
            push(NIL_VAL);
            break;
        case OP_TRUE:
            push(BOOL_VAL(true));
            break;
        case OP_FALSE:
            push(BOOL_VAL(false));
            break;
        case OP_POP:
            pop();
            break;
        case OP_GET_LOCAL:
        {
            uint8_t slot = READ_BYTE();
            push(frame->slots[slot]);
            break;
        }
        case OP_SET_LOCAL:
        {
            uint8_t slot = READ_BYTE();
            frame->slots[slot] = peek(0);
            break;
        }
        case OP_GET_GLOBAL:
        {
            ObjString *name = READ_STRING();
            Value value;
            if (!tableGet(frame->env, name, &value))
            {
                if (!runtimeError(RAIN_ERR_UNDEFINED_VAR, name->chars))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            push(value);
            break;
        }
        case OP_SET_GLOBAL:
        {
            ObjString *name = READ_STRING();
            if (tableSet(frame->env, name, peek(0)))
            {
                // tableSet returned true = key was NEW = variable never declared
                // but tableSet already inserted it! we need to undo that
                tableDelete(frame->env, name);
                if (!runtimeError(RAIN_ERR_UNDEFINED_VAR, name->chars))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            break;
        }
        case OP_DEFINE_GLOBAL:
        {
            ObjString *name = READ_STRING();
            tableSet(frame->env, name, peek(0));
            pop();
            break;
        }
        case OP_GET_UPVALUE:
        {
            uint8_t slot = READ_BYTE();
            push(*frame->closure->upvalues[slot]->location);
            break;
        }
        case OP_SET_UPVALUE:
        {
            uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->location = peek(0);
            break;
        }
        case OP_GET_PROPERTY:
        {
            if (!IS_INSTANCE(peek(0)))
            {
                if (!runtimeError(RAIN_ERR_ONLY_INSTANCES_PROPERTIES))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            ObjInstance *instance = AS_INSTANCE(peek(0));
            ObjString *name = READ_STRING();

            Value value;
            if (tableGet(&instance->fields, name, &value))
            {
                pop();
                push(value);
                break;
            }
            if (!bindMethod(instance->klass, name))
            {
                HANDLE_CALL_ERROR();
            }
            break;
        }
        case OP_GET_PROPERTY_SAFE:
        {
            if (IS_NIL(peek(0)))
            {
                pop();
                (void)READ_STRING(); // skips the operand
                push(NIL_VAL);
                break;
            }

            if (!IS_INSTANCE(peek(0)))
            {
                if (!runtimeError(RAIN_ERR_ONLY_INSTANCES_PROPERTIES))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            ObjInstance *instance = AS_INSTANCE(peek(0));
            ObjString *name = READ_STRING();
            Value value;

            if (tableGet(&instance->fields, name, &value))
            {
                pop();
                push(value);
                break;
            }

            if (!bindMethod(instance->klass, name))
            {
                HANDLE_CALL_ERROR();
            }
            break;
        }
        case OP_SET_PROPERTY:
        {
            if (!IS_INSTANCE(peek(1)))
            {
                if (!runtimeError(RAIN_ERR_ONLY_INSTANCES_FIELDS))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            ObjInstance *instance = AS_INSTANCE(peek(1));
            tableSet(&instance->fields, READ_STRING(), peek(0));

            Value value = pop();
            pop();
            push(value);
            break;
        }
        case OP_GET_SUPER:
        {
            ObjString *name = READ_STRING();
            ObjClass *superclass = AS_CLASS(pop());
            if (!bindMethod(superclass, name))
            {
                HANDLE_CALL_ERROR();
            }
            break;
        }
        case OP_EQUAL:
        {
            Value b = pop();
            Value a = pop();
            push(BOOL_VAL(valuesEqual(a, b)));
            break;
        }

        case OP_GREATER:
            BINARY_OP(BOOL_VAL, >);
            break;
        case OP_LESS:
            BINARY_OP(BOOL_VAL, <);
            break;
        case OP_ADD:
            if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
            {
                concatenate();
            }
            else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1)))
            {
                BINARY_OP(NUMBER_VAL, +);
            }
            else if (IS_STRING(peek(1)) && IS_NUMBER(peek(0)))
            {
                // Keep the string on the stack while copyString may trigger GC.
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", AS_NUMBER(peek(0)));
                ObjString *numStr = copyString(buf, (int)strlen(buf));
                pop(); // number
                push(OBJ_VAL(numStr));
                concatenate();
            }
            else if (IS_NUMBER(peek(1)) && IS_STRING(peek(0)))
            {
                // Keep both values on the stack until after copyString.
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", AS_NUMBER(peek(1)));
                ObjString *numStr = copyString(buf, (int)strlen(buf));
                ObjString *str = AS_STRING(peek(0));
                pop(); // string
                pop(); // number
                push(OBJ_VAL(numStr));
                push(OBJ_VAL(str));
                concatenate();
            }
            else if (IS_STRING(peek(1)) && IS_BOOL(peek(0)))
            {
                const char *boolStr = AS_BOOL(peek(0)) ? "true" : "false";
                ObjString *b = copyString(boolStr, (int)strlen(boolStr));
                pop(); // bool
                push(OBJ_VAL(b));
                concatenate();
            }
            else if (IS_BOOL(peek(1)) && IS_STRING(peek(0)))
            {
                const char *boolStr = AS_BOOL(peek(1)) ? "true" : "false";
                ObjString *b = copyString(boolStr, (int)strlen(boolStr));
                ObjString *str = AS_STRING(peek(0));
                pop(); // string
                pop(); // bool
                push(OBJ_VAL(b));
                push(OBJ_VAL(str));
                concatenate();
            }
            else
            {
                if (!runtimeError(RAIN_ERR_OPERANDS_NUMBERS_OR_STRINGS))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            break;
        case OP_SUBTRACT:
            BINARY_OP(NUMBER_VAL, -);
            break;
        case OP_MULTIPLY:
            BINARY_OP(NUMBER_VAL, *);
            break;
        case OP_DIVIDE:
            BINARY_OP(NUMBER_VAL, /);
            break;
        case OP_NOT:
            push(BOOL_VAL(isFalsey(pop())));
            break;
        case OP_NEGATE:
            if (!IS_NUMBER(peek(0)))
            {
                if (!runtimeError(RAIN_ERR_OPERAND_NUMBER))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            push(NUMBER_VAL(-AS_NUMBER(pop())));
            break;
        case OP_PRINT:
        {
            printValue(pop());
            printf("\n");
            break;
        }
        case OP_JUMP_IF_FALSE:
        {
            uint16_t offset = READ_SHORT();
            if (isFalsey(peek(0)))
                frame->ip += offset;
            break;
        }
        case OP_JUMP:
        {
            uint16_t offset = READ_SHORT();
            frame->ip += offset;
            break;
        }
        case OP_PUSH_EXCEPTION_HANDLER:
        {
            // read 2-byte offset to fang block
            uint16_t offset = READ_SHORT();

            // save current state
            RainExceptionHandler handler;
            handler.frameCount = vm.frameCount;
            handler.stackTop = vm.stackTop;
            handler.catchIp = frame->ip + offset; // where fang block is

            // push onto handler stack
            if (vm.handlerCount < MAX_EXCEPTION_HANDLERS)
                vm.handlerStack[vm.handlerCount++] = handler;

            break;
        }

        case OP_POP_EXCEPTION_HANDLER:
        {
            // try succeeded — remove handler
            if (vm.handlerCount > 0)
                vm.handlerCount--;
            break;
        }
        case OP_LOOP:
        {
            uint16_t offset = READ_SHORT();
            frame->ip -= offset;
            break;
        }
        case OP_CALL:
        {
            int argCount = READ_BYTE();
            if (!callValue(peek(argCount), argCount))
            {
                HANDLE_CALL_ERROR();
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_INHERIT:
        {
            Value superclass = peek(1);

            if (!IS_CLASS(superclass))
            {
                if (!runtimeError(RAIN_ERR_SUPERCLASS_CLASS))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            ObjClass *subclass = AS_CLASS(peek(0));
            tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
            pop();
            break;
        }
        case OP_METHOD:
            defineMethod(READ_STRING());
            break;
        case OP_CLASS:
        {
            push(OBJ_VAL(newClass(READ_STRING())));
            break;
        }
        case OP_INVOKE:
        {
            ObjString *method = READ_STRING();
            int argCount = READ_BYTE();
            if (!invoke(method, argCount))
            {
                HANDLE_CALL_ERROR();
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_SUPER_INVOKE:
        {
            ObjString *method = READ_STRING();
            int argCount = READ_BYTE();
            ObjClass *superclass = AS_CLASS(pop());
            if (!invokeFromClass(superclass, method, argCount))
            {
                HANDLE_CALL_ERROR();
            }
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_CLOSURE:
        {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
            ObjClosure *closure = newClosure(function);
            closure->env = frame->env;
            push(OBJ_VAL(closure));
            for (int i = 0; i < closure->upvalueCount; i++)
            {
                uint8_t isLocal = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (isLocal)
                {
                    closure->upvalues[i] = captureUpvalue(frame->slots + index);
                }
                else
                {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }

            break;
        }
        case OP_CLOSE_UPVALUE:
        {
            closeUpvalues(vm.stackTop - 1);
            pop();
            break;
        }
        case OP_RETURN:
        {
            Value result = pop();
            closeUpvalues(frame->slots);
            vm.frameCount--;
            if (vm.frameCount == frameBase)
            {
                if (frameBase == 0)
                {
                    pop(); // script closure
                    return INTERPRET_OK;
                }
                vm.lastResult = result; // ← move here
                vm.stackTop = frame->slots;
                push(result);
                return INTERPRET_OK;
            }
            vm.stackTop = frame->slots;
            push(result);
            frame = &vm.frames[vm.frameCount - 1];
            break;
        }
        case OP_ARRAY_BUILD:
        {
            int count = READ_SHORT();
            ObjArray *array = newArray();
            push(OBJ_VAL(array));

            array->capacity = count;
            array->values = ALLOCATE(Value, count);
            array->count = count;

            for (int i = 0; i < count; i++)
            {
                array->values[i] = peek(count - i);
            }

            vm.stackTop -= count + 1;
            push(OBJ_VAL(array));
            break;
        }
        case OP_MAP_BUILD:
        {
            int count = READ_SHORT();
            ObjMap *map = newMap();
            push(OBJ_VAL(map));

            for (int i = count - 1; i >= 0; i--)
            {
                Value val = peek(i * 2 + 1);
                Value key = peek(i * 2 + 2);
                if (!IS_STRING(key))
                {
                    if (!runtimeError(RAIN_ERR_MAP_KEY_STRING))
                        return INTERPRET_RUNTIME_ERROR;
                    frame = &vm.frames[vm.frameCount - 1];
                    break;
                }
                tableSet(&map->entries, AS_STRING(key), val);
            }

            pop();
            vm.stackTop -= count * 2;
            push(OBJ_VAL(map));
            break;
        }
        case OP_MAP_NEXT:
        {
            uint8_t mapSlot = READ_BYTE();
            int idx = (int)AS_NUMBER(pop());
            ObjMap *map = AS_MAP(frame->slots[mapSlot]);

            while (idx <= map->entries.capacity)
            {
                Entry *entry = &map->entries.entries[idx];
                idx++;
                if (entry->key != NULL)
                {
                    push(OBJ_VAL(entry->key));
                    push(entry->value);
                    push(NUMBER_VAL((double)idx));
                    push(BOOL_VAL(true));
                    goto map_next_done;
                }
            }
            push(NIL_VAL);
            push(NIL_VAL);
            push(NUMBER_VAL(-1));
            push(BOOL_VAL(false));
        map_next_done:
            break;
        }
        case OP_INDEX_GET:
        {

            if (IS_MAP(peek(1)))
            {
                if (!IS_STRING(peek(0)))
                {
                    if (!runtimeError(RAIN_ERR_MAP_KEY_STRING))
                        return INTERPRET_RUNTIME_ERROR;
                    frame = &vm.frames[vm.frameCount - 1];
                    break;
                }
                ObjString *key = AS_STRING(pop());
                ObjMap *map = AS_MAP(pop());
                Value value;
                if (!tableGet(&map->entries, key, &value))
                    value = NIL_VAL;
                push(value);
                break;
            }

            if (!IS_NUMBER(peek(0)))
            {
                if (!runtimeError(RAIN_ERR_ARRAY_INDEX_NUMBER))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            int index = (int)AS_NUMBER(pop());

            if (!IS_ARRAY(peek(0)))
            {
                if (!runtimeError(RAIN_ERR_ONLY_ARRAYS_INDEXED))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            ObjArray *array = AS_ARRAY(pop());
            if (index < 0 || index >= array->count)
            {
                if (!runtimeError(RAIN_ERR_ARRAY_INDEX_BOUNDS))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            push(array->values[index]);
            break;
        }
        case OP_INDEX_SET:
        {

            if (IS_MAP(peek(2)))
            {
                if (!IS_STRING(peek(1)))
                {
                    if (!runtimeError(RAIN_ERR_MAP_KEY_STRING))
                        return INTERPRET_RUNTIME_ERROR;
                    frame = &vm.frames[vm.frameCount - 1];
                    break;
                }
                tableSet(&AS_MAP(peek(2))->entries, AS_STRING(peek(1)), peek(0));
                Value result = peek(0);
                vm.stackTop -= 3;
                push(result);
                break;
            }

            if (!IS_NUMBER(peek(1)))
            {
                if (!runtimeError(RAIN_ERR_ARRAY_INDEX_NUMBER))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            Value value = pop();
            int index = (int)AS_NUMBER(pop());

            if (!IS_ARRAY(peek(0)))
            {
                if (!runtimeError(RAIN_ERR_ONLY_ARRAYS_INDEXED))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            ObjArray *array = AS_ARRAY(pop());
            if (index < 0 || index >= array->count)
            {
                if (!runtimeError(RAIN_ERR_ARRAY_INDEX_BOUNDS))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            array->values[index] = value;
            push(value);
            break;
        }
        case OP_GET_MODULE:
        {
            ObjString *field = READ_STRING();
            Value moduleVal = peek(0);

            if (!IS_MODULE(moduleVal))
            {
                if (!runtimeError(RAIN_ERR_MODULE_SCOPE_ONLY))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            ObjModule *module = AS_MODULE(moduleVal);
            Value value;
            if (!tableGet(&module->fields, field, &value))
            {
                if (!runtimeError(RAIN_ERR_MODULE_NO_FIELD, module->name->chars, field->chars))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            };
            pop();
            push(value);
            break;
        }
        }
    resume_after_catch:
        vm.exceptionCaught = false;
    }

// undefine macros after use — keep them local to this file
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
#undef HANDLE_CALL_ERROR
}
