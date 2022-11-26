#include "value.h"

#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"

void initValueArray(ValueArray* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(GC* gc, ValueArray* array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values = GROW_ARRAY(
        gc, Value, array->values, oldCapacity, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
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

uint32_t hashValue(Value v) {
  if (IS_STRING(v)) {
    return AS_STRING(v)->hash;
  } else if (IS_NUMBER(v)) {
    union {
      double d;
      struct {
        uint32_t u1;
        uint32_t u2;
      };
    } pun = { .d = AS_NUMBER(v) };
    return pun.u1 ^ pun.u2;
  } else if (IS_BOOL(v)) {
    return AS_BOOL(v);
  }
  return 0; // Unsupported type.
}

const char* valueType(Value v) {
  if (IS_BOOL(v)) {
    return "bool";
  } else if (IS_NIL(v)) {
    return "nil";
  } else if (IS_NUMBER(v)) {
    return "number";
  } else if (IS_OBJ(v)) {
    return objectType(v);
  }
  return "???"; // GCOV_EXCL_LINE: Unreachable.
}
