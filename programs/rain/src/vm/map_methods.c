#include <string.h>
#include "map_methods.h"
#include "vm.h"
#include "vm_internal.h"
#include "../constants/map_methods.h"
#include "../constants/errors_vm.h"
#include "../memory.h"
#include "../object.h"
#include "../table.h"

bool invokeMap(ObjMap *map, ObjString *name, int argCount)
{
    if (strcmp(name->chars, RAIN_MAP_KEYS) == 0)
    {
        if (argCount != 0)
        {
            runtimeError("'keys' expects 0 arguments.");
            return false;
        }
        ObjArray *arr = newArray();
        push(OBJ_VAL(arr));
        for (int i = 0; i <= map->entries.capacity; i++)
        {
            Entry *entry = &map->entries.entries[i];
            if (entry->key == NULL)
                continue;
            if (arr->count == arr->capacity)
            {
                int oldCap = arr->capacity;
                arr->capacity = GROW_CAPACITY(oldCap);
                arr->values = GROW_ARRAY(Value, arr->values, oldCap, arr->capacity);
            }
            arr->values[arr->count++] = OBJ_VAL(entry->key);
        }
        pop();
        vm.stackTop -= argCount + 1;
        push(OBJ_VAL(arr));
        return true;
    }

    if (strcmp(name->chars, RAIN_MAP_VALUES) == 0)
    {
        if (argCount != 0)
        {
            runtimeError("'values' expects 0 arguments.");
            return false;
        }
        ObjArray *arr = newArray();
        push(OBJ_VAL(arr));
        for (int i = 0; i <= map->entries.capacity; i++)
        {
            Entry *entry = &map->entries.entries[i];
            if (entry->key == NULL)
                continue;
            if (arr->count == arr->capacity)
            {
                int oldCap = arr->capacity;
                arr->capacity = GROW_CAPACITY(oldCap);
                arr->values = GROW_ARRAY(Value, arr->values, oldCap, arr->capacity);
            }
            arr->values[arr->count++] = entry->value;
        }
        pop();
        vm.stackTop -= argCount + 1;
        push(OBJ_VAL(arr));
        return true;
    }

    if (strcmp(name->chars, RAIN_MAP_HAS) == 0)
    {
        if (argCount != 1)
        {
            runtimeError("'has' expects 1 argument.");
            return false;
        }
        if (!IS_STRING(peek(0)))
        {
            runtimeError("'has' expects a string key.");
            return false;
        }
        ObjString *key = AS_STRING(peek(0));
        Value dummy;
        bool found = tableGet(&map->entries, key, &dummy);
        vm.stackTop -= argCount + 1;
        push(BOOL_VAL(found));
        return true;
    }

    if (strcmp(name->chars, RAIN_MAP_DELETE) == 0)
    {
        if (argCount != 1)
        {
            runtimeError("'delete' expects 1 argument.");
            return false;
        }
        if (!IS_STRING(peek(0)))
        {
            runtimeError("'delete' expects a string key.");
            return false;
        }
        ObjString *key = AS_STRING(peek(0));
        tableDelete(&map->entries, key);
        vm.stackTop -= argCount + 1;
        push(NIL_VAL);
        return true;
    }

    if (strcmp(name->chars, RAIN_MAP_LEN) == 0)
    {
        if (argCount != 0)
        {
            runtimeError("'len' expects 0 arguments.");
            return false;
        }
        int count = 0;
        for (int i = 0; i <= map->entries.capacity; i++)
        {
            if (map->entries.entries[i].key != NULL)
                count++;
        }
        vm.stackTop -= argCount + 1;
        push(NUMBER_VAL((double)count));
        return true;
    }

    runtimeError("Map has no method '%s'.", name->chars);
    return false;
}
