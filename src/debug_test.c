#include "debug.h"
#include "chunk.h"
#include "utest.h"

#define ufx utest_fixture

typedef struct {
  char* buf;
  size_t size;
} MemBuf;

struct DisassembleChunk {
  Chunk chunk;
  MemBuf out;
  MemBuf err;
  FILE* fout;
  FILE* ferr;
};

UTEST_F_SETUP(DisassembleChunk) {
  ufx->fout = open_memstream(&ufx->out.buf, &ufx->out.size);
  ufx->ferr = open_memstream(&ufx->err.buf, &ufx->err.size);
  initChunk(&ufx->chunk);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(DisassembleChunk) {
  freeChunk(&ufx->chunk);
  free(ufx->out.buf);
  free(ufx->err.buf);
  ASSERT_TRUE(1);
}

UTEST_F(DisassembleChunk, UnknownOpCode) {
  writeChunk(&ufx->chunk, 255, 1);
  disassembleChunk(ufx->fout, ufx->ferr, &ufx->chunk, "");
  fclose(ufx->fout);
  fclose(ufx->ferr);

  ASSERT_LT(0ul, ufx->err.size);
  EXPECT_STREQ("Unknown opcode 255\n", ufx->err.buf);
}

UTEST_F(DisassembleChunk, Chapter14Sample1) {
  writeChunk(&ufx->chunk, OP_RETURN, 123);
  disassembleChunk(ufx->fout, ufx->ferr, &ufx->chunk, "test chunk");
  fclose(ufx->fout);
  fclose(ufx->ferr);

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
  disassembleChunk(ufx->fout, ufx->ferr, &ufx->chunk, "test chunk");
  fclose(ufx->fout);
  fclose(ufx->ferr);

  ASSERT_LT(0ul, ufx->out.size);
  const char outMsg[] =
      "== test chunk ==\n"
      "0000  123 OP_CONSTANT         0 '1.2'\n"
      "0002    | OP_RETURN\n";
  EXPECT_STREQ(outMsg, ufx->out.buf);
  EXPECT_EQ(0ul, ufx->err.size);
}

UTEST_MAIN();
