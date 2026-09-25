#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "object.h"
#include "literals.h"
#include "types.h"

void initValueArray(ValueArray *array)
{
    array->count = 0;
    array->capacity = 0;
    array->values = NULL;
}

void writeValueArray(ValueArray *array, Value value)
{
    if (array->capacity < array->count + 1)
    {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray *array)
{
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value)
{
#ifdef NAN_BOXING
    if (IS_BOOL(value))
    {
        printf(AS_BOOL(value) ? RAIN_LIT_TRUE : RAIN_LIT_FALSE);
    }
    else if (IS_NIL(value))
    {
        printf(RAIN_LIT_NIL);
    }
    else if (IS_NUMBER(value))
    {
        printf("%g", AS_NUMBER(value));
    }
    else if (IS_OBJ(value))
    {
        printObject(value);
    }
#else
    switch (value.type)
    {
    case VAL_BOOL:
        printf("%s", AS_BOOL(value) ? RAIN_LIT_TRUE : RAIN_LIT_FALSE);
        break;
    case VAL_NIL:
        printf("%s", RAIN_LIT_NIL);
        break;
    case VAL_NUMBER:
        printf("%g", AS_NUMBER(value));
        break;
    case VAL_OBJ:
        printObject(value);
        break;
    }
#endif
}

const char *valueTypeName(Value value)
{
    if (IS_NIL(value))
        return RAIN_LIT_NIL;
    if (IS_BOOL(value))
        return RAIN_TYPE_BOOL;
    if (IS_NUMBER(value))
        return RAIN_TYPE_NUMBER;
    if (IS_STRING(value))
        return RAIN_TYPE_STRING;
    if (IS_ARRAY(value))
        return RAIN_TYPE_ARRAY;
    if (IS_MAP(value))
        return RAIN_TYPE_MAP;
    if (IS_INSTANCE(value))
        return AS_INSTANCE(value)->klass->name->chars;
    if (IS_CLASS(value))
        return RAIN_TYPE_CLASS;
    if (IS_MODULE(value))
        return RAIN_TYPE_MODULE;
    if (IS_MATRIX(value))
        return RAIN_TYPE_MATRIX;
    if (IS_CLOSURE(value) || IS_FUNCTION(value) || IS_NATIVE(value) || IS_BOUND_METHOD(value))
        return RAIN_TYPE_FUNCTION;

    return RAIN_LIT_UNKNOWN;
}

bool valueIsType(Value value, const char *typeName)
{
    if (typeName == NULL)
        return false;

    if (IS_INSTANCE(value))
        return strcmp(AS_INSTANCE(value)->klass->name->chars, typeName) == 0;

    return strcmp(valueTypeName(value), typeName) == 0;
}

ObjString *valueTypeString(Value value)
{
    if (IS_INSTANCE(value))
        return AS_INSTANCE(value)->klass->name;

    const char *name = valueTypeName(value);
    return copyString(name, (int)strlen(name));
}

bool valuesEqual(Value a, Value b)
{
#ifdef NAN_BOXING
    if (IS_NUMBER(a) && IS_NUMBER(b))
    {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    return a == b;
#else
    if (a.type != b.type)
        return false;
    switch (a.type)
    {
    case VAL_BOOL:
        return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
        return true;
    case VAL_NUMBER:
        return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:
    {
        ObjString *aString = AS_STRING(a);
        ObjString *bString = AS_STRING(b);
        return aString->length == bString->length && memcmp(aString->chars, bString->chars, aString->length) == 0;
    }
    default:
        return false;
    }
#endif
}
