#include "value.h"

#include "utest.h"

#include "membuf.h"
#include "memory.h"
#include "object.h"

#define ufx utest_fixture

struct ValueArray {
  ValueArray va;
};

UTEST_F_SETUP(ValueArray) {
  initValueArray(&ufx->va);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(ValueArray) {
  freeValueArray(&ufx->va);
  ASSERT_TRUE(1);
}

UTEST_F(ValueArray, Empty) {
  EXPECT_EQ(0, ufx->va.count);
}

UTEST_F(ValueArray, WriteOne) {
  writeValueArray(&ufx->va, NUMBER_VAL(1.1));
  ASSERT_EQ(1, ufx->va.count);
  EXPECT_VALEQ(NUMBER_VAL(1.1), ufx->va.values[0]);
}

UTEST_F(ValueArray, WriteSome) {
  writeValueArray(&ufx->va, NUMBER_VAL(1.1));
  writeValueArray(&ufx->va, NUMBER_VAL(2.2));
  writeValueArray(&ufx->va, NUMBER_VAL(3.3));
  ASSERT_EQ(3, ufx->va.count);
  EXPECT_VALEQ(NUMBER_VAL(1.1), ufx->va.values[0]);
  EXPECT_VALEQ(NUMBER_VAL(2.2), ufx->va.values[1]);
  EXPECT_VALEQ(NUMBER_VAL(3.3), ufx->va.values[2]);
}

UTEST_F(ValueArray, WriteLots) {
  double data[] = { 1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9 };
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    writeValueArray(&ufx->va, NUMBER_VAL(data[i]));
  }
  ASSERT_EQ((int)ARRAY_SIZE(data), ufx->va.count);
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    EXPECT_VALEQ(NUMBER_VAL(data[i]), ufx->va.values[i]);
  }
}

UTEST_F(ValueArray, WriteBools) {
  writeValueArray(&ufx->va, BOOL_VAL(true));
  writeValueArray(&ufx->va, BOOL_VAL(false));
  ASSERT_EQ(2, ufx->va.count);
  EXPECT_VALEQ(BOOL_VAL(true), ufx->va.values[0]);
  EXPECT_VALEQ(BOOL_VAL(false), ufx->va.values[1]);
}

UTEST_F(ValueArray, WriteNil) {
  writeValueArray(&ufx->va, NIL_VAL);
  ASSERT_EQ(1, ufx->va.count);
  EXPECT_VALEQ(NIL_VAL, ufx->va.values[0]);
}

UTEST(Value, PrintBoolValues) {
  MemBuf out;
  initMemBuf(&out);

  printValue(out.fptr, BOOL_VAL(false));
  printValue(out.fptr, BOOL_VAL(true));
  fflush(out.fptr);
  EXPECT_STREQ("falsetrue", out.buf);

  freeMemBuf(&out);
}

UTEST(Value, PrintNilValue) {
  MemBuf out;
  initMemBuf(&out);

  printValue(out.fptr, NIL_VAL);
  fflush(out.fptr);
  EXPECT_STREQ("nil", out.buf);

  freeMemBuf(&out);
}

UTEST(Value, PrintNumberValue) {
  MemBuf out;
  initMemBuf(&out);

  printValue(out.fptr, NUMBER_VAL(2.5));
  fflush(out.fptr);
  EXPECT_STREQ("2.5", out.buf);

  freeMemBuf(&out);
}

UTEST(Value, BoolsEqual) {
  EXPECT_VALEQ(BOOL_VAL(false), BOOL_VAL(false));
  EXPECT_VALEQ(BOOL_VAL(true), BOOL_VAL(true));
}

UTEST(Value, BoolsUnequal) {
  EXPECT_VALNE(BOOL_VAL(false), BOOL_VAL(true));
  EXPECT_VALNE(BOOL_VAL(true), BOOL_VAL(false));
}

UTEST(Value, NilsEqual) {
  EXPECT_VALEQ(NIL_VAL, NIL_VAL);
}

UTEST(Value, NumbersEqual) {
  EXPECT_VALEQ(NUMBER_VAL(0.0), NUMBER_VAL(0.0));
  EXPECT_VALEQ(NUMBER_VAL(1.0), NUMBER_VAL(1.0));
  EXPECT_VALEQ(NUMBER_VAL(-1.0), NUMBER_VAL(-1.0));
}

UTEST(Value, NumbersUnequal) {
  EXPECT_VALNE(NUMBER_VAL(0.0), NUMBER_VAL(1.0));
  EXPECT_VALNE(NUMBER_VAL(-1.0), NUMBER_VAL(1.0));
}

UTEST(Value, StringsEqual) {
  Obj* objects = NULL;
  Table strings;
  initTable(&strings, 0.75);
  ObjString* empty = copyString(&objects, &strings, "", 0);
  ObjString* foo = copyString(&objects, &strings, "foo", 3);
  ObjString* bar = copyString(&objects, &strings, "bar", 3);
  ObjString* blah = copyString(&objects, &strings, "blah", 4);

  EXPECT_VALEQ(OBJ_VAL(empty), OBJ_VAL(empty));
  EXPECT_VALEQ(OBJ_VAL(foo), OBJ_VAL(foo));
  EXPECT_VALEQ(OBJ_VAL(bar), OBJ_VAL(bar));
  EXPECT_VALEQ(OBJ_VAL(blah), OBJ_VAL(blah));

  freeTable(&strings);
  freeObjects(objects);
}

UTEST(Value, StringsUnequal) {
  Obj* objects = NULL;
  Table strings;
  initTable(&strings, 0.75);
  ObjString* empty = copyString(&objects, &strings, "", 0);
  ObjString* foo = copyString(&objects, &strings, "foo", 3);
  ObjString* bar = copyString(&objects, &strings, "bar", 3);
  ObjString* blah = copyString(&objects, &strings, "blah", 4);

  EXPECT_VALNE(OBJ_VAL(empty), OBJ_VAL(foo));
  EXPECT_VALNE(OBJ_VAL(foo), OBJ_VAL(empty));
  EXPECT_VALNE(OBJ_VAL(foo), OBJ_VAL(bar));
  EXPECT_VALNE(OBJ_VAL(foo), OBJ_VAL(blah));

  freeTable(&strings);
  freeObjects(objects);
}

UTEST_MAIN();
