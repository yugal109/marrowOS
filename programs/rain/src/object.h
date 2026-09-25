#ifndef rain_object_h
#define rain_object_h

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

typedef enum
{
    OBJ_ARRAY,
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_MATRIX,
    OBJ_MODULE,
    OBJ_NATIVE,
    OBJ_UPVALUE,
    OBJ_MAP,
    OBJ_STRING // heap allocated string object
} ObjType;

struct Obj
{
    ObjType type; // what kind of object - string,function,class.....
    bool isMarked;
    Obj *next;
};

typedef struct
{
    Obj obj;
    int rows;
    int cols;
    double *data;
} ObjMatrix;

typedef struct
{
    Obj obj;
    Table entries;
} ObjMap;

typedef struct
{
    Obj obj;
    ObjString *name;
    Table fields;
    Table *env; // isolated module env; kept for closure upvalues
} ObjModule;

typedef struct
{
    Obj obj;
    int count;
    int capacity;
    Value *values;
} ObjArray;

typedef struct
{
    Obj obj;
    int arity; // no of params
    bool isVariadic;
    int upvalueCount;
    Chunk chunk;     // the function's bytecode ( each function's own bytecode, separate chunk)
    ObjString *name; // function name

} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value *args);

typedef struct
{
    Obj obj;
    NativeFn function;
} ObjNative;

struct ObjString
{
    Obj obj;
    int length;
    char *chars;
    uint32_t hash;
};

typedef struct ObjUpvalue
{
    Obj obj;
    Value *location;
    Value closed;
    struct ObjUpvalue *next;
} ObjUpvalue;

typedef struct
{
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalueCount;
    Table *env;
} ObjClosure;

typedef struct
{
    Obj obj;
    ObjString *name;
    Table methods;
} ObjClass;

typedef struct
{
    Obj obj;
    ObjClass *klass;
    Table fields;
} ObjInstance;

typedef struct
{
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

#define ALLOCATE_OBJ(type, objectType) \
    (type *)allocateObject(sizeof(type), objectType) // allocate specific heap object type

ObjMatrix *newMatrix(int rows, int cols);
ObjMap *newMap(void);
ObjModule *newModule(ObjString *name);
ObjArray *newArray(void);
ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method);
ObjClass *newClass(ObjString *name);
ObjClosure *newClosure(ObjFunction *function);
ObjFunction *newFunction(void);
ObjInstance *newInstance(ObjClass *klass);
ObjNative *newNative(NativeFn function);
ObjString *takeString(char *chars, int length);
ObjString *copyString(const char *chars, int length); // copy chars into new ObjString
ObjUpvalue *newUpvalue(Value *slot);
void printObject(Value value); // print a heap object

// inline function — safe version of IS_STRING check
// function not macro because value must only be evaluated once
static inline bool isObjType(Value value, ObjType type)
{
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#define OBJ_TYPE(value) (AS_OBJ(value)->type) // get type tag from heap object

#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_ARRAY(value) isObjType(value, OBJ_ARRAY)
#define IS_MODULE(value) isObjType(value, OBJ_MODULE)
#define IS_MATRIX(value) isObjType(value, OBJ_MATRIX)
#define IS_MAP(value) isObjType(value, OBJ_MAP)

#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_ARRAY(value) ((ObjArray *)AS_OBJ(value))
#define AS_MODULE(value) ((ObjModule *)AS_OBJ(value))
#define AS_MATRIX(value) ((ObjMatrix *)AS_OBJ(value))
#define AS_MAP(value) ((ObjMap *)AS_OBJ(value))
#define AS_NATIVE(value) \
    (((ObjNative *)AS_OBJ(value))->function)
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

#endif
