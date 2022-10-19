#include "compiler.h"

#include <stdio.h>

#include "utest.h"

typedef struct {
  char* buf;
  size_t size;
  FILE* fptr;
} MemBuf;

UTEST(Compiler, PrintOnePlusTwo) {
  MemBuf out;
  out.fptr = open_memstream(&out.buf, &out.size);

  compile(out.fptr, "print 1 + 2;\n");

  fflush(out.fptr);
  const char outMsg[] =
      "   1 31 'print'\n"
      "   | 21 '1'\n"
      "   |  7 '+'\n"
      "   | 21 '2'\n"
      "   |  8 ';'\n"
      "   2 39 ''\n";
  ASSERT_EQ(sizeof(outMsg), out.size + 1);
  EXPECT_STREQ(outMsg, out.buf);

  fclose(out.fptr);
  free(out.buf);
}

UTEST_MAIN();
