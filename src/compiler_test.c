#include "compiler.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#define ufx utest_fixture

typedef struct {
  char* buf;
  size_t size;
  FILE* fptr;
} MemBuf;

typedef struct {
  const char* src;
  bool result;
  uint8_t* code;
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
  bool result = compile(ufx->out.fptr, ufx->err.fptr,
      ufx->cases[utest_index].src, &ufx->chunk);
  EXPECT_EQ(ufx->cases[utest_index].result, result);

  // Compare expected and actual code.
  if (ufx->cases[utest_index].code) {
    int count = 0;
    uint8_t* o = ufx->cases[utest_index].code;
    while (*o != 255 && count < ufx->chunk.count) {
      if (*utest_result == UTEST_TEST_FAILURE) {
        break;
      }
      EXPECT_EQ(*o, ufx->chunk.code[count]);
      count++;
      o++;
    }
    if (*utest_result == UTEST_TEST_PASSED) {
      EXPECT_EQ(count, ufx->chunk.count);
    }
  }

  // Compare expected and actual values.
  if (ufx->cases[utest_index].values) {
    int count = 0;
    Value* v = ufx->cases[utest_index].values;
    while (*v != 255.0 && count < ufx->chunk.constants.count) {
      if (*utest_result == UTEST_TEST_FAILURE) {
        break;
      }
      EXPECT_EQ(*v, ufx->chunk.constants.values[count]);
      count++;
      v++;
    }
    if (*utest_result == UTEST_TEST_PASSED) {
      EXPECT_EQ(count, ufx->chunk.constants.count);
    }
  }

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
  { "", false, NULL, NULL },
  { "#", false, NULL, NULL },
  { "1 1", false, NULL, NULL },
};

COMPILE_EXPRS(Errors, exprErrors, 3);

SourceToChunk exprNumber[] = {
  { "123", true, (uint8_t[]){ OP_CONSTANT, 0, OP_RETURN, 255 },
      (Value[]){ 123.0, 255.0 } },
};

COMPILE_EXPRS(Number, exprNumber, 1);

SourceToChunk exprUnary[] = {
  { "-1", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_NEGATE, OP_RETURN, 255 },
      (Value[]){ 1.0, 255.0 } },
  { "--1", true,
      (uint8_t[]){
          OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_RETURN, 255 },
      (Value[]){ 1.0, 255.0 } },
};

COMPILE_EXPRS(Unary, exprUnary, 2);

SourceToChunk exprGrouping[] = {
  { "(", false, NULL, NULL },
  { "(1)", true, (uint8_t[]){ OP_CONSTANT, 0, OP_RETURN, 255 },
      (Value[]){ 1.0, 255.0 } },
  { "(-1)", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_NEGATE, OP_RETURN, 255 },
      (Value[]){ 1.0, 255.0 } },
  { "-(1)", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_NEGATE, OP_RETURN, 255 },
      (Value[]){ 1.0, 255.0 } },
  { "-(-1)", true,
      (uint8_t[]){
          OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_RETURN, 255 },
      (Value[]){ 1.0, 255.0 } },
};

COMPILE_EXPRS(Grouping, exprGrouping, 5);

SourceToChunk exprBinary[] = {
  { "3 + 2", true,
      (uint8_t[]){
          OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_RETURN, 255 },
      (Value[]){ 3.0, 2.0, 255.0 } },
  { "3 - 2", true,
      (uint8_t[]){
          OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT, OP_RETURN, 255 },
      (Value[]){ 3.0, 2.0, 255.0 } },
  { "3 * 2", true,
      (uint8_t[]){
          OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY, OP_RETURN, 255 },
      (Value[]){ 3.0, 2.0, 255.0 } },
  { "3 / 2", true,
      (uint8_t[]){
          OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE, OP_RETURN, 255 },
      (Value[]){ 3.0, 2.0, 255.0 } },
  { "4 + 3 - 2 + 1 - 0", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_SUBTRACT, OP_CONSTANT, 3, OP_ADD, OP_CONSTANT, 4,
          OP_SUBTRACT, OP_RETURN, 255 },
      (Value[]){ 4.0, 3.0, 2.0, 1.0, 0.0, 255.0 } },
  { "4 / 3 * 2 / 1 * 0", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_DIVIDE,
          OP_CONSTANT, 4, OP_MULTIPLY, OP_RETURN, 255 },
      (Value[]){ 4.0, 3.0, 2.0, 1.0, 0.0, 255.0 } },
  { "3 * 2 + 1", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_CONSTANT, 2, OP_ADD, OP_RETURN, 255 },
      (Value[]){ 3.0, 2.0, 1.0, 255.0 } },
  { "3 + 2 * 1", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
          OP_MULTIPLY, OP_ADD, OP_RETURN, 255 },
      (Value[]){ 3.0, 2.0, 1.0, 255.0 } },
  { "(-1 + 2) * 3 - -4", true,
      (uint8_t[]){ OP_CONSTANT, 0, OP_NEGATE, OP_CONSTANT, 1, OP_ADD,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_NEGATE,
          OP_SUBTRACT, OP_RETURN, 255 },
      (Value[]){ 1.0, 2.0, 3.0, 4.0, 255.0 } },
};

COMPILE_EXPRS(Binary, exprBinary, 9);

UTEST_MAIN();
