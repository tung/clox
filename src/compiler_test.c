#include "compiler.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#include "debug.h"
#include "list.h"
#include "membuf.h"
#include "memory.h"

#define ufx utest_fixture

#define N NUMBER_LIT

// We need enough of ObjString here to work with printObject in
// object.c.  We don't need to set .obj.next since it's only needed for
// the VM.
// clang-format off
#define S(str) { \
    .type = VAL_OBJ, \
    .as.obj = (Obj*)&(ObjString){ \
      .obj = { .type = OBJ_STRING, .next = NULL }, \
      .length = sizeof(str) / sizeof(str[0]) - 1, \
      .chars.ro = str, \
      .borrowed = true, \
    } \
  }
// clang-format on

typedef struct {
  const char* src;
  bool result;
  int codeSize;
  uint8_t* code;
  int valueSize;
  Value* values;
} SourceToChunk;

struct CompileExpr {
  Chunk chunk;
  Obj* objects;
  MemBuf out;
  MemBuf err;
  SourceToChunk* cases;
};

UTEST_I_SETUP(CompileExpr) {
  (void)utest_index;
  initChunk(&ufx->chunk);
  ufx->objects = NULL;
  initMemBuf(&ufx->out);
  initMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(CompileExpr) {
  SourceToChunk* expected = &ufx->cases[utest_index];

  // Prepare expected/actual out/err memstreams.
  MemBuf xOut, xErr, aOut, aErr;
  initMemBuf(&xOut);
  initMemBuf(&xErr);
  initMemBuf(&aOut);
  initMemBuf(&aErr);

  // If success is expected, assemble, dump and free our expected chunk.
  if (expected->result) {
    Chunk expectChunk;
    initChunk(&expectChunk);
    for (int i = 0; i < expected->codeSize; ++i) {
      writeChunk(&expectChunk, expected->code[i], 1);
    }
    for (int i = 0; i < expected->valueSize; ++i) {
      addConstant(&expectChunk, expected->values[i]);
    }
    disassembleChunk(xOut.fptr, xErr.fptr, &expectChunk, "CompileExpr");
    freeChunk(&expectChunk);
  }

  bool result = compile(ufx->out.fptr, ufx->err.fptr, expected->src,
      &ufx->chunk, &ufx->objects);

  EXPECT_EQ(expected->result, result);

  // If success was expected but not achieved, print any compile errors.
  if (expected->result && !result) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }

  // If compile succeeded, dump the actual chunk.
  if (result) {
    disassembleChunk(aOut.fptr, aErr.fptr, &ufx->chunk, "CompileExpr");
  }

  // Compare out memstreams.
  fflush(xOut.fptr);
  fflush(aOut.fptr);
  EXPECT_STREQ(xOut.buf, aOut.buf);

  // Compare err memstreams.
  fflush(xErr.fptr);
  fflush(aErr.fptr);
  EXPECT_STREQ(xErr.buf, aErr.buf);

  // Clean up memstreams.
  freeMemBuf(&xOut);
  freeMemBuf(&xErr);
  freeMemBuf(&aOut);
  freeMemBuf(&aErr);

  // Fixture teardown.
  freeChunk(&ufx->chunk);
  freeObjects(ufx->objects);
  freeMemBuf(&ufx->out);
  freeMemBuf(&ufx->err);
}

#define COMPILE_EXPRS(name, data, count) \
  UTEST_I(CompileExpr, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

SourceToChunk exprErrors[] = {
  { "", false, LIST(uint8_t), LIST(Value) },
  { "#", false, LIST(uint8_t), LIST(Value) },
  { "1 1", false, LIST(uint8_t), LIST(Value) },
};

COMPILE_EXPRS(Errors, exprErrors, 3);

SourceToChunk exprLiteral[] = {
  { "false", true, LIST(uint8_t, OP_FALSE, OP_RETURN), LIST(Value) },
  { "nil", true, LIST(uint8_t, OP_NIL, OP_RETURN), LIST(Value) },
  { "true", true, LIST(uint8_t, OP_TRUE, OP_RETURN), LIST(Value) },
};

COMPILE_EXPRS(Literal, exprLiteral, 3);

SourceToChunk exprNumber[] = {
  { "123", true, LIST(uint8_t, OP_CONSTANT, 0, OP_RETURN),
      LIST(Value, N(123.0)) },
};

COMPILE_EXPRS(Number, exprNumber, 1);

SourceToChunk exprUnary[] = {
  { "-1", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_RETURN),
      LIST(Value, N(1.0)) },
  { "--1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_RETURN),
      LIST(Value, N(1.0)) },
  { "!false", true, LIST(uint8_t, OP_FALSE, OP_NOT, OP_RETURN),
      LIST(Value) },
  { "!!false", true, LIST(uint8_t, OP_FALSE, OP_NOT, OP_NOT, OP_RETURN),
      LIST(Value) },
};

COMPILE_EXPRS(Unary, exprUnary, 4);

SourceToChunk exprGrouping[] = {
  { "(", false, LIST(uint8_t), LIST(Value) },
  { "(1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_RETURN),
      LIST(Value, N(1.0)) },
  { "(-1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_RETURN),
      LIST(Value, N(1.0)) },
  { "-(1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_RETURN),
      LIST(Value, N(1.0)) },
  { "-(-1)", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_RETURN),
      LIST(Value, N(1.0)) },
};

COMPILE_EXPRS(Grouping, exprGrouping, 5);

SourceToChunk exprBinaryNums[] = {
  { "3 + 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
  { "3 - 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT,
          OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
  { "3 * 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
  { "3 / 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE,
          OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
  { "4 + 3 - 2 + 1 - 0", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_SUBTRACT, OP_CONSTANT, 3, OP_ADD, OP_CONSTANT, 4,
          OP_SUBTRACT, OP_RETURN),
      LIST(Value, N(4.0), N(3.0), N(2.0), N(1.0), N(0.0)) },
  { "4 / 3 * 2 / 1 * 0", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_DIVIDE,
          OP_CONSTANT, 4, OP_MULTIPLY, OP_RETURN),
      LIST(Value, N(4.0), N(3.0), N(2.0), N(1.0), N(0.0)) },
  { "3 * 2 + 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_CONSTANT, 2, OP_ADD, OP_RETURN),
      LIST(Value, N(3.0), N(2.0), N(1.0)) },
  { "3 + 2 * 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
          OP_MULTIPLY, OP_ADD, OP_RETURN),
      LIST(Value, N(3.0), N(2.0), N(1.0)) },
  { "(-1 + 2) * 3 - -4", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_CONSTANT, 1, OP_ADD,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_NEGATE,
          OP_SUBTRACT, OP_RETURN),
      LIST(Value, N(1.0), N(2.0), N(3.0), N(4.0)) },
};

COMPILE_EXPRS(BinaryNums, exprBinaryNums, 9);

SourceToChunk exprBinaryCompare[] = {
  { "true != true", true,
      LIST(uint8_t, OP_TRUE, OP_TRUE, OP_EQUAL, OP_NOT, OP_RETURN),
      LIST(Value) },
  { "true == true", true,
      LIST(uint8_t, OP_TRUE, OP_TRUE, OP_EQUAL, OP_RETURN),
      LIST(Value) },
  { "0 > 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_RETURN),
      LIST(Value, N(0.0), N(1.0)) },
  { "0 >= 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_NOT,
          OP_RETURN),
      LIST(Value, N(0.0), N(1.0)) },
  { "0 < 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_RETURN),
      LIST(Value, N(0.0), N(1.0)) },
  { "0 <= 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER, OP_NOT,
          OP_RETURN),
      LIST(Value, N(0.0), N(1.0)) },
};

COMPILE_EXPRS(BinaryCompare, exprBinaryCompare, 6);

SourceToChunk exprConcatStrings[] = {
  { "\"\" + \"\"", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_RETURN),
      LIST(Value, S(""), S("")) },
  { "\"foo\" + \"bar\"", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_RETURN),
      LIST(Value, S("foo"), S("bar")) },
  { "\"foo\" + \"bar\" + \"baz\"", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_ADD, OP_RETURN),
      LIST(Value, S("foo"), S("bar"), S("baz")) },
};

COMPILE_EXPRS(ConcatStrings, exprConcatStrings, 3);

UTEST_MAIN();
