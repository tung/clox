#include "table.h"

#include "utest.h"

#include "memory.h"

#define ufx utest_fixture

struct Table {
  Table t;
  Obj* objects;
  Table strings;
};

UTEST_F_SETUP(Table) {
  initTable(&ufx->t, 1.0);
  ufx->objects = NULL;
  initTable(&ufx->strings, 0.75);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(Table) {
  freeTable(&ufx->t);
  freeTable(&ufx->strings);
  freeObjects(ufx->objects);
  ASSERT_TRUE(1);
}

UTEST_F(Table, InitFree) {
  EXPECT_EQ(ufx->t.count, 0);
}

UTEST_F(Table, GetSetDelete) {
  ObjString* foo = copyString(&ufx->objects, &ufx->strings, "foo", 3);
  ObjString* bar = copyString(&ufx->objects, &ufx->strings, "bar", 3);
  Value fooValue, barValue;

  // No keys in an empty table.
  EXPECT_FALSE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));

  // No keys to delete from an empty table.
  EXPECT_FALSE(tableDelete(&ufx->t, foo));
  EXPECT_FALSE(tableDelete(&ufx->t, bar));

  // Insert foo => 1.0.
  EXPECT_TRUE(tableSet(&ufx->t, foo, NUMBER_VAL(1.0)));
  EXPECT_TRUE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_VALEQ(NUMBER_VAL(1.0), fooValue);
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));

  // Update foo => 2.0.
  EXPECT_FALSE(tableSet(&ufx->t, foo, NUMBER_VAL(2.0)));
  EXPECT_TRUE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_VALEQ(NUMBER_VAL(2.0), fooValue);
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));

  // Delete foo.
  EXPECT_TRUE(tableDelete(&ufx->t, foo));
  EXPECT_FALSE(tableDelete(&ufx->t, bar));
  EXPECT_FALSE(tableGet(&ufx->t, foo, &fooValue));
  EXPECT_FALSE(tableGet(&ufx->t, bar, &barValue));
}

UTEST_F(Table, AddAll) {
  Table t2;
  initTable(&t2, 1.0);

  ObjString* foo = copyString(&ufx->objects, &ufx->strings, "foo", 3);
  ObjString* bar = copyString(&ufx->objects, &ufx->strings, "bar", 3);
  ObjString* baz = copyString(&ufx->objects, &ufx->strings, "baz", 3);

  EXPECT_TRUE(tableSet(&ufx->t, foo, NUMBER_VAL(1.0)));
  EXPECT_TRUE(tableSet(&ufx->t, bar, NUMBER_VAL(2.0)));
  EXPECT_TRUE(tableSet(&t2, bar, NUMBER_VAL(3.0)));
  EXPECT_TRUE(tableSet(&t2, baz, NUMBER_VAL(4.0)));

  tableAddAll(&t2, &ufx->t);

  Value v;
  EXPECT_TRUE(tableGet(&ufx->t, foo, &v));
  EXPECT_VALEQ(NUMBER_VAL(1.0), v);
  EXPECT_TRUE(tableGet(&ufx->t, bar, &v));
  EXPECT_VALEQ(NUMBER_VAL(3.0), v);
  EXPECT_TRUE(tableGet(&ufx->t, baz, &v));
  EXPECT_VALEQ(NUMBER_VAL(4.0), v);

  freeTable(&t2);
}

UTEST_F(Table, SetGetLots) {
  const char* strs[] = { "a", "b", "c", "d", "e", "f", "g", "h", "i" };
  ObjString* oStrs[ARRAY_SIZE(strs)];

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    oStrs[i] = copyString(&ufx->objects, &ufx->strings, strs[i], 1);
    EXPECT_TRUE(tableSet(&ufx->t, oStrs[i], NUMBER_VAL(i)));
  }

  EXPECT_EQ(9, ufx->t.count);

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    Value v;
    EXPECT_TRUE(tableGet(&ufx->t, oStrs[i], &v));
    EXPECT_VALEQ(NUMBER_VAL(i), v);
  }
}

UTEST_F(Table, FullTableGetMissing) {
  // Assume eight entries will fill a table with maxLoad = 1.0.
  ufx->t.maxLoad = 1.0;
  const char* strs[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };

  ObjString* a = copyString(&ufx->objects, &ufx->strings, strs[0], 1);
  ObjString* b = copyString(&ufx->objects, &ufx->strings, strs[1], 1);
  EXPECT_TRUE(tableSet(&ufx->t, a, NUMBER_VAL(0.0)));
  EXPECT_TRUE(tableSet(&ufx->t, b, NUMBER_VAL(1.0)));
  for (size_t i = 2; i < ARRAY_SIZE(strs); ++i) {
    ObjString* oStr =
        copyString(&ufx->objects, &ufx->strings, strs[i], 1);
    EXPECT_TRUE(tableSet(&ufx->t, oStr, NUMBER_VAL(i)));
  }

  // Check that the table is indeed full.
  EXPECT_EQ(ufx->t.capacity, ufx->t.count);

  const char missingStr[] = "z";
  ObjString* missingOStr =
      copyString(&ufx->objects, &ufx->strings, missingStr, 1);
  Value missingValue;

  EXPECT_FALSE(tableGet(&ufx->t, missingOStr, &missingValue));

  // Delete a couple of entries and do it again.
  EXPECT_TRUE(tableDelete(&ufx->t, a));
  EXPECT_TRUE(tableDelete(&ufx->t, b));

  // Recheck that the table is full.
  EXPECT_EQ(ufx->t.capacity, ufx->t.count);

  EXPECT_FALSE(tableGet(&ufx->t, missingOStr, &missingValue));
}

UTEST_MAIN();
