#include "value.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"

void initValueArray(ValueArray* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

static void ensureNewSpace(GC* gc, ValueArray* array) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(
        gc, Value, array->values, oldCapacity, array->capacity);
  }
}

void writeValueArray(GC* gc, ValueArray* array, Value value) {
  ensureNewSpace(gc, array);
  array->values[array->count] = value;
  array->count++;
}

void insertValueArray(GC* gc, ValueArray* array, int pos, Value value) {
  assert(pos >= 0 && pos < array->count); // GCOV_EXCL_LINE
  ensureNewSpace(gc, array);
  memmove(array->values + pos + 1, array->values + pos,
      (array->count - pos) * sizeof(Value));
  array->values[pos] = value;
  array->count++;
}

Value removeValueArray(ValueArray* array, int pos) {
  assert(pos >= 0 && pos < array->count); // GCOV_EXCL_LINE
  Value value = array->values[pos];
  memmove(array->values + pos, array->values + pos + 1,
      (array->count - pos - 1) * sizeof(Value));
  array->count--;
  return value;
}

int findInValueArray(ValueArray* array, Value value) {
  for (int i = 0; i < array->count; ++i) {
    if (valuesEqual(value, array->values[i])) {
      return i;
    }
  }
  return -1;
}

void freeValueArray(GC* gc, ValueArray* array) {
  FREE_ARRAY(gc, Value, array->values, array->capacity);
  initValueArray(array);
}

void printValue(FILE* fout, Value value) {
#if NAN_BOXING == 1
  if (IS_BOOL(value)) {
    fprintf(fout, "%s", AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    fprintf(fout, "nil");
  } else if (IS_NUMBER(value)) {
    fprintf(fout, "%g", AS_NUMBER(value));
  } else if (IS_OBJ(value)) {
    printObject(fout, value);
  }
#else
  switch (value.type) {
    case VAL_BOOL:
      fprintf(fout, AS_BOOL(value) ? "true" : "false");
      break;
    case VAL_NIL: fprintf(fout, "nil"); break;
    case VAL_NUMBER: fprintf(fout, "%g", AS_NUMBER(value)); break;
    case VAL_OBJ: printObject(fout, value); break;
  }
#endif
}

void printValueShallow(FILE* fout, Value value) {
  if (IS_LIST(value)) {
    fprintf(fout, "<list %u>", AS_LIST(value)->elements.count);
  } else {
    printValue(fout, value);
  }
}

bool valuesEqual(Value a, Value b) {
#if NAN_BOXING == 1
  if (IS_NUMBER(a) && IS_NUMBER(b)) {
    return AS_NUMBER(a) == AS_NUMBER(b);
  }
  return a == b;
#else
  if (a.type != b.type) {
    return false;
  }
  switch (a.type) {
    case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL: return true;
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(b);
    default: return false; // GCOV_EXCL_LINE: Unreachable.
  }
#endif
}
