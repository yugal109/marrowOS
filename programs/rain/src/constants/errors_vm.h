#ifndef rain_errors_vm_h
#define rain_errors_vm_h

/* VM runtime errors — format strings for runtimeError(). */

#define RAIN_ERR_OPERANDS_NUMBERS "Operands must be numbers."
#define RAIN_ERR_UNDEFINED_VAR "Undefined variable '%s'."
#define RAIN_ERR_ONLY_INSTANCES_PROPERTIES "Only instances have properties."
#define RAIN_ERR_ONLY_INSTANCES_FIELDS "Only instances have fields."
#define RAIN_ERR_OPERANDS_NUMBERS_OR_STRINGS "Operands must be two numbers or two strings."
#define RAIN_ERR_OPERAND_NUMBER "Operand must be a number."
#define RAIN_ERR_SUPERCLASS_CLASS "Superclass must be a class."
#define RAIN_ERR_MAP_KEY_STRING "Map key must be a string."
#define RAIN_ERR_ARRAY_INDEX_NUMBER "Array index must be a number."
#define RAIN_ERR_ONLY_ARRAYS_INDEXED "Only arrays can be indexed."
#define RAIN_ERR_ARRAY_INDEX_BOUNDS "Array index out of bounds."
#define RAIN_ERR_MODULE_SCOPE_ONLY "'::' can only be used on modules."
#define RAIN_ERR_MODULE_NO_FIELD "Module '%s' has no field '%s'."

#define RAIN_ERR_CALL_ARITY "Expected %d arguments but got %d."
#define RAIN_ERR_STACK_OVERFLOW "Stack overflow."
#define RAIN_ERR_CALL_ZERO_ARGS "Expected 0 arguments but got %d."
#define RAIN_ERR_NOT_CALLABLE "Can only call functions and classes."
#define RAIN_ERR_UNDEFINED_PROPERTY "Undefined property '%s'."
#define RAIN_ERR_ONLY_INSTANCES_METHODS "Only instances have methods."

#define RAIN_ERR_ARR_LEN_ARGS "len() takes no arguments."
#define RAIN_ERR_ARR_PUSH_ARGS "push() takes exactly 1 argument."
#define RAIN_ERR_ARR_POP_ARGS "pop() takes no arguments."
#define RAIN_ERR_ARR_POP_EMPTY "Cannot pop from empty array."
#define RAIN_ERR_ARR_CONTAINS_ARGS "contains() takes exactly one argument."
#define RAIN_ERR_ARR_REVERSE_ARGS "reverse() takes no arguments."
#define RAIN_ERR_ARR_SORT_ARGS "sort() takes no arguments."
#define RAIN_ERR_ARR_SLICE_ARGS "slice() takes exactly 2 arguments."
#define RAIN_ERR_ARR_SLICE_NUMBERS "slice() arguments must be numbers."
#define RAIN_ERR_ARR_SLICE_BOUNDS "slice() index out of bounds."
#define RAIN_ERR_ARR_UNKNOWN_METHOD "Unknown array method '%s'."

#define RAIN_ERR_ASSERT_TYPE_ARGS "assertType() expects 2 arguments."
#define RAIN_ERR_ASSERT_TYPE_NAME "assertType() type name must be a string."
#define RAIN_ERR_ASSERT_TYPE_MISMATCH "TypeError: expected \"%s\", got \"%s\"."

#endif
