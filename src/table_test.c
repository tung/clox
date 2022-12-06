#include "table.h"

#include "utest.h"

#include "gc.h"
#include "memory.h"

#define ufx utest_fixture

struct Table {
  GC gc;
  Table t;
  Table strings;
};

UTEST_F_SETUP(Table) {
  initGC(&ufx->gc);
  initTable(&ufx->t, 1.0);
  initTable(&ufx->strings, 0.75);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(Table) {
  freeTable(&ufx->gc, &ufx->t);
  freeTable(&ufx->gc, &ufx->strings);
  freeGC(&ufx->gc);
  ASSERT_TRUE(1);
}

UTEST_F(Table, InitFree) {
  EXPECT_EQ(ufx->t.count, 0);
}

UTEST_F(Table, GetSetDelete) {
  ObjString* foo = copyString(&ufx->gc, &ufx->strings, "foo", 3);
  pushTemp(&ufx->gc, OBJ_VAL(foo));
  ObjString* bar = copyString(&ufx->gc, &ufx->strings, "bar", 3);
  pushTemp(&ufx->gc, OBJ_VAL(bar));
  Value fooValue, barValue;

  // No keys in an empty table.
  EXPECT_FALSE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));

  // No keys to delete from an empty table.
  EXPECT_FALSE(tableDelete(&ufx->t, foo));
  EXPECT_FALSE(tableDelete(&ufx->t, bar));

  // Insert foo => 1.0.
  EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, foo, NUMBER_VAL(1.0)));
  EXPECT_TRUE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_VALEQ(NUMBER_VAL(1.0), fooValue);
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));

  // Update foo => 2.0.
  EXPECT_FALSE(tableSet(&ufx->gc, &ufx->t, foo, NUMBER_VAL(2.0)));
  EXPECT_TRUE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_VALEQ(NUMBER_VAL(2.0), fooValue);
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));

  // Delete foo.
  EXPECT_TRUE(tableDelete(&ufx->t, foo));
  EXPECT_FALSE(tableDelete(&ufx->t, bar));
  EXPECT_FALSE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));

  popTemp(&ufx->gc);
  popTemp(&ufx->gc);
}

UTEST_F(Table, AddAll) {
  Table t2;
  initTable(&t2, 1.0);

  ObjString* foo = copyString(&ufx->gc, &ufx->strings, "foo", 3);
  pushTemp(&ufx->gc, OBJ_VAL(foo));
  ObjString* bar = copyString(&ufx->gc, &ufx->strings, "bar", 3);
  pushTemp(&ufx->gc, OBJ_VAL(bar));
  ObjString* baz = copyString(&ufx->gc, &ufx->strings, "baz", 3);
  pushTemp(&ufx->gc, OBJ_VAL(baz));

  EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, foo, NUMBER_VAL(1.0)));
  EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, bar, NUMBER_VAL(2.0)));
  EXPECT_TRUE(tableSet(&ufx->gc, &t2, bar, NUMBER_VAL(3.0)));
  EXPECT_TRUE(tableSet(&ufx->gc, &t2, baz, NUMBER_VAL(4.0)));

  popTemp(&ufx->gc);
  popTemp(&ufx->gc);
  popTemp(&ufx->gc);

  tableAddAll(&ufx->gc, &t2, &ufx->t);

  Value v;
  EXPECT_TRUE(tableGet(&ufx->t, foo, &v));
  EXPECT_VALEQ(NUMBER_VAL(1.0), v);
  EXPECT_TRUE(tableGet(&ufx->t, bar, &v));
  EXPECT_VALEQ(NUMBER_VAL(3.0), v);
  EXPECT_TRUE(tableGet(&ufx->t, baz, &v));
  EXPECT_VALEQ(NUMBER_VAL(4.0), v);

  freeTable(&ufx->gc, &t2);
}

UTEST_F(Table, SetGetLots) {
  const char* strs[] = { "a", "b", "c", "d", "e", "f", "g", "h", "i" };
  ObjString* oStrs[ARRAY_SIZE(strs)];

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    oStrs[i] = copyString(&ufx->gc, &ufx->strings, strs[i], 1);
    pushTemp(&ufx->gc, OBJ_VAL(oStrs[i]));
    EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, oStrs[i], NUMBER_VAL(i)));
  }

  EXPECT_EQ(9, ufx->t.count);

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    Value v;
    EXPECT_TRUE(tableGet(&ufx->t, oStrs[i], &v));
    popTemp(&ufx->gc);
    EXPECT_VALEQ(NUMBER_VAL(i), v);
  }
}

UTEST_F(Table, SetGetCollisions) {
  // Avoid resizing the table.
  ufx->t.maxLoad = 1.0;
  // All of these should return 0 when sent to hashString().
  const char* strs[7] = {
    "!l`V[[",
    "  5,b`>",
    "! 6.]~C",
    "# !dKJV",
    "  Fq*G{",
    "! lxuz/",
    "# $}W=}",
  };
  ObjString* oStrs[ARRAY_SIZE(strs)];

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    oStrs[i] =
        copyString(&ufx->gc, &ufx->strings, strs[i], strlen(strs[i]));
    EXPECT_EQ((uint32_t)0, oStrs[i]->hash);
    pushTemp(&ufx->gc, OBJ_VAL(oStrs[i]));
    EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, oStrs[i], NUMBER_VAL(i)));
  }

  EXPECT_EQ((int)ARRAY_SIZE(strs), ufx->t.count);

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    Value v;
    EXPECT_TRUE(tableGet(&ufx->t, oStrs[i], &v));
    EXPECT_VALEQ(NUMBER_VAL(i), v);
  }

  // Delete second and third strings and find an entry for the second.
  EXPECT_TRUE(tableDelete(&ufx->t, oStrs[1]));
  EXPECT_TRUE(tableDelete(&ufx->t, oStrs[2]));
  EXPECT_TRUE(tableJoinedStringsEntry(
      &ufx->gc, &ufx->t, "", 0, strs[1], strlen(strs[1]), 0));

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    popTemp(&ufx->gc);
  }
}

UTEST_F(Table, FullTableGetMissing) {
  // Assume eight entries will fill a table with maxLoad = 1.0.
  ufx->t.maxLoad = 1.0;
  const char* strs[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };

  ObjString* a = copyString(&ufx->gc, &ufx->strings, strs[0], 1);
  pushTemp(&ufx->gc, OBJ_VAL(a));
  ObjString* b = copyString(&ufx->gc, &ufx->strings, strs[1], 1);
  pushTemp(&ufx->gc, OBJ_VAL(b));
  EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, a, NUMBER_VAL(0.0)));
  EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, b, NUMBER_VAL(1.0)));
  for (size_t i = 2; i < ARRAY_SIZE(strs); ++i) {
    ObjString* oStr = copyString(&ufx->gc, &ufx->strings, strs[i], 1);
    pushTemp(&ufx->gc, OBJ_VAL(oStr));
    EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, oStr, NUMBER_VAL(i)));
  }

  // Check that the table is indeed full.
  EXPECT_EQ(ufx->t.capacity, ufx->t.count);

  const char missingStr[] = "z";
  ObjString* missingOStr =
      copyString(&ufx->gc, &ufx->strings, missingStr, 1);
  pushTemp(&ufx->gc, OBJ_VAL(missingOStr));
  Value missingValue;

  EXPECT_FALSE(tableGet(&ufx->t, missingOStr, &missingValue));

  // Delete a couple of entries and do it again.
  EXPECT_TRUE(tableDelete(&ufx->t, a));
  EXPECT_TRUE(tableDelete(&ufx->t, b));

  // Recheck that the table is full.
  EXPECT_EQ(ufx->t.capacity, ufx->t.count);

  EXPECT_FALSE(tableGet(&ufx->t, missingOStr, &missingValue));

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    popTemp(&ufx->gc);
  }
  popTemp(&ufx->gc);
}

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
