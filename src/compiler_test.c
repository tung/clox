#include "compiler.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#include "debug.h"
#include "list.h"

#define ufx utest_fixture

typedef struct {
  char* buf;
  size_t size;
  FILE* fptr;
} MemBuf;

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
  MemBuf out;
  MemBuf err;
  SourceToChunk* cases;
};

UTEST_I_SETUP(CompileExpr) {
  (void)utest_index;
  initChunk(&ufx->chunk);
  ufx->out.fptr = open_memstream(&ufx->out.buf, &ufx->out.size);
  ufx->err.fptr = open_memstream(&ufx->err.buf, &ufx->err.size);
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(CompileExpr) {
  SourceToChunk* expected = &ufx->cases[utest_index];

  // Prepare expected/actual out/err memstreams.
  MemBuf xOut, xErr, aOut, aErr;
  xOut.fptr = open_memstream(&xOut.buf, &xOut.size);
  xErr.fptr = open_memstream(&xErr.buf, &xErr.size);
  aOut.fptr = open_memstream(&aOut.buf, &aOut.size);
  aErr.fptr = open_memstream(&aErr.buf, &aErr.size);

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

  bool result =
      compile(ufx->out.fptr, ufx->err.fptr, expected->src, &ufx->chunk);

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
  fclose(xOut.fptr);
  fclose(xErr.fptr);
  fclose(aOut.fptr);
  fclose(aErr.fptr);
  free(xOut.buf);
  free(xErr.buf);
  free(aOut.buf);
  free(aErr.buf);

  // Fixture teardown.
  freeChunk(&ufx->chunk);
  fclose(ufx->out.fptr);
  fclose(ufx->err.fptr);
  free(ufx->out.buf);
  free(ufx->err.buf);
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

SourceToChunk exprNumber[] = {
  { "123", true, LIST(uint8_t, OP_CONSTANT, 0, OP_RETURN),
      LIST(Value, 123.0) },
};

COMPILE_EXPRS(Number, exprNumber, 1);

SourceToChunk exprUnary[] = {
  { "-1", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_RETURN),
      LIST(Value, 1.0) },
  { "--1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_RETURN),
      LIST(Value, 1.0) },
};

COMPILE_EXPRS(Unary, exprUnary, 2);

SourceToChunk exprGrouping[] = {
  { "(", false, LIST(uint8_t), LIST(Value) },
  { "(1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_RETURN),
      LIST(Value, 1.0) },
  { "(-1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_RETURN),
      LIST(Value, 1.0) },
  { "-(1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_RETURN),
      LIST(Value, 1.0) },
  { "-(-1)", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_RETURN),
      LIST(Value, 1.0) },
};

COMPILE_EXPRS(Grouping, exprGrouping, 5);

SourceToChunk exprBinary[] = {
  { "3 + 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_RETURN),
      LIST(Value, 3.0, 2.0) },
  { "3 - 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT,
          OP_RETURN),
      LIST(Value, 3.0, 2.0) },
  { "3 * 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_RETURN),
      LIST(Value, 3.0, 2.0) },
  { "3 / 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE,
          OP_RETURN),
      LIST(Value, 3.0, 2.0) },
  { "4 + 3 - 2 + 1 - 0", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_SUBTRACT, OP_CONSTANT, 3, OP_ADD, OP_CONSTANT, 4,
          OP_SUBTRACT, OP_RETURN),
      LIST(Value, 4.0, 3.0, 2.0, 1.0, 0.0) },
  { "4 / 3 * 2 / 1 * 0", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_DIVIDE,
          OP_CONSTANT, 4, OP_MULTIPLY, OP_RETURN),
      LIST(Value, 4.0, 3.0, 2.0, 1.0, 0.0) },
  { "3 * 2 + 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_CONSTANT, 2, OP_ADD, OP_RETURN),
      LIST(Value, 3.0, 2.0, 1.0) },
  { "3 + 2 * 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
          OP_MULTIPLY, OP_ADD, OP_RETURN),
      LIST(Value, 3.0, 2.0, 1.0) },
  { "(-1 + 2) * 3 - -4", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_CONSTANT, 1, OP_ADD,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_NEGATE,
          OP_SUBTRACT, OP_RETURN),
      LIST(Value, 1.0, 2.0, 3.0, 4.0) },
};

COMPILE_EXPRS(Binary, exprBinary, 9);

UTEST_MAIN();
