#include "value.h"

#include "utest.h"

#include "gc.h"
#include "membuf.h"
#include "memory.h"
#include "object.h"

#define ufx utest_fixture

struct ValueArray {
  GC gc;
  ValueArray va;
};

UTEST_F_SETUP(ValueArray) {
  initGC(&ufx->gc);
  initValueArray(&ufx->va);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(ValueArray) {
  freeValueArray(&ufx->gc, &ufx->va);
  freeGC(&ufx->gc);
  ASSERT_TRUE(1);
}

UTEST_F(ValueArray, Empty) {
  EXPECT_EQ(0, ufx->va.count);
}

UTEST_F(ValueArray, WriteOne) {
  writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(1.1));
  ASSERT_EQ(1, ufx->va.count);
  EXPECT_VALEQ(NUMBER_VAL(1.1), ufx->va.values[0]);
}

UTEST_F(ValueArray, WriteSome) {
  writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(1.1));
  writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(2.2));
  writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(3.3));
  ASSERT_EQ(3, ufx->va.count);
  EXPECT_VALEQ(NUMBER_VAL(1.1), ufx->va.values[0]);
  EXPECT_VALEQ(NUMBER_VAL(2.2), ufx->va.values[1]);
  EXPECT_VALEQ(NUMBER_VAL(3.3), ufx->va.values[2]);
}

UTEST_F(ValueArray, WriteLots) {
  double data[] = { 1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9 };
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(data[i]));
  }
  ASSERT_EQ((int)ARRAY_SIZE(data), ufx->va.count);
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    EXPECT_VALEQ(NUMBER_VAL(data[i]), ufx->va.values[i]);
  }
}

UTEST_F(ValueArray, WriteBools) {
  writeValueArray(&ufx->gc, &ufx->va, BOOL_VAL(true));
  writeValueArray(&ufx->gc, &ufx->va, BOOL_VAL(false));
  ASSERT_EQ(2, ufx->va.count);
  EXPECT_VALEQ(BOOL_VAL(true), ufx->va.values[0]);
  EXPECT_VALEQ(BOOL_VAL(false), ufx->va.values[1]);
}

UTEST_F(ValueArray, WriteNil) {
  writeValueArray(&ufx->gc, &ufx->va, NIL_VAL);
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

UTEST(Value, PrintUpvalue) {
  MemBuf out;
  initMemBuf(&out);

  Obj o = { .type = OBJ_UPVALUE, .next = NULL };
  printValue(out.fptr, OBJ_VAL(&o));
  fflush(out.fptr);
  EXPECT_STREQ("upvalue", out.buf);

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
  GC gc;
  Table strings;
  initGC(&gc);
  initTable(&strings, 0.75);

  size_t temps = 0;

  Value empty1 = copyString(&gc, &strings, "", 0);
  pushTemp(&gc, empty1);
  temps++;
  Value foo1 = copyString(&gc, &strings, "foo", 3);
  pushTemp(&gc, foo1);
  temps++;
  Value bar1 = copyString(&gc, &strings, "bar", 3);
  pushTemp(&gc, bar1);
  temps++;
  Value blah1 = copyString(&gc, &strings, "blah", 4);
  pushTemp(&gc, blah1);
  temps++;

  Value empty2 = copyString(&gc, &strings, "", 0);
  pushTemp(&gc, empty1);
  temps++;
  Value foo2 = copyString(&gc, &strings, "foo", 3);
  pushTemp(&gc, foo1);
  temps++;
  Value bar2 = copyString(&gc, &strings, "bar", 3);
  pushTemp(&gc, bar1);
  temps++;
  Value blah2 = copyString(&gc, &strings, "blah", 4);
  pushTemp(&gc, blah1);
  temps++;

  EXPECT_VALEQ(empty1, empty2);
  EXPECT_VALEQ(foo1, foo2);
  EXPECT_VALEQ(bar1, bar2);
  EXPECT_VALEQ(blah1, blah2);

  while (temps > 0) {
    popTemp(&gc);
    temps--;
  }

  freeTable(&gc, &strings);
  freeGC(&gc);
}

UTEST(Value, StringsUnequal) {
  GC gc;
  Table strings;
  initGC(&gc);
  initTable(&strings, 0.75);

  Value empty = copyString(&gc, &strings, "", 0);
  Value foo = copyString(&gc, &strings, "foo", 3);
  Value bar = copyString(&gc, &strings, "bar", 3);
  Value blah = copyString(&gc, &strings, "blah", 4);

  EXPECT_VALNE(empty, foo);
  EXPECT_VALNE(foo, empty);
  EXPECT_VALNE(foo, bar);
  EXPECT_VALNE(foo, blah);

  freeTable(&gc, &strings);
  freeGC(&gc);
}

UTEST(Value, Hash) {
  GC gc;
  Table strings;
  initGC(&gc);
  initTable(&strings, 0.75);

  Value fooStr = copyString(&gc, &strings, "foo", 3);
  EXPECT_EQ(2851307223u, hashValue(fooStr));
  EXPECT_EQ(1079951360u, hashValue(NUMBER_VAL(123.0)));
  EXPECT_EQ(0u, hashValue(BOOL_VAL(false)));
  EXPECT_EQ(1u, hashValue(BOOL_VAL(true)));
  EXPECT_EQ(0u, hashValue(NIL_VAL));

  freeTable(&gc, &strings);
  freeGC(&gc);
}

UTEST(Value, Type) {
  EXPECT_STREQ("bool", valueType(BOOL_VAL(false)));
  EXPECT_STREQ("bool", valueType(BOOL_VAL(true)));
  EXPECT_STREQ("nil", valueType(NIL_VAL));
  EXPECT_STREQ("number", valueType(NUMBER_VAL(123.0)));
}

UTEST(Value, ObjectType) {
  GC gc;
  Table strings;
  initGC(&gc);
  initTable(&strings, 0.75);

  size_t temps = 0;

  Value fooStr = copyString(&gc, &strings, "foo", 3);
  pushTemp(&gc, fooStr);
  temps++;

  ObjFunction* fooFun = newFunction(&gc);
  Value fooFunVal = OBJ_VAL(fooFun);
  pushTemp(&gc, fooFunVal);
  temps++;

  ObjClosure* fooClo = newClosure(&gc, fooFun);
  Value fooCloVal = OBJ_VAL(fooClo);
  pushTemp(&gc, fooCloVal);
  temps++;

  ObjBoundMethod* fooBound = newBoundMethod(&gc, fooStr, fooClo);
  Value fooBoundVal = OBJ_VAL(fooBound);
  pushTemp(&gc, fooBoundVal);
  temps++;

  ObjClass* fooClass = newClass(&gc, fooStr);
  Value fooClassVal = OBJ_VAL(fooClass);
  pushTemp(&gc, fooClassVal);
  temps++;

  ObjInstance* fooInst = newInstance(&gc, fooClass);
  Value fooInstVal = OBJ_VAL(fooInst);
  pushTemp(&gc, fooInstVal);
  temps++;

  ObjNative* fooNative = newNative(&gc, NULL);
  Value fooNativeVal = OBJ_VAL(fooNative);
  pushTemp(&gc, fooNativeVal);
  temps++;

  ObjUpvalue* fooUpVal = newUpvalue(&gc, &fooStr);
  Value fooUpValVal = OBJ_VAL(fooUpVal);
  pushTemp(&gc, fooUpValVal);
  temps++;

  EXPECT_STREQ("string", valueType(fooStr));
  EXPECT_STREQ("function", valueType(fooFunVal));
  EXPECT_STREQ("closure", valueType(fooCloVal));
  EXPECT_STREQ("bound method", valueType(fooBoundVal));
  EXPECT_STREQ("class", valueType(fooClassVal));
  EXPECT_STREQ("instance", valueType(fooInstVal));
  EXPECT_STREQ("native", valueType(fooNativeVal));
  EXPECT_STREQ("upvalue", valueType(fooUpValVal));

  while (temps > 0) {
    popTemp(&gc);
    temps--;
  }

  freeTable(&gc, &strings);
  freeGC(&gc);
}

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
