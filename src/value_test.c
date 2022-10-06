#include "acutest.h"

#include "value.h"

void testEmpty(void) {
  ValueArray va;
  initValueArray(&va);
  TEST_CHECK(va.count == 0);
  freeValueArray(&va);
}

void testWriteOne(void) {
  ValueArray va;
  initValueArray(&va);
  writeValueArray(&va, 1.1);
  TEST_CHECK(va.count == 1);
  TEST_CHECK(va.values[0] == 1.1);
  freeValueArray(&va);
}

void testWriteSome(void) {
  ValueArray va;
  initValueArray(&va);
  writeValueArray(&va, 1.1);
  writeValueArray(&va, 2.2);
  writeValueArray(&va, 3.3);
  TEST_CHECK(va.count == 3);
  TEST_CHECK(va.values[0] == 1.1);
  TEST_CHECK(va.values[1] == 2.2);
  TEST_CHECK(va.values[2] == 3.3);
  freeValueArray(&va);
}

void testWriteLots(void) {
  Value data[] = { 1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9 };
  ValueArray va;
  initValueArray(&va);
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    writeValueArray(&va, data[i]);
  }
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    TEST_CASE_("i = %ld (%g)", i, data[i]);
    TEST_CHECK(va.values[i] == data[i]);
  }
  freeValueArray(&va);
}

TEST_LIST = {
  { "Empty", testEmpty },
  { "WriteOne", testWriteOne },
  { "WriteSome", testWriteSome },
  { "WriteLots", testWriteLots },
  { NULL, NULL }
};
