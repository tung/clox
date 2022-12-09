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

UTEST_F(ValueArray, InsertRemove) {
  writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(3.0));
  insertValueArray(&ufx->gc, &ufx->va, 0, NUMBER_VAL(2.0));
  insertValueArray(&ufx->gc, &ufx->va, 0, NUMBER_VAL(1.0));
  insertValueArray(&ufx->gc, &ufx->va, 0, NUMBER_VAL(0.0));

  EXPECT_EQ(4, ufx->va.count);
  EXPECT_VALEQ(NUMBER_VAL(0.0), ufx->va.values[0]);
  EXPECT_VALEQ(NUMBER_VAL(1.0), ufx->va.values[1]);
  EXPECT_VALEQ(NUMBER_VAL(2.0), ufx->va.values[2]);
  EXPECT_VALEQ(NUMBER_VAL(3.0), ufx->va.values[3]);

  Value v;
  v = removeValueArray(&ufx->va, 0);
  EXPECT_VALEQ(NUMBER_VAL(0.0), v);
  v = removeValueArray(&ufx->va, 0);
  EXPECT_VALEQ(NUMBER_VAL(1.0), v);
  v = removeValueArray(&ufx->va, 0);
  EXPECT_VALEQ(NUMBER_VAL(2.0), v);
  v = removeValueArray(&ufx->va, 0);
  EXPECT_VALEQ(NUMBER_VAL(3.0), v);

  EXPECT_EQ(0, ufx->va.count);
}

UTEST_F(ValueArray, FindInValueArray) {
  writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(1.0));
  writeValueArray(&ufx->gc, &ufx->va, NUMBER_VAL(2.0));

  EXPECT_EQ(-1, findInValueArray(&ufx->va, NUMBER_VAL(0.0)));
  EXPECT_EQ(0, findInValueArray(&ufx->va, NUMBER_VAL(1.0)));
  EXPECT_EQ(1, findInValueArray(&ufx->va, NUMBER_VAL(2.0)));
  EXPECT_EQ(-1, findInValueArray(&ufx->va, NUMBER_VAL(3.0)));
  EXPECT_EQ(-1, findInValueArray(&ufx->va, NIL_VAL));
  EXPECT_EQ(-1, findInValueArray(&ufx->va, BOOL_VAL(false)));
  EXPECT_EQ(-1, findInValueArray(&ufx->va, BOOL_VAL(true)));
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

UTEST(Value, PrintClosure) {
  GC gc;
  MemBuf out;
  initGC(&gc);
  initMemBuf(&out);

  ObjFunction* f = newFunction(&gc);
  pushTemp(&gc, OBJ_VAL(f));
  ObjClosure* c = newClosure(&gc, f);
  pushTemp(&gc, OBJ_VAL(c));

  printValue(out.fptr, OBJ_VAL(c));
  fflush(out.fptr);
  EXPECT_STREQ("<script>", out.buf);

  popTemp(&gc);
  popTemp(&gc);

  freeMemBuf(&out);
  freeGC(&gc);
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

UTEST(Value, PrintList) {
  GC gc;
  Table strings;
  MemBuf out;
  initGC(&gc);
  initTable(&strings, 0.75);
  initMemBuf(&out);

  ObjList* l0 = newList(&gc);
  pushTemp(&gc, OBJ_VAL(l0));

  ObjList* l1 = newList(&gc);
  pushTemp(&gc, OBJ_VAL(l1));
  writeValueArray(&gc, &l1->elements, NUMBER_VAL(1.1));

  ObjString* s = copyString(&gc, &strings, "hi", 2);
  pushTemp(&gc, OBJ_VAL(s));

  ObjList* l2 = newList(&gc);
  pushTemp(&gc, OBJ_VAL(l2));
  writeValueArray(&gc, &l2->elements, NUMBER_VAL(2.2));
  writeValueArray(&gc, &l2->elements, OBJ_VAL(s));
  writeValueArray(&gc, &l2->elements, OBJ_VAL(l0));

  printValue(out.fptr, OBJ_VAL(l0));
  printValue(out.fptr, OBJ_VAL(l1));
  printValue(out.fptr, OBJ_VAL(l2));
  fflush(out.fptr);
  EXPECT_STREQ("[][1.1][2.2, hi, <list 0>]", out.buf);

  popTemp(&gc);
  popTemp(&gc);
  popTemp(&gc);
  popTemp(&gc);

  freeMemBuf(&out);
  freeTable(&gc, &strings);
  freeGC(&gc);
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

  ObjString* empty = copyString(&gc, &strings, "", 0);
  pushTemp(&gc, OBJ_VAL(empty));
  ObjString* foo = copyString(&gc, &strings, "foo", 3);
  pushTemp(&gc, OBJ_VAL(foo));
  ObjString* bar = copyString(&gc, &strings, "bar", 3);
  pushTemp(&gc, OBJ_VAL(bar));
  ObjString* blah = copyString(&gc, &strings, "blah", 4);
  pushTemp(&gc, OBJ_VAL(blah));

  EXPECT_VALEQ(OBJ_VAL(empty), OBJ_VAL(empty));
  EXPECT_VALEQ(OBJ_VAL(foo), OBJ_VAL(foo));
  EXPECT_VALEQ(OBJ_VAL(bar), OBJ_VAL(bar));
  EXPECT_VALEQ(OBJ_VAL(blah), OBJ_VAL(blah));

  popTemp(&gc);
  popTemp(&gc);
  popTemp(&gc);
  popTemp(&gc);

  freeTable(&gc, &strings);
  freeGC(&gc);
}

UTEST(Value, StringsUnequal) {
  GC gc;
  Table strings;
  initGC(&gc);
  initTable(&strings, 0.75);

  ObjString* empty = copyString(&gc, &strings, "", 0);
  pushTemp(&gc, OBJ_VAL(empty));
  ObjString* foo = copyString(&gc, &strings, "foo", 3);
  pushTemp(&gc, OBJ_VAL(foo));
  ObjString* bar = copyString(&gc, &strings, "bar", 3);
  pushTemp(&gc, OBJ_VAL(bar));
  ObjString* blah = copyString(&gc, &strings, "blah", 4);
  pushTemp(&gc, OBJ_VAL(blah));

  EXPECT_VALNE(OBJ_VAL(empty), OBJ_VAL(foo));
  EXPECT_VALNE(OBJ_VAL(foo), OBJ_VAL(empty));
  EXPECT_VALNE(OBJ_VAL(foo), OBJ_VAL(bar));
  EXPECT_VALNE(OBJ_VAL(foo), OBJ_VAL(blah));

  popTemp(&gc);
  popTemp(&gc);
  popTemp(&gc);
  popTemp(&gc);

  freeTable(&gc, &strings);
  freeGC(&gc);
}

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
