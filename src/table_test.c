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
  Value foo = copyString(&ufx->gc, &ufx->strings, "foo", 3);
  pushTemp(&ufx->gc, foo);
  Value bar = copyString(&ufx->gc, &ufx->strings, "bar", 3);
  pushTemp(&ufx->gc, bar);
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

  Value foo = copyString(&ufx->gc, &ufx->strings, "foo", 3);
  pushTemp(&ufx->gc, foo);
  Value bar = copyString(&ufx->gc, &ufx->strings, "bar", 3);
  pushTemp(&ufx->gc, bar);
  Value baz = copyString(&ufx->gc, &ufx->strings, "baz", 3);
  pushTemp(&ufx->gc, baz);

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
  Value vStrs[ARRAY_SIZE(strs)];

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    vStrs[i] = copyString(&ufx->gc, &ufx->strings, strs[i], 1);
    pushTemp(&ufx->gc, vStrs[i]);
    EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, vStrs[i], NUMBER_VAL(i)));
  }

  EXPECT_EQ(9, ufx->t.count);

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    Value v;
    EXPECT_TRUE(tableGet(&ufx->t, vStrs[i], &v));
    popTemp(&ufx->gc);
    EXPECT_VALEQ(NUMBER_VAL(i), v);
  }
}

UTEST_F(Table, SetGetCollisions) {
  // Assume eight entries will fill a table with maxLoad = 1.0.
  ufx->t.maxLoad = 1.0;
  // All of these should return 0 when sent to hashString().
  const char* strs[8] = {
    "  5,b`>",
    "! 6.]~C",
    "\"!s&uL ",
    "# !dKJV",
    "  Fq*G{",
    "! lxuz/",
    "# $}W=}",
    "  Y[N{>",
  };
  Value vStrs[ARRAY_SIZE(strs)];

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    vStrs[i] =
        copyString(&ufx->gc, &ufx->strings, strs[i], strlen(strs[i]));
    EXPECT_EQ((uint32_t)0, hashValue(vStrs[i]));
    pushTemp(&ufx->gc, vStrs[i]);
    EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, vStrs[i], NUMBER_VAL(i)));
  }

  EXPECT_EQ((int)ARRAY_SIZE(strs), ufx->t.count);

  for (size_t i = 0; i < ARRAY_SIZE(strs); ++i) {
    Value v;
    EXPECT_TRUE(tableGet(&ufx->t, vStrs[i], &v));
    popTemp(&ufx->gc);
    EXPECT_VALEQ(NUMBER_VAL(i), v);
  }
}

UTEST_F(Table, FullTableGetMissing) {
  // Assume eight entries will fill a table with maxLoad = 1.0.
  ufx->t.maxLoad = 1.0;
  const char* strs[8] = { "a", "b", "c", "d", "e", "f", "g", "h" };

  Value a = copyString(&ufx->gc, &ufx->strings, strs[0], 1);
  pushTemp(&ufx->gc, a);
  Value b = copyString(&ufx->gc, &ufx->strings, strs[1], 1);
  pushTemp(&ufx->gc, b);
  EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, a, NUMBER_VAL(0.0)));
  EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, b, NUMBER_VAL(1.0)));
  for (size_t i = 2; i < ARRAY_SIZE(strs); ++i) {
    Value vStr = copyString(&ufx->gc, &ufx->strings, strs[i], 1);
    pushTemp(&ufx->gc, vStr);
    EXPECT_TRUE(tableSet(&ufx->gc, &ufx->t, vStr, NUMBER_VAL(i)));
  }

  // Check that the table is indeed full.
  EXPECT_EQ(ufx->t.capacity, ufx->t.count);

  const char missingStr[] = "z";
  Value missingVStr =
      copyString(&ufx->gc, &ufx->strings, missingStr, 1);
  pushTemp(&ufx->gc, missingVStr);
  Value missingValue;

  EXPECT_FALSE(tableGet(&ufx->t, missingVStr, &missingValue));

  // Delete a couple of entries and do it again.
  EXPECT_TRUE(tableDelete(&ufx->t, a));
  EXPECT_TRUE(tableDelete(&ufx->t, b));

  // Recheck that the table is full.
  EXPECT_EQ(ufx->t.capacity, ufx->t.count);

  EXPECT_FALSE(tableGet(&ufx->t, missingVStr, &missingValue));

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
