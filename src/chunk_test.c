#include "acutest.h"

#include "chunk.h"

void testEmpty(void) {
  Chunk chunk;
  initChunk(&chunk);
  TEST_CHECK(chunk.count == 0);
  freeChunk(&chunk);
}

void testWrite(void) {
  Chunk chunk;
  initChunk(&chunk);
  for (size_t i = 0; i < 9; ++i) {
    writeChunk(&chunk, OP_RETURN, 1);
  }
  TEST_CHECK(chunk.count == 9);
  for (size_t i = 0; i < 9; ++i) {
    TEST_CASE_("i = %ld", i);
    TEST_CHECK(chunk.code[i] == OP_RETURN);
  }
  freeChunk(&chunk);
}

void testWriteConstant(void) {
  Chunk chunk;
  initChunk(&chunk);

  const int NUM = 258;
  for (int i = 0; i < NUM; ++i) {
    writeConstant(&chunk, (double)i, i + 1);
  }

  int shortConst = NUM < 256 ? NUM : 256;
  int longConst = NUM < 256 ? 0 : NUM - 256;
  TEST_CHECK(chunk.count == shortConst * 2 + longConst * 4);

  for (int i = 0; i < shortConst; ++i) {
    TEST_CASE_("OP_CONSTANT code i = %d", i);
    TEST_CHECK(chunk.code[i * 2] == OP_CONSTANT);
    TEST_CHECK(chunk.code[i * 2 + 1] == i);
  }

  for (int i = 0; i < longConst; ++i) {
    int baseIndex = 256 * 2 + i * 4;
    TEST_CASE_("OP_CONSTANT_LONG code i = %d", i + 256);
    TEST_CHECK(chunk.code[baseIndex] == OP_CONSTANT_LONG);
    int constIndex = (chunk.code[baseIndex + 1] << 16) +
        (chunk.code[baseIndex + 2] << 8) +
        chunk.code[baseIndex + 3];
    TEST_CHECK(constIndex == i + 256);
  }

  TEST_CHECK(chunk.constants.count == NUM);
  for (int i = 0; i < NUM; ++i) {
    TEST_CASE_("constant i = %d", i);
    TEST_CHECK(chunk.constants.values[i] == (double)i);
  }

  freeChunk(&chunk);
}

void testAddConstant(void) {
  Chunk chunk;
  initChunk(&chunk);
  addConstant(&chunk, 2.0);
  addConstant(&chunk, 2.0);
  addConstant(&chunk, 2.0);
  TEST_CHECK(chunk.constants.count == 3);
  TEST_CHECK(chunk.constants.values[0] == 2.0);
  TEST_CHECK(chunk.constants.values[1] == 2.0);
  TEST_CHECK(chunk.constants.values[2] == 2.0);
  freeChunk(&chunk);
}

void testLines(void) {
  Chunk chunk;
  initChunk(&chunk);
  writeChunk(&chunk, OP_RETURN, 1);
  writeChunk(&chunk, OP_RETURN, 2);
  writeChunk(&chunk, OP_RETURN, 2);
  writeChunk(&chunk, OP_RETURN, 3);
  writeChunk(&chunk, OP_RETURN, 3);
  writeChunk(&chunk, OP_RETURN, 3);
  writeChunk(&chunk, OP_RETURN, 4);
  TEST_CHECK(chunk.count == 7);
  TEST_CHECK(getLine(&chunk, 0) == 1);
  TEST_CHECK(getLine(&chunk, 1) == 2);
  TEST_CHECK(getLine(&chunk, 2) == 2);
  TEST_CHECK(getLine(&chunk, 3) == 3);
  TEST_CHECK(getLine(&chunk, 4) == 3);
  TEST_CHECK(getLine(&chunk, 5) == 3);
  TEST_CHECK(getLine(&chunk, 6) == 4);
  TEST_CHECK(chunk.lineCount == 4);
  freeChunk(&chunk);
}

void testOpReturn(void) {
  Chunk chunk;
  initChunk(&chunk);
  writeChunk(&chunk, OP_RETURN, 1);
  TEST_CHECK(chunk.count == 1);
  TEST_CHECK(chunk.code[0] == OP_RETURN);
  freeChunk(&chunk);
}

void testOpConstant(void) {
  Chunk chunk;
  initChunk(&chunk);
  writeChunk(&chunk, OP_CONSTANT, 1);
  Value value = 1.5;
  int constIndex = addConstant(&chunk, value);
  writeChunk(&chunk, constIndex, 1);
  TEST_CHECK(chunk.count == 2);
  TEST_CHECK(chunk.code[0] == OP_CONSTANT);
  TEST_CHECK(chunk.code[1] == constIndex);
  TEST_CHECK(chunk.constants.count == 1);
  TEST_CHECK(chunk.constants.values[0] == value);
  freeChunk(&chunk);
}

TEST_LIST = {
  { "Empty", testEmpty },
  { "Write", testWrite },
  { "WriteConstant", testWriteConstant },
  { "AddConstant", testAddConstant },
  { "Lines", testLines },
  { "OpReturn", testOpReturn },
  { "OpConstant", testOpConstant },
  { NULL, NULL }
};
