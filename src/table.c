#include "table.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

void initTable(Table* table, double maxLoad) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
  assert(maxLoad > 0.0 && maxLoad <= 1.0); // GCOV_EXCL_LINE
  table->maxLoad = maxLoad;
}

void freeTable(GC* gc, Table* table) {
  FREE_ARRAY(gc, Entry, table->entries, table->capacity);
  initTable(table, table->maxLoad);
}

static Entry* findEntry(Entry* entries, int capacity, Value key) {
  uint32_t index = hashValue(key) & (capacity - 1);
  Entry* tombstone = NULL;

  for (int i = 0; i < capacity; i++) {
    Entry* entry = &entries[index];
    if (IS_NIL(entry->key)) {
      if (IS_NIL(entry->value)) {
        // Empty entry.
        return tombstone != NULL ? tombstone : entry;
      } else {
        // We found a tombstone.
        if (tombstone == NULL) {
          tombstone = entry;
        }
      }
    } else if (valuesEqual(entry->key, key)) {
      // We found the key.
      return entry;
    }

    index = (index + 1) & (capacity - 1);
  }

  // NOTE: This may be null if the table is filled to capacity.
  return tombstone;
}

bool tableGet(Table* table, Value key, Value* value) {
  if (table->count == 0) {
    return false;
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry == NULL || IS_NIL(entry->key)) {
    return false;
  }

  *value = entry->value;
  return true;
}

static void adjustCapacity(GC* gc, Table* table, int capacity) {
  Entry* entries = ALLOCATE(gc, Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NIL_VAL;
    entries[i].value = NIL_VAL;
  }

  table->count = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (IS_NIL(entry->key)) {
      continue;
    }

    Entry* dest = findEntry(entries, capacity, entry->key);
    assert(dest != NULL); // GCOV_EXCL_LINE
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  FREE_ARRAY(gc, Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableSet(GC* gc, Table* table, Value key, Value value) {
  assert(!IS_NIL(key)); // GCOV_EXCL_LINE

  if (table->count + 1 > table->capacity * table->maxLoad) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(gc, table, capacity);
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);
  bool isNewKey = IS_NIL(entry->key);
  if (isNewKey && IS_NIL(entry->value)) {
    table->count++;
  }

  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool tableDelete(Table* table, Value key) {
  if (table->count == 0) {
    return false;
  }

  // Find the entry.
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (IS_NIL(entry->key)) {
    return false;
  }

  // Place a tombstone in the entry.
  entry->key = NIL_VAL;
  entry->value = BOOL_VAL(true);
  return true;
}

void tableAddAll(GC* gc, Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (!IS_NIL(entry->key)) {
      tableSet(gc, to, entry->key, entry->value);
    }
  }
}

Value tableFindString(
    Table* table, const char* chars, int length, uint32_t hash) {
  uint32_t index = hash & (table->capacity - 1);
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[index];
    if (IS_NIL(entry->key)) {
      // Stop if we find an empty non-tombstone entry.
      if (IS_NIL(entry->value)) {
        return NIL_VAL;
      }
    } else if (IS_STRING(entry->key)) {
      ObjString* keyStr = AS_STRING(entry->key);
      if (keyStr->hash == hash && keyStr->length == length &&
          !memcmp(keyStr->chars, chars, length)) {
        // We found it.
        return entry->key;
      }
    }

    index = (index + 1) & (table->capacity - 1);
  }

  return NIL_VAL;
}

void tableRemoveWhite(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (IS_OBJ(entry->key) && !AS_OBJ(entry->key)->isMarked) {
      tableDelete(table, entry->key);
    }
  }
}

void markTable(GC* gc, Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    markValue(gc, entry->key);
    markValue(gc, entry->value);
  }
}
