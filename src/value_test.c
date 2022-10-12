#include "value.h"
#include "utest.h"

#define ufx utest_fixture

typedef struct {
  char* buf;
  size_t size;
} MemBuf;

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
  writeValueArray(&ufx->va, 1.1);
  ASSERT_EQ(1, ufx->va.count);
  EXPECT_EQ(1.1, ufx->va.values[0]);
}

UTEST_F(ValueArray, WriteSome) {
  writeValueArray(&ufx->va, 1.1);
  writeValueArray(&ufx->va, 2.2);
  writeValueArray(&ufx->va, 3.3);
  ASSERT_EQ(3, ufx->va.count);
  EXPECT_EQ(1.1, ufx->va.values[0]);
  EXPECT_EQ(2.2, ufx->va.values[1]);
  EXPECT_EQ(3.3, ufx->va.values[2]);
}

UTEST_F(ValueArray, WriteLots) {
  Value data[] = { 1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9 };
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    writeValueArray(&ufx->va, data[i]);
  }
  ASSERT_EQ((int)ARRAY_SIZE(data), ufx->va.count);
  for (size_t i = 0; i < ARRAY_SIZE(data); ++i) {
    EXPECT_EQ(data[i], ufx->va.values[i]);
  }
}

UTEST(Value, PrintValue) {
  MemBuf out;
  FILE* fout = open_memstream(&out.buf, &out.size);
  printValue(fout, (Value)2.5);
  fclose(fout);
  ASSERT_GT(out.size, 0ul);
  EXPECT_STREQ("2.5", out.buf);
  free(out.buf);
}

UTEST_MAIN();
