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
  TEST_CHECK(chunk.lines[0] == 1);
  TEST_CHECK(chunk.lines[1] == 2);
  TEST_CHECK(chunk.lines[2] == 2);
  TEST_CHECK(chunk.lines[3] == 3);
  TEST_CHECK(chunk.lines[4] == 3);
  TEST_CHECK(chunk.lines[5] == 3);
  TEST_CHECK(chunk.lines[6] == 4);
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

void testOpReturn(void) {
  Chunk chunk;
  initChunk(&chunk);
  writeChunk(&chunk, OP_RETURN, 1);
  TEST_CHECK(chunk.count == 1);
  TEST_CHECK(chunk.code[0] == OP_RETURN);
  freeChunk(&chunk);
}

TEST_LIST = {
  { "Empty", testEmpty },
  { "Write", testWrite },
  { "AddConstant", testAddConstant },
  { "Lines", testLines },
  { "OpConstant", testOpConstant },
  { "OpReturn", testOpReturn },
  { NULL, NULL }
};
