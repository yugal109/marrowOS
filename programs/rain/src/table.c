#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table *table)
{
    table->count = 0;
    table->capacity = -1;
    table->entries = NULL;
}

void freeTable(Table *table)
{
    FREE_ARRAY(Entry, table->entries, table->capacity + 1);
    initTable(table);
}

static Entry *findEntry(Entry *entries, int capacity, ObjString *key)
{
    uint32_t index = key->hash & capacity;
    Entry *tombstone = NULL;
    for (;;)
    {
        Entry *entry = &entries[index];
        if (entry->key == NULL)
        {
            if (IS_NIL(entry->value))
            {
                // truly empty bucket -> stop
                return tombstone != NULL ? tombstone : entry;
            }
            else
            {
                // tombstone -> remember it , keep probing
                if (tombstone == NULL)
                    tombstone = entry;
            }
        }
        else if (entry->key == key)
        {
            return entry;
        }

        index = (index + 1) & capacity;
    }
}

bool tableGet(Table *table, ObjString *key, Value *value)
{
    if (table->count == 0)
        return false;
    Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    *value = entry->value;
    return true;
}

static void adjustCapacity(Table *table, int capacity)
{

    // allocate new empty array
    Entry *entries = ALLOCATE(Entry, capacity + 1);
    for (int i = 0; i <= capacity; i++)
    {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    // re-insert all existing entries into  new array
    table->count = 0;
    for (int i = 0; i <= table->capacity; i++)
    {
        Entry *entry = &table->entries[i];
        if (entry->key == NULL)
            continue;
        Entry *dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity + 1);

    table->entries = entries;
    table->capacity = capacity;
}

bool tableSet(Table *table, ObjString *key, Value value)
{
    if (table->count + 1 > (table->capacity + 1) * TABLE_MAX_LOAD)
    {
        int capacity = GROW_CAPACITY(table->capacity + 1) - 1;
        adjustCapacity(table, capacity);
    }

    Entry *entry = findEntry(table->entries, table->capacity, key);

    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value))
        table->count++;
    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table *table, ObjString *key)
{
    if (table->count == 0)
        return false;

    Entry *entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    // Tombstone: keep count as-is so load factor still triggers grow/rehash
    // and clears tombstones. Decrementing count here lets tombstone slots fill
    // with live keys while count under-reports, so tableDelete can no-op during
    // GC (count==0) and leave dangling pointers in the intern table.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);

    return true;
}

void tableAddAll(Table *from, Table *to)
{
    for (int i = 0; i <= from->capacity; i++)
    {
        Entry *entry = &from->entries[i];
        if (entry->key != NULL)
        {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString *tableFindString(Table *table, const char *chars, int length, uint32_t hash)
{
    if (table->count == 0)
        return NULL;

    uint32_t index = hash & table->capacity;

    for (;;)
    {
        Entry *entry = &table->entries[index];

        if (entry->key == NULL)
        {
            if (IS_NIL(entry->value))
                return NULL; // truly empty -> not found
            // tombstone -> keep probing
        }
        else if (entry->key->length == length &&
                 entry->key->hash == hash &&
                 memcmp(entry->key->chars, chars, length) == 0)
        {
            return entry->key; // found matching string
        }

        index = (index + 1) & table->capacity; // linear probe
    }
}

void tableRemoveWhite(Table *table)
{
    for (int i = 0; i <= table->capacity; i++)
    {
        Entry *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.isMarked)
        {
            tableDelete(table, entry->key);
        }
    }
}

void markTable(Table *table)
{
    for (int i = 0; i <= table->capacity; i++)
    {
        Entry *entry = &table->entries[i];
        markObject((Obj *)entry->key);
        markValue(entry->value);
    }
}
