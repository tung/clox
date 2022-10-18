#include "debug.h"

#include <stdio.h>

#include "utest.h"

#include "chunk.h"

#define ufx utest_fixture

typedef struct {
  char* buf;
  size_t size;
  FILE* fptr;
} MemBuf;

struct DisassembleChunk {
  Chunk chunk;
  MemBuf out;
  MemBuf err;
};

UTEST_F_SETUP(DisassembleChunk) {
  initChunk(&ufx->chunk);
  ufx->out.fptr = open_memstream(&ufx->out.buf, &ufx->out.size);
  ufx->err.fptr = open_memstream(&ufx->err.buf, &ufx->err.size);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(DisassembleChunk) {
  freeChunk(&ufx->chunk);
  fclose(ufx->out.fptr);
  fclose(ufx->err.fptr);
  free(ufx->out.buf);
  free(ufx->err.buf);
  ASSERT_TRUE(1);
}

UTEST_F(DisassembleChunk, OpConstant) {
  uint8_t constantIndex = addConstant(&ufx->chunk, 1.0);
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constantIndex, 123);
  disassembleInstruction(ufx->out.fptr, ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] = "0000  123 OP_CONSTANT         0 '1'\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, OpAdd) {
  writeChunk(&ufx->chunk, OP_ADD, 123);
  disassembleInstruction(ufx->out.fptr, ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] = "0000  123 OP_ADD\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, OpSubtract) {
  writeChunk(&ufx->chunk, OP_SUBTRACT, 123);
  disassembleInstruction(ufx->out.fptr, ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] = "0000  123 OP_SUBTRACT\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, OpMultiply) {
  writeChunk(&ufx->chunk, OP_MULTIPLY, 123);
  disassembleInstruction(ufx->out.fptr, ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] = "0000  123 OP_MULTIPLY\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, OpDivide) {
  writeChunk(&ufx->chunk, OP_DIVIDE, 123);
  disassembleInstruction(ufx->out.fptr, ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] = "0000  123 OP_DIVIDE\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, OpNegate) {
  writeChunk(&ufx->chunk, OP_NEGATE, 123);
  disassembleInstruction(ufx->out.fptr, ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] = "0000  123 OP_NEGATE\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, UnknownOpCode) {
  writeChunk(&ufx->chunk, 255, 1);
  disassembleChunk(ufx->out.fptr, ufx->err.fptr, &ufx->chunk, "");

  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->err.size);
  EXPECT_STREQ("Unknown opcode 255\n", ufx->err.buf);
}

UTEST_F(DisassembleChunk, Chapter14Sample1) {
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(
      ufx->out.fptr, ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] =
      "== test chunk ==\n"
      "0000  123 OP_RETURN\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, Chapter14Sample2) {
  int constant = addConstant(&ufx->chunk, 1.2);
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(
      ufx->out.fptr, ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_RETURN\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, Chapter15Sample1) {
  int constant = addConstant(&ufx->chunk, 1.2);
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  writeChunk(&ufx->chunk, OP_NEGATE, 123);
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(
      ufx->out.fptr, ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_RETURN\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_F(DisassembleChunk, Chapter15Sample2) {
  int constant = addConstant(&ufx->chunk, 1.2);
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  constant = addConstant(&ufx->chunk, 3.4);
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  writeChunk(&ufx->chunk, OP_ADD, 123);
  constant = addConstant(&ufx->chunk, 5.6);
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  writeChunk(&ufx->chunk, OP_DIVIDE, 123);
  writeChunk(&ufx->chunk, OP_NEGATE, 123);
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(
      ufx->out.fptr, ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->out.fptr);
  fflush(ufx->err.fptr);
  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_CONSTANT         1 '3.4'\n"
      "0004    | OP_ADD\n"
      "0005    | OP_CONSTANT         2 '5.6'\n"
      "0007    | OP_DIVIDE\n"
      "0008    | OP_NEGATE\n"
      "0009    | OP_RETURN\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_MAIN();
