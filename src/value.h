#pragma once
#ifndef clox_value_h
#define clox_value_h

#include <stdio.h>

#include "common.h"

typedef struct GC GC;
typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ,
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
} Value;

// clang-format off
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)

#define BOOL_VAL(value)   (Value)BOOL_LIT(value)
#define NIL_VAL           (Value)NIL_LIT
#define NUMBER_VAL(value) (Value)NUMBER_LIT(value)
#define OBJ_VAL(object)   (Value){ VAL_OBJ, { .obj = (Obj*)object } }

#define BOOL_LIT(lit)     { VAL_BOOL, { .boolean = lit } }
#define NIL_LIT           { VAL_NIL, { .number = 0 } }
#define NUMBER_LIT(lit)   { VAL_NUMBER, { .number = lit } }
// clang-format on

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(GC* gc, ValueArray* array, Value value);
void freeValueArray(GC* gc, ValueArray* array);
void printValue(FILE* fout, Value value);
bool valuesEqual(Value a, Value b);

#define EXPECT_VALEQ(x, y) \
  UTEST_SURPRESS_WARNING_BEGIN do { \
    if (!valuesEqual(x, y)) { \
      UTEST_PRINTF("%s:%u: Failure\n", __FILE__, __LINE__); \
      UTEST_PRINTF("  Expected : "); \
      if (utest_state.output) { \
        printValue(utest_state.output, x); \
      } \
      printValue(stdout, x); \
      UTEST_PRINTF("\n"); \
      UTEST_PRINTF("    Actual : "); \
      if (utest_state.output) { \
        printValue(utest_state.output, y); \
      } \
      printValue(stdout, y); \
      UTEST_PRINTF("\n"); \
      *utest_result = UTEST_TEST_FAILURE; \
    } \
  } \
  while (0) \
  UTEST_SURPRESS_WARNING_END

#define EXPECT_VALNE(x, y) \
  UTEST_SURPRESS_WARNING_BEGIN do { \
    if (valuesEqual(x, y)) { \
      UTEST_PRINTF("%s:%u: Failure\n", __FILE__, __LINE__); \
      UTEST_PRINTF("  Expected : "); \
      if (utest_state.output) { \
        printValue(utest_state.output, x); \
      } \
      printValue(stdout, x); \
      UTEST_PRINTF("\n"); \
      UTEST_PRINTF("    Actual : "); \
      if (utest_state.output) { \
        printValue(utest_state.output, y); \
      } \
      printValue(stdout, y); \
      UTEST_PRINTF("\n"); \
      *utest_result = UTEST_TEST_FAILURE; \
    } \
  } \
  while (0) \
  UTEST_SURPRESS_WARNING_END

#endif
