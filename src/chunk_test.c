#include "chunk.h"
#include "utest.h"

#define ufx utest_fixture

struct Chunk {
  Chunk chunk;
};

UTEST_F_SETUP(Chunk) {
  initChunk(&ufx->chunk);
  EXPECT_TRUE(1);
}

UTEST_F_TEARDOWN(Chunk) {
  freeChunk(&ufx->chunk);
  EXPECT_TRUE(1);
}

UTEST_F(Chunk, Empty) {
  ASSERT_EQ(0, ufx->chunk.count);
}

UTEST_F(Chunk, Write) {
  for (size_t i = 0; i < 9; ++i) {
    writeChunk(&ufx->chunk, OP_RETURN, 1);
  }
  ASSERT_EQ(9, ufx->chunk.count);
  for (size_t i = 0; i < 9; ++i) {
    EXPECT_EQ(OP_RETURN, ufx->chunk.code[i]);
  }
}

UTEST_F(Chunk, AddConstant) {
  addConstant(&ufx->chunk, 2.0);
  addConstant(&ufx->chunk, 2.0);
  addConstant(&ufx->chunk, 2.0);
  ASSERT_EQ(3, ufx->chunk.constants.count);
  EXPECT_EQ(2.0, ufx->chunk.constants.values[0]);
  EXPECT_EQ(2.0, ufx->chunk.constants.values[1]);
  EXPECT_EQ(2.0, ufx->chunk.constants.values[2]);
}

UTEST_F(Chunk, Lines) {
  writeChunk(&ufx->chunk, OP_RETURN, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 2);
  writeChunk(&ufx->chunk, OP_RETURN, 2);
  writeChunk(&ufx->chunk, OP_RETURN, 3);
  writeChunk(&ufx->chunk, OP_RETURN, 3);
  writeChunk(&ufx->chunk, OP_RETURN, 3);
  writeChunk(&ufx->chunk, OP_RETURN, 4);
  ASSERT_EQ(7, ufx->chunk.count);
  EXPECT_EQ(1, ufx->chunk.lines[0]);
  EXPECT_EQ(2, ufx->chunk.lines[1]);
  EXPECT_EQ(2, ufx->chunk.lines[2]);
  EXPECT_EQ(3, ufx->chunk.lines[3]);
  EXPECT_EQ(3, ufx->chunk.lines[4]);
  EXPECT_EQ(3, ufx->chunk.lines[5]);
  EXPECT_EQ(4, ufx->chunk.lines[6]);
}

UTEST_F(Chunk, OpConstant) {
  writeChunk(&ufx->chunk, OP_CONSTANT, 1);
  Value value = 1.5;
  int constIndex = addConstant(&ufx->chunk, value);
  writeChunk(&ufx->chunk, constIndex, 1);
  ASSERT_EQ(2, ufx->chunk.count);
  EXPECT_EQ(OP_CONSTANT, ufx->chunk.code[0]);
  EXPECT_EQ(constIndex, ufx->chunk.code[1]);
  ASSERT_EQ(1, ufx->chunk.constants.count);
  EXPECT_EQ(value, ufx->chunk.constants.values[0]);
}

UTEST_MAIN();
