#include "debug.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#include "chunk.h"
#include "gc.h"
#include "membuf.h"
#include "memory.h"
#include "object.h"
#include "table.h"

#define ufx utest_fixture

struct DisassembleChunk {
  GC gc;
  Chunk chunk;
  MemBuf err;
};

UTEST_F_SETUP(DisassembleChunk) {
  initGC(&ufx->gc);
  initChunk(&ufx->chunk);
  initMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(DisassembleChunk) {
  freeChunk(&ufx->gc, &ufx->chunk);
  freeGC(&ufx->gc);
  freeMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F(DisassembleChunk, OpConstant) {
  uint8_t constantIndex =
      addConstant(&ufx->gc, &ufx->chunk, NUMBER_VAL(1.0));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->gc, &ufx->chunk, constantIndex, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_CONSTANT         0 '1'\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpGetLocal) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_GET_LOCAL, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 0, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_GET_LOCAL        0\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpSetLocal) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_SET_LOCAL, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 0, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_SET_LOCAL        0\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpGetGlobal) {
  Table strings;
  initTable(&strings, 0.75);

  ObjString* globalOStr = copyString(&ufx->gc, &strings, "foo", 3);
  pushTemp(&ufx->gc, OBJ_VAL(globalOStr));

  uint8_t global =
      addConstant(&ufx->gc, &ufx->chunk, OBJ_VAL(globalOStr));
  writeChunk(&ufx->gc, &ufx->chunk, OP_GET_GLOBAL, 123);
  writeChunk(&ufx->gc, &ufx->chunk, global, 123);

  popTemp(&ufx->gc);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_GET_GLOBAL       0 'foo'\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&ufx->gc, &strings);
}

UTEST_F(DisassembleChunk, OpDefineGlobal) {
  Table strings;
  initTable(&strings, 0.75);

  ObjString* globalOStr = copyString(&ufx->gc, &strings, "foo", 3);
  pushTemp(&ufx->gc, OBJ_VAL(globalOStr));

  uint8_t global =
      addConstant(&ufx->gc, &ufx->chunk, OBJ_VAL(globalOStr));
  writeChunk(&ufx->gc, &ufx->chunk, OP_DEFINE_GLOBAL, 123);
  writeChunk(&ufx->gc, &ufx->chunk, global, 123);

  popTemp(&ufx->gc);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_DEFINE_GLOBAL    0 'foo'\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&ufx->gc, &strings);
}

UTEST_F(DisassembleChunk, OpSetGlobal) {
  Table strings;
  initTable(&strings, 0.75);

  ObjString* globalOStr = copyString(&ufx->gc, &strings, "foo", 3);
  pushTemp(&ufx->gc, OBJ_VAL(globalOStr));

  uint8_t global =
      addConstant(&ufx->gc, &ufx->chunk, OBJ_VAL(globalOStr));
  writeChunk(&ufx->gc, &ufx->chunk, OP_SET_GLOBAL, 123);
  writeChunk(&ufx->gc, &ufx->chunk, global, 123);

  popTemp(&ufx->gc);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_SET_GLOBAL       0 'foo'\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&ufx->gc, &strings);
}

UTEST_F(DisassembleChunk, OpGetUpvalue) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_GET_UPVALUE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 2, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_GET_UPVALUE      2\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpSetUpvalue) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_SET_UPVALUE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 2, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_SET_UPVALUE      2\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpJump) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_JUMP, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 1, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 1, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_JUMP             0 -> 260\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpJumpIfFalse) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_JUMP_IF_FALSE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 1, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 1, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_JUMP_IF_FALSE    0 -> 260\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpLoop) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_NIL, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_LOOP, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 0, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 4, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 1);

  fflush(ufx->err.fptr);
  const char msg[] = "0001    | OP_LOOP             1 -> 0\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpCall) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_CALL, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 45, 123);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_CALL            45\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, OpClosure0) {
  Table strings;
  initTable(&strings, 0.75);

  ObjFunction* fun = newFunction(&ufx->gc);
  pushTemp(&ufx->gc, OBJ_VAL(fun));

  uint8_t funIndex = addConstant(&ufx->gc, &ufx->chunk, OBJ_VAL(fun));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CLOSURE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, funIndex, 123);

  popTemp(&ufx->gc);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] = "0000  123 OP_CLOSURE          0 <script>\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&ufx->gc, &strings);
}

UTEST_F(DisassembleChunk, OpClosure2) {
  Table strings;
  initTable(&strings, 0.75);

  ObjFunction* fun = newFunction(&ufx->gc);
  fun->upvalueCount = 2;
  pushTemp(&ufx->gc, OBJ_VAL(fun));

  uint8_t funIndex = addConstant(&ufx->gc, &ufx->chunk, OBJ_VAL(fun));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CLOSURE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, funIndex, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 1, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 1, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 0, 123);
  writeChunk(&ufx->gc, &ufx->chunk, 2, 123);

  popTemp(&ufx->gc);
  disassembleInstruction(ufx->err.fptr, &ufx->chunk, 0);

  fflush(ufx->err.fptr);
  const char msg[] =
      "0000  123 OP_CLOSURE          0 <script>\n"
      "0002      |                     local 1\n"
      "0004      |                     upvalue 2\n";
  EXPECT_STREQ(msg, ufx->err.buf);

  freeTable(&ufx->gc, &strings);
}

UTEST_F(DisassembleChunk, Chapter14Sample1) {
  writeChunk(&ufx->gc, &ufx->chunk, OP_RETURN, 123);
  disassembleChunk(ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->err.fptr);
  const char msg[] =
      "== test chunk ==\n"
      "0000  123 OP_RETURN\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, Chapter14Sample2) {
  int constant = addConstant(&ufx->gc, &ufx->chunk, NUMBER_VAL(1.2));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->gc, &ufx->chunk, constant, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_RETURN, 123);
  disassembleChunk(ufx->err.fptr, &ufx->chunk, "test chunk");

  fflush(ufx->err.fptr);
  const char msg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_RETURN\n";
  EXPECT_STREQ(msg, ufx->err.buf);
}

UTEST_F(DisassembleChunk, Chapter15Sample1) {
  int constant = addConstant(&ufx->gc, &ufx->chunk, NUMBER_VAL(1.2));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->gc, &ufx->chunk, constant, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_NEGATE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_RETURN, 123);
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
  int constant = addConstant(&ufx->gc, &ufx->chunk, NUMBER_VAL(1.2));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->gc, &ufx->chunk, constant, 123);
  constant = addConstant(&ufx->gc, &ufx->chunk, NUMBER_VAL(3.4));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->gc, &ufx->chunk, constant, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_ADD, 123);
  constant = addConstant(&ufx->gc, &ufx->chunk, NUMBER_VAL(5.6));
  writeChunk(&ufx->gc, &ufx->chunk, OP_CONSTANT, 123);
  writeChunk(&ufx->gc, &ufx->chunk, constant, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_DIVIDE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_NEGATE, 123);
  writeChunk(&ufx->gc, &ufx->chunk, OP_RETURN, 123);
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

  GC gc;
  Chunk chunk;
  MemBuf err;
  initGC(&gc);
  initChunk(&chunk);
  initMemBuf(&err);

  writeChunk(&gc, &chunk, expected->op, 123);
  disassembleInstruction(err.fptr, &chunk, 0);

  fflush(err.fptr);
  EXPECT_STREQ(expected->str, err.buf);

  freeChunk(&gc, &chunk);
  freeGC(&gc);
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
  SIMPLE_OP(OP_CLOSE_UPVALUE),
  SIMPLE_OP(OP_RETURN),
  { 255, "0000  123 Unknown opcode 255\n" }
};
// clang-format on

#define NUM_SIMPLE_OPS 17

UTEST_I(DisassembleSimple, SimpleOps, NUM_SIMPLE_OPS) {
  static_assert(
      sizeof(simpleOps) / sizeof(simpleOps[0]) == NUM_SIMPLE_OPS,
      "SimpleOps");
  ufx->cases = simpleOps;
  ASSERT_TRUE(1);
}

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
