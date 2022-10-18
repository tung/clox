#include "vm.h"

#include <stdio.h>

#include "utest.h"

#include "chunk.h"
#include "value.h"

#define memBufSuffix(mb, constStr) \
  ((mb).buf + (mb).size + 1 - sizeof(constStr))
#define ufx utest_fixture
#define writeConstant(chunk, value, line) \
  do { \
    writeChunk((chunk), OP_CONSTANT, (line)); \
    writeChunk((chunk), addConstant((chunk), (value)), (line)); \
  } while (0)

typedef struct {
  char* buf;
  size_t size;
  FILE* fptr;
} MemBuf;

struct VM {
  VM vm;
  Chunk chunk;
  MemBuf out;
  MemBuf err;
};

UTEST_F_SETUP(VM) {
  initVM(&ufx->vm);
  initChunk(&ufx->chunk);
  ufx->out.fptr = open_memstream(&ufx->out.buf, &ufx->out.size);
  ufx->err.fptr = open_memstream(&ufx->err.buf, &ufx->err.size);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(VM) {
  freeVM(&ufx->vm);
  freeChunk(&ufx->chunk);
  fclose(ufx->out.fptr);
  fclose(ufx->err.fptr);
  free(ufx->out.buf);
  free(ufx->err.buf);
  ASSERT_TRUE(1);
}

UTEST_F(VM, Empty) {
  ASSERT_EQ(0ul, ufx->vm.stackCount);
}

UTEST_F(VM, PushPop) {
  push(&ufx->vm, 1.2);
  push(&ufx->vm, 3.4);
  push(&ufx->vm, 5.6);
  ASSERT_EQ(3ul, ufx->vm.stackCount);
  EXPECT_EQ(5.6, pop(&ufx->vm));
  EXPECT_EQ(3.4, pop(&ufx->vm));
  EXPECT_EQ(1.2, pop(&ufx->vm));
  ASSERT_EQ(0ul, ufx->vm.stackCount);
}

UTEST_F(VM, PushPopLots) {
  const size_t count = 256;
  for (size_t i = 0; i < count; ++i) {
    push(&ufx->vm, 1.0);
  }
  ASSERT_EQ(count, ufx->vm.stackCount);
  for (size_t i = 0; i < count; ++i) {
    EXPECT_EQ(1.0, pop(&ufx->vm));
  }
  ASSERT_EQ(0ul, ufx->vm.stackCount);
}

UTEST_F(VM, OpConstantOpReturn) {
  writeConstant(&ufx->chunk, 2.5, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  const char outMsg[] = "2.5\n";
  fflush(ufx->out.fptr);
  ASSERT_LE(sizeof(outMsg), ufx->out.size + 1);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, OpAdd) {
  writeConstant(&ufx->chunk, 3.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_ADD, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "5\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size + 1);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, OpSubtract) {
  writeConstant(&ufx->chunk, 3.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_SUBTRACT, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "1\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size + 1);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, OpMultiply) {
  writeConstant(&ufx->chunk, 3.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_MULTIPLY, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "6\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size + 1);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, OpDivide) {
  writeConstant(&ufx->chunk, 3.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_DIVIDE, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "1.5\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size + 1);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, OpNegate) {
  writeConstant(&ufx->chunk, 3.0, 1);
  writeChunk(&ufx->chunk, OP_NEGATE, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "-3\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size + 1);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, OpNegateStackUnderflow) {
  writeChunk(&ufx->chunk, OP_NEGATE, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  fflush(ufx->err.fptr);
  const char errMsg[] = "Stack underflow\n";
  ASSERT_LE(sizeof(errMsg), ufx->err.size + 1);
  EXPECT_STREQ(errMsg, memBufSuffix(ufx->err, errMsg));
}

UTEST_F(VM, UnknownOp) {
  writeChunk(&ufx->chunk, 255, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  fflush(ufx->err.fptr);
  const char errMsg[] = "Unknown opcode 255\n";
  ASSERT_LE(sizeof(errMsg), ufx->err.size + 1);
  EXPECT_STREQ(errMsg, memBufSuffix(ufx->err, errMsg));
}

UTEST_F(VM, InterpretEmpty) {
  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  fflush(ufx->err.fptr);
  const char errMsg[] = "missing OP_RETURN\n";
  ASSERT_LE(sizeof(errMsg), ufx->err.size + 1);
  EXPECT_STREQ(errMsg, memBufSuffix(ufx->err, errMsg));
}

UTEST_F(VM, Expression1) {
  // 1 * 2 + 3
  writeConstant(&ufx->chunk, 1.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_MULTIPLY, 1);
  writeConstant(&ufx->chunk, 3.0, 1);
  writeChunk(&ufx->chunk, OP_ADD, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "5\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, Expression2) {
  // 1 + 2 * 3
  writeConstant(&ufx->chunk, 1.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeConstant(&ufx->chunk, 3.0, 1);
  writeChunk(&ufx->chunk, OP_MULTIPLY, 1);
  writeChunk(&ufx->chunk, OP_ADD, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "7\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, Expression3) {
  // 3 - 2 - 1
  writeConstant(&ufx->chunk, 3.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_SUBTRACT, 1);
  writeConstant(&ufx->chunk, 1.0, 1);
  writeChunk(&ufx->chunk, OP_SUBTRACT, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "0\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, Expression4) {
  // 1 + 2 * 3 - 4 / -5
  writeConstant(&ufx->chunk, 1.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeConstant(&ufx->chunk, 3.0, 1);
  writeChunk(&ufx->chunk, OP_MULTIPLY, 1);
  writeChunk(&ufx->chunk, OP_ADD, 1);
  writeConstant(&ufx->chunk, 4.0, 1);
  writeConstant(&ufx->chunk, 5.0, 1);
  writeChunk(&ufx->chunk, OP_NEGATE, 1);
  writeChunk(&ufx->chunk, OP_DIVIDE, 1);
  writeChunk(&ufx->chunk, OP_SUBTRACT, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "7.8\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, ExpressionNoOpNegate) {
  // 4 - 3 * -2
  writeConstant(&ufx->chunk, 4.0, 1);
  writeConstant(&ufx->chunk, 3.0, 1);
  writeConstant(&ufx->chunk, 0.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_SUBTRACT, 1);
  writeChunk(&ufx->chunk, OP_MULTIPLY, 1);
  writeChunk(&ufx->chunk, OP_SUBTRACT, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "10\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_F(VM, ExpressionNoOpSubtract) {
  // 4 - 3 * -2
  writeConstant(&ufx->chunk, 4.0, 1);
  writeConstant(&ufx->chunk, 3.0, 1);
  writeConstant(&ufx->chunk, 2.0, 1);
  writeChunk(&ufx->chunk, OP_NEGATE, 1);
  writeChunk(&ufx->chunk, OP_MULTIPLY, 1);
  writeChunk(&ufx->chunk, OP_NEGATE, 1);
  writeChunk(&ufx->chunk, OP_ADD, 1);
  writeChunk(&ufx->chunk, OP_RETURN, 1);

  InterpretResult ires =
      interpret(ufx->out.fptr, ufx->err.fptr, &ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(ufx->out.fptr);
  const char outMsg[] = "10\n";
  ASSERT_LE(sizeof(outMsg), ufx->out.size);
  EXPECT_STREQ(outMsg, memBufSuffix(ufx->out, outMsg));
}

UTEST_MAIN();
