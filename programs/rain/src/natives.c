#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "natives.h"
#include "memory.h"
#include "vm.h"
#include "vm_internal.h"
#include "object.h"
#include "value.h"
#include "table.h"
#include "compiler.h"
#include "literals.h"
#include "errors_vm.h"
#include "builtins.h"

void defineNative(const char *name, NativeFn function)
{
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

static Value clockNative(int argCount, Value *args)
{
    (void)argCount;
    (void)args;
    return NUMBER_VAL(rain_clock());
}

static Value toStringNative(int argCount, Value *args)
{
    if (argCount != 1)
        return NIL_VAL;

    Value val = args[0];
    char buf[256];

    if (IS_STRING(val))
        return val;

    if (IS_NUMBER(val))
    {
        snprintf(buf, sizeof(buf), "%g", AS_NUMBER(val));
        return OBJ_VAL(copyString(buf, (int)strlen(buf)));
    }

    if (IS_BOOL(val))
    {
        const char *s = AS_BOOL(val) ? RAIN_LIT_TRUE : RAIN_LIT_FALSE;
        return OBJ_VAL(copyString(s, (int)strlen(s)));
    }

    if (IS_NIL(val))
        return OBJ_VAL(copyString(RAIN_LIT_NIL, (int)strlen(RAIN_LIT_NIL)));

    if (IS_ARRAY(val))
    {
        ObjArray *arr = AS_ARRAY(val);
        char result[4096];
        int len = 0;
        len += snprintf(result + len, sizeof(result) - len, "#[");
        for (int i = 0; i < arr->count; i++)
        {
            if (i > 0)
                len += snprintf(result + len, sizeof(result) - len, ", ");
            if (IS_STRING(arr->values[i]))
                len += snprintf(result + len, sizeof(result) - len, "\"%s\"", AS_CSTRING(arr->values[i]));
            else if (IS_NUMBER(arr->values[i]))
                len += snprintf(result + len, sizeof(result) - len, "%g", AS_NUMBER(arr->values[i]));
            else if (IS_BOOL(arr->values[i]))
                len += snprintf(result + len, sizeof(result) - len, "%s", AS_BOOL(arr->values[i]) ? RAIN_LIT_TRUE : RAIN_LIT_FALSE);
            else if (IS_NIL(arr->values[i]))
                len += snprintf(result + len, sizeof(result) - len, "%s", RAIN_LIT_NIL);
        }
        len += snprintf(result + len, sizeof(result) - len, "]");
        return OBJ_VAL(copyString(result, len));
    }

    if (IS_MAP(val))
    {
        ObjMap *map = AS_MAP(val);
        char result[4096];
        int len = 0;
        bool first = true;
        len += snprintf(result + len, sizeof(result) - len, "{");
        for (int i = 0; i <= map->entries.capacity; i++)
        {
            Entry *entry = &map->entries.entries[i];
            if (entry->key == NULL)
                continue;
            if (!first)
                len += snprintf(result + len, sizeof(result) - len, ", ");
            len += snprintf(result + len, sizeof(result) - len, "\"%s\": ", entry->key->chars);
            if (IS_STRING(entry->value))
                len += snprintf(result + len, sizeof(result) - len, "\"%s\"", AS_CSTRING(entry->value));
            else if (IS_NUMBER(entry->value))
                len += snprintf(result + len, sizeof(result) - len, "%g", AS_NUMBER(entry->value));
            else if (IS_BOOL(entry->value))
                len += snprintf(result + len, sizeof(result) - len, "%s", AS_BOOL(entry->value) ? RAIN_LIT_TRUE : RAIN_LIT_FALSE);
            else if (IS_NIL(entry->value))
                len += snprintf(result + len, sizeof(result) - len, "%s", RAIN_LIT_NIL);
            first = false;
        }
        len += snprintf(result + len, sizeof(result) - len, "}");
        return OBJ_VAL(copyString(result, len));
    }

    return OBJ_VAL(copyString(RAIN_LIT_OBJECT, (int)strlen(RAIN_LIT_OBJECT)));
}

static Value typeOfNative(int argCount, Value *args)
{
    if (argCount != 1)
        return NIL_VAL;

    return OBJ_VAL(valueTypeString(args[0]));
}

static Value isTypeNative(int argCount, Value *args)
{
    if (argCount != 2 || !IS_STRING(args[1]))
        return BOOL_VAL(false);

    return BOOL_VAL(valueIsType(args[0], AS_CSTRING(args[1])));
}

static Value assertTypeNative(int argCount, Value *args)
{
    if (argCount != 2)
    {
        runtimeError(RAIN_ERR_ASSERT_TYPE_ARGS);
        return NIL_VAL;
    }

    if (!IS_STRING(args[1]))
    {
        runtimeError(RAIN_ERR_ASSERT_TYPE_NAME);
        return NIL_VAL;
    }

    const char *expected = AS_CSTRING(args[1]);
    if (!valueIsType(args[0], expected))
    {
        runtimeError(RAIN_ERR_ASSERT_TYPE_MISMATCH, expected, valueTypeName(args[0]));
        return NIL_VAL;
    }

    return args[0];
}

static void registerModuleLoader(const char *name, NativeFn loader)
{
    ObjString *key = copyString(name, (int)strlen(name));
    push(OBJ_VAL(key));
    ObjNative *native = newNative(loader);
    push(OBJ_VAL(native));
    tableSet(&vm.preload, key, OBJ_VAL(native));
    pop();
    pop();
}

// Reads a .rn file for `benutzen`. The platform decides where files come from
static char *readFile(const char *path)
{
    return rain_read_file(path);
}

static ObjString *moduleNameFromPath(const char *path, int length)
{
    int start = 0;
    for (int i = length - 1; i >= 0; i--)
    {
        // find last '/' or start

        if (path[i] == '/')
        {
            start = i + 1;
            break;
        }
    }
    int end = length;
    if (length > 3 && path[length - 3] == '.' && path[length - 2] == 'r' && path[length - 1] == 'n')
    {
        end = length - 3;
    }
    return copyString(path + start, end - start);
}

static Value importNative(int argCount, Value *args)
{
    if (argCount != 1 || !IS_STRING(args[0]))
        return NIL_VAL;

    ObjString *path = AS_STRING(args[0]);
    int pathLen = path->length;

    bool isFile = pathLen > 3 &&
                  path->chars[pathLen - 3] == '.' &&
                  path->chars[pathLen - 2] == 'r' &&
                  path->chars[pathLen - 1] == 'n';

    if (isFile)
    {
        ObjString *moduleName = moduleNameFromPath(path->chars, pathLen);

        Value cached;
        if (tableGet(&vm.importedModules, moduleName, &cached))
            return cached;

        Value loading;
        if (tableGet(&vm.loadingModules, moduleName, &loading))
            return NIL_VAL;

        tableSet(&vm.loadingModules, moduleName, BOOL_VAL(true));

        char *source = readFile(path->chars);
        if (source == NULL)
        {
            tableDelete(&vm.loadingModules, moduleName);
            return NIL_VAL;
        }

        ObjModule *module = newModule(moduleName);
        push(OBJ_VAL(module));

        ObjFunction *function = compile(source);
        free(source);

        if (function == NULL)
        {
            pop();
            tableDelete(&vm.loadingModules, moduleName);
            return NIL_VAL;
        }

        ObjClosure *closure = newClosure(function);
        push(OBJ_VAL(closure));

        // allocate fresh env for this module — only natives from vm.globals
        Table *moduleEnv = ALLOCATE(Table, 1);
        initTable(moduleEnv);

        for (int i = 0; i <= vm.globals.capacity; i++)
        {
            Entry *entry = &vm.globals.entries[i];
            if (entry->key == NULL)
                continue;
            if (!IS_OBJ(entry->value))
                continue;
            if (OBJ_TYPE(entry->value) != OBJ_NATIVE)
                continue;
            tableSet(moduleEnv, entry->key, entry->value);
        }

        int savedFrameCount = vm.frameCount;
        int frameBase = vm.frameCount;
        callFunction(closure, 0);

        // override inherited env with module's fresh isolated env
        vm.frames[vm.frameCount - 1].env = moduleEnv;

        InterpretResult result = run(frameBase);

        if (result != INTERPRET_OK)
        {
            pop(); // closure
            pop(); // module
            freeTable(moduleEnv);
            FREE(Table, moduleEnv);
            tableDelete(&vm.loadingModules, moduleName);
            vm.frameCount = savedFrameCount; // restore parent frames
            return NIL_VAL;
        }

        module->env = moduleEnv;

        // harvest exports into module->fields — skip natives
        for (int i = 0; i <= moduleEnv->capacity; i++)
        {
            Entry *entry = &moduleEnv->entries[i];
            if (entry->key == NULL)
                continue;
            if (IS_OBJ(entry->value) && OBJ_TYPE(entry->value) == OBJ_NATIVE)
                continue;
            tableSet(&module->fields, entry->key, entry->value);
        }

        tableSet(&vm.importedModules, moduleName, OBJ_VAL(module));
        tableDelete(&vm.loadingModules, moduleName);

        pop(); // closure
        pop(); // module
        return OBJ_VAL(module);
    }
    else
    {
        Value cached;
        if (tableGet(&vm.importedModules, path, &cached))
            return cached;

        Value loader;
        if (!tableGet(&vm.preload, path, &loader))
            return NIL_VAL;

        NativeFn loaderFn = AS_NATIVE(loader);
        Value module = loaderFn(0, NULL);

        push(module);
        tableSet(&vm.importedModules, path, module);
        pop();
        return module;
    }
}

void registerNatives(void)
{
    defineNative(RAIN_BUILTIN_CLOCK, clockNative);
    defineNative(RAIN_BUILTIN_VAKYA, toStringNative);
    defineNative(RAIN_BUILTIN_TO_STRING, toStringNative);
    defineNative(RAIN_BUILTIN_TYPE_OF, typeOfNative);
    defineNative(RAIN_BUILTIN_IS_TYPE, isTypeNative);
    defineNative(RAIN_BUILTIN_ASSERT_TYPE, assertTypeNative);
    defineNative(RAIN_BUILTIN_IMPORT, importNative);

}
