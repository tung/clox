#include "debug.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#include "chunk.h"
#include "membuf.h"

#define ufx utest_fixture

struct DisassembleChunk {
  Chunk chunk;
  MemBuf out;
  MemBuf err;
};

UTEST_F_SETUP(DisassembleChunk) {
  initChunk(&ufx->chunk);
  initMemBuf(&ufx->out);
  initMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(DisassembleChunk) {
  freeChunk(&ufx->chunk);
  freeMemBuf(&ufx->out);
  freeMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F(DisassembleChunk, OpConstant) {
  uint8_t constantIndex = addConstant(&ufx->chunk, NUMBER_VAL(1.0));
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
  int constant = addConstant(&ufx->chunk, NUMBER_VAL(1.2));
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
  int constant = addConstant(&ufx->chunk, NUMBER_VAL(1.2));
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
  int constant = addConstant(&ufx->chunk, NUMBER_VAL(1.2));
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  constant = addConstant(&ufx->chunk, NUMBER_VAL(3.4));
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  writeChunk(&ufx->chunk, OP_ADD, 123);
  constant = addConstant(&ufx->chunk, NUMBER_VAL(5.6));
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

typedef struct {
  OpCode op;
  const char* str;
} OpCodeToString;

struct DisassembleSimple {
  OpCodeToString* cases;
};

UTEST_I_SETUP(DisassembleSimple) {
  (void)utest_fixture;
  (void)utest_index;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(DisassembleSimple) {
  OpCodeToString* expected = &ufx->cases[utest_index];

  Chunk chunk;
  MemBuf out, err;
  initChunk(&chunk);
  initMemBuf(&out);
  initMemBuf(&err);

  writeChunk(&chunk, expected->op, 123);
  disassembleInstruction(out.fptr, err.fptr, &chunk, 0);

  fflush(out.fptr);
  fflush(err.fptr);
  EXPECT_STREQ(expected->str, out.buf);
  EXPECT_STREQ("", err.buf);

  freeChunk(&chunk);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

#define SIMPLE_OP(o) \
  { o, "0000  123 " #o "\n" }

OpCodeToString simpleOps[] = {
  SIMPLE_OP(OP_NIL),
  SIMPLE_OP(OP_TRUE),
  SIMPLE_OP(OP_FALSE),
  SIMPLE_OP(OP_EQUAL),
  SIMPLE_OP(OP_GREATER),
  SIMPLE_OP(OP_LESS),
  SIMPLE_OP(OP_ADD),
  SIMPLE_OP(OP_SUBTRACT),
  SIMPLE_OP(OP_MULTIPLY),
  SIMPLE_OP(OP_DIVIDE),
  SIMPLE_OP(OP_NOT),
  SIMPLE_OP(OP_NEGATE),
  SIMPLE_OP(OP_RETURN),
};

#define NUM_SIMPLE_OPS 13

UTEST_I(DisassembleSimple, SimpleOps, NUM_SIMPLE_OPS) {
  static_assert(
      sizeof(simpleOps) / sizeof(simpleOps[0]) == NUM_SIMPLE_OPS,
      "SimpleOps");
  ufx->cases = simpleOps;
  ASSERT_TRUE(1);
}

UTEST_MAIN();
