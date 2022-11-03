#include "debug.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#include "chunk.h"
#include "membuf.h"
#include "memory.h"
#include "object.h"
#include "table.h"

#define ufx utest_fixture

struct DisassembleChunk {
  Chunk chunk;
  MemBuf err;
};

UTEST_F_SETUP(DisassembleChunk) {
  initChunk(&ufx->chunk);
  initMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(DisassembleChunk) {
  freeChunk(&ufx->chunk);
  freeMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F(DisassembleChunk, OpConstant) {
  uint8_t constantIndex = addConstant(&ufx->chunk, NUMBER_VAL(1.0));
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constantIndex, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_CONSTANT         0 '1'\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpGetGlobal) {
  Obj* objects = NULL;
  Table strings;
  initTable(&strings, 0.75);

  ObjString* globalOStr = copyString(&objects, &strings, "foo", 3);

  uint8_t global = addConstant(&ufx->chunk, OBJ_VAL(globalOStr));
  writeChunk(&ufx->chunk, OP_GET_GLOBAL, 123);
  writeChunk(&ufx->chunk, global, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_GET_GLOBAL       0 'foo'\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&strings);
  freeObjects(objects);
}

UTEST_F(DisassembleChunk, OpDefineGlobal) {
  Obj* objects = NULL;
  Table strings;
  initTable(&strings, 0.75);

  ObjString* globalOStr = copyString(&objects, &strings, "foo", 3);

  uint8_t global = addConstant(&ufx->chunk, OBJ_VAL(globalOStr));
  writeChunk(&ufx->chunk, OP_DEFINE_GLOBAL, 123);
  writeChunk(&ufx->chunk, global, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_DEFINE_GLOBAL    0 'foo'\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&strings);
  freeObjects(objects);
}

UTEST_F(DisassembleChunk, OpSetGlobal) {
  Obj* objects = NULL;
  Table strings;
  initTable(&strings, 0.75);

  ObjString* globalOStr = copyString(&objects, &strings, "foo", 3);

  uint8_t global = addConstant(&ufx->chunk, OBJ_VAL(globalOStr));
  writeChunk(&ufx->chunk, OP_SET_GLOBAL, 123);
  writeChunk(&ufx->chunk, global, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_SET_GLOBAL       0 'foo'\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&strings);
  freeObjects(objects);
}

UTEST_F(DisassembleChunk, Chapter14Sample1) {
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->err.fptr);
  const char msg[] =
      "== test chunk ==\n"
      "0000  123 OP_RETURN\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, Chapter14Sample2) {
  int constant = addConstant(&ufx->chunk, NUMBER_VAL(1.2));
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->err.fptr);
  const char msg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_RETURN\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, Chapter15Sample1) {
  int constant = addConstant(&ufx->chunk, NUMBER_VAL(1.2));
  writeChunk(&ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->chunk, constant, 123);
  writeChunk(&ufx->chunk, OP_NEGATE, 123);
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->err.fptr);
  const char msg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_RETURN\n";
  EXPECT_STREQ(msg, ufx->err.buf);
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
  disassembleChunk(ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->err.fptr);
  const char msg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_CONSTANT         1 '3.4'\n"
      "0004    | OP_ADD\n"
      "0005    | OP_CONSTANT         2 '5.6'\n"
      "0007    | OP_DIVIDE\n"
      "0008    | OP_NEGATE\n"
      "0009    | OP_RETURN\n";
  EXPECT_STREQ(msg, ufx->err.buf);
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
  MemBuf err;
  initChunk(&chunk);
  initMemBuf(&err);

  writeChunk(&chunk, expected->op, 123);
  disassembleInstruction(err.fptr, &chunk, 0);

  fflush(err.fptr);
  EXPECT_STREQ(expected->str, err.buf);

  freeChunk(&chunk);
  freeMemBuf(&err);
}

#define SIMPLE_OP(o) \
  { o, "0000  123 " #o "\n" }

// clang-format off
OpCodeToString simpleOps[] = {
  SIMPLE_OP(OP_NIL),
  SIMPLE_OP(OP_TRUE),
  SIMPLE_OP(OP_FALSE),
  SIMPLE_OP(OP_POP),
  SIMPLE_OP(OP_EQUAL),
  SIMPLE_OP(OP_GREATER),
  SIMPLE_OP(OP_LESS),
  SIMPLE_OP(OP_ADD),
  SIMPLE_OP(OP_SUBTRACT),
  SIMPLE_OP(OP_MULTIPLY),
  SIMPLE_OP(OP_DIVIDE),
  SIMPLE_OP(OP_NOT),
  SIMPLE_OP(OP_NEGATE),
  SIMPLE_OP(OP_PRINT),
  SIMPLE_OP(OP_RETURN),
  { 255, "0000  123 Unknown opcode 255\n" }
};
// clang-format on

#define NUM_SIMPLE_OPS 16

UTEST_I(DisassembleSimple, SimpleOps, NUM_SIMPLE_OPS) {
  static_assert(
      sizeof(simpleOps) / sizeof(simpleOps[0]) == NUM_SIMPLE_OPS,
      "SimpleOps");
  ufx->cases = simpleOps;
  ASSERT_TRUE(1);
}

UTEST_MAIN();
