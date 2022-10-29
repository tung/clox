#include "value.h"

#include <stdio.h>

#include "memory.h"

void initValueArray(ValueArray* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values =
        GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray* array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

void printValue(FILE* fout, Value value) {
  switch (value.type) {
    case VAL_BOOL:
      fprintf(fout, AS_BOOL(value) ? "true" : "false");
      break;
    case VAL_NIL: fprintf(fout, "nil"); break;
    case VAL_NUMBER: fprintf(fout, "%g", AS_NUMBER(value)); break;
  }
}

bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) {
    return false;
  }
  switch (a.type) {
    case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL: return true;
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    default: return false; // GCOV_EXCL_LINE: Unreachable.
  }
}
