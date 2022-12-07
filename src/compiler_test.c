#include "compiler.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#include "debug.h"
#include "gc.h"
#include "list.h"
#include "membuf.h"
#include "memory.h"

#define ufx utest_fixture

typedef struct {
  const char* src;
  bool result;
  int codeSize;
  uint8_t* code;
  int valueSize;
  Value* values;
} SourceToChunk;

#define COMPILE_EXPRS(name, data, count) \
  UTEST_I(CompileExpr, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

static void dump(MemBuf* out, ObjFunction* fun) {
  for (int i = 0; i < fun->chunk.constants.count; ++i) {
    if (IS_FUNCTION(fun->chunk.constants.values[i])) {
      dump(out, AS_FUNCTION(fun->chunk.constants.values[i]));
    }
  }
  disassembleChunk(out->fptr, &fun->chunk,
      fun->name ? fun->name->chars : "<script>");
}

typedef struct {
  bool success;
  const char* src;
  const char* msg;
} SourceToDump;

struct DumpSrc {
  SourceToDump* cases;
};

UTEST_I_SETUP(DumpSrc) {
  (void)utest_index;
  (void)utest_fixture;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(DumpSrc) {
  SourceToDump* expected = &ufx->cases[utest_index];

  GC gc;
  Table strings;
  MemBuf out, err;

  initGC(&gc);
  initTable(&strings, 0.75);
  initMemBuf(&out);
  initMemBuf(&err);

  ObjFunction* result =
      compile(out.fptr, err.fptr, expected->src, &gc, &strings);
  EXPECT_EQ(expected->success, !!result);

  if (result) {
    dump(&out, result);
  }

  fflush(out.fptr);
  fflush(err.fptr);
  if (expected->success) {
    EXPECT_STREQ(expected->msg, out.buf);
    EXPECT_STREQ("", err.buf);
  } else {
    const char* findMsg = strstr(err.buf, expected->msg);
    if (expected->msg && expected->msg[0] && findMsg) {
      EXPECT_STRNEQ(expected->msg, findMsg, strlen(expected->msg));
    } else {
      EXPECT_STREQ(expected->msg, err.buf);
    }
  }

  freeTable(&gc, &strings);
  freeGC(&gc);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

#define DUMP_SRC(name, data, count) \
  UTEST_I(DumpSrc, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

SourceToDump literals[] = {
  { true, "false;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_POP\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n" },
  { true, "nil;",
      "== <script> ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_POP\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n" },
  { true, "true;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_POP\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n" },
};

DUMP_SRC(Literals, literals, 3);

SourceToDump numbers[] = {
  { true, "123;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '123'\n"
      "0002    | OP_POP\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n" },
};

DUMP_SRC(Numbers, numbers, 1);

SourceToDump strings[] = {
  { true, "\"\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 ''\n"
      "0002    | OP_POP\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n" },
  { true, "\"foo\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0002    | OP_POP\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n" },
};

DUMP_SRC(Strings, strings, 2);

SourceToDump varGet[] = {
  { true, "foo;",
      "== <script> ==\n"
      "0000    1 OP_GET_GLOBAL       0 'foo'\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "foo + foo;",
      "== <script> ==\n"
      "0000    1 OP_GET_GLOBAL       0 'foo'\n"
      "0003    | OP_GET_GLOBAL       0 'foo'\n"
      "0006    | OP_ADD\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "foo + bar + foo + bar;",
      "== <script> ==\n"
      "0000    1 OP_GET_GLOBAL       0 'foo'\n"
      "0003    | OP_GET_GLOBAL       1 'bar'\n"
      "0006    | OP_ADD\n"
      "0007    | OP_GET_GLOBAL       0 'foo'\n"
      "0010    | OP_ADD\n"
      "0011    | OP_GET_GLOBAL       1 'bar'\n"
      "0014    | OP_ADD\n"
      "0015    | OP_POP\n"
      "0016    | OP_NIL\n"
      "0017    | OP_RETURN\n" },
};

DUMP_SRC(VarGet, varGet, 3);

SourceToDump varSet[] = {
  { false, "foo + bar = 0;", "Invalid assignment target." },
  { true, "foo = 0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '0'\n"
      "0002    | OP_SET_GLOBAL       0 'foo'\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
};

DUMP_SRC(VarSet, varSet, 2);

SourceToDump unary[] = {
  { true, "-1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "--1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "!false;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_NOT\n"
      "0002    | OP_POP\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n" },
  { true, "!!false;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_NOT\n"
      "0002    | OP_NOT\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
};

DUMP_SRC(Unary, unary, 4);

SourceToDump grouping[] = {
  { false, "(;", "Expect expression." },
  { true, "(1);",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_POP\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n" },
  { true, "(-1);",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "-(1);",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "-(-1);",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
};

DUMP_SRC(Grouping, grouping, 5);

SourceToDump binaryNums[] = {
  { true, "3 + 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0002    | OP_ADD_C            1 '2'\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "3 - 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0002    | OP_SUBTRACT_C       1 '2'\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "3 * 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_MULTIPLY\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "3 / 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_DIVIDE\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "3 % 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_MODULO\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "4 + 3 % 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '4'\n"
      "0002    | OP_CONSTANT         1 '3'\n"
      "0004    | OP_CONSTANT         2 '2'\n"
      "0006    | OP_MODULO\n"
      "0007    | OP_ADD\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "4 + 3 - 2 + 1 - 0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '4'\n"
      "0002    | OP_ADD_C            1 '3'\n"
      "0004    | OP_SUBTRACT_C       2 '2'\n"
      "0006    | OP_ADD_C            3 '1'\n"
      "0008    | OP_SUBTRACT_C       4 '0'\n"
      "0010    | OP_POP\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
  { true, "4 / 3 * 2 / 1 * 0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '4'\n"
      "0002    | OP_CONSTANT         1 '3'\n"
      "0004    | OP_DIVIDE\n"
      "0005    | OP_CONSTANT         2 '2'\n"
      "0007    | OP_MULTIPLY\n"
      "0008    | OP_CONSTANT         3 '1'\n"
      "0010    | OP_DIVIDE\n"
      "0011    | OP_CONSTANT         4 '0'\n"
      "0013    | OP_MULTIPLY\n"
      "0014    | OP_POP\n"
      "0015    | OP_NIL\n"
      "0016    | OP_RETURN\n" },
  { true, "3 * 2 + 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_MULTIPLY\n"
      "0005    | OP_ADD_C            2 '1'\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "3 + 2 * 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_CONSTANT         2 '1'\n"
      "0006    | OP_MULTIPLY\n"
      "0007    | OP_ADD\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "(-1 + 2) * 3 - -4;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_NEGATE\n"
      "0003    | OP_ADD_C            1 '2'\n"
      "0005    | OP_CONSTANT         2 '3'\n"
      "0007    | OP_MULTIPLY\n"
      "0008    | OP_CONSTANT         3 '4'\n"
      "0010    | OP_NEGATE\n"
      "0011    | OP_SUBTRACT\n"
      "0012    | OP_POP\n"
      "0013    | OP_NIL\n"
      "0014    | OP_RETURN\n" },
};

DUMP_SRC(BinaryNums, binaryNums, 11);

SourceToDump binaryCompare[] = {
  { true, "true != true;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_TRUE\n"
      "0002    | OP_EQUAL\n"
      "0003    | OP_NOT\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "true == true;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_TRUE\n"
      "0002    | OP_EQUAL\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "0 > 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_CONSTANT         1 '1'\n"
      "0004    | OP_GREATER\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "0 >= 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_LESS_C           1 '1'\n"
      "0004    | OP_NOT\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "0 < 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_LESS_C           1 '1'\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "0 <= 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_CONSTANT         1 '1'\n"
      "0004    | OP_GREATER\n"
      "0005    | OP_NOT\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "0 + 1 < 2 == true;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_ADD_C            1 '1'\n"
      "0004    | OP_LESS_C           2 '2'\n"
      "0006    | OP_TRUE\n"
      "0007    | OP_EQUAL\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "true == 0 < 1 + 2;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_CONSTANT         0 '0'\n"
      "0003    | OP_CONSTANT         1 '1'\n"
      "0005    | OP_ADD_C            2 '2'\n"
      "0007    | OP_LESS\n"
      "0008    | OP_EQUAL\n"
      "0009    | OP_POP\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
  { true, "0 >= 1 + 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_CONSTANT         1 '1'\n"
      "0004    | OP_ADD_C            2 '2'\n"
      "0006    | OP_LESS\n"
      "0007    | OP_NOT\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
};

DUMP_SRC(BinaryCompare, binaryCompare, 9);

SourceToDump addStrings[] = {
  { true, "\"\" + \"\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 ''\n"
      "0002    | OP_ADD_C            0 ''\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "\"foo\" + \"bar\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0002    | OP_ADD_C            1 'bar'\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "\"foo\" + \"bar\" + \"baz\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0002    | OP_ADD_C            1 'bar'\n"
      "0004    | OP_ADD_C            2 'baz'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "\"foo\" + \"bar\" + \"foo\" + \"bar\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0002    | OP_ADD_C            1 'bar'\n"
      "0004    | OP_ADD_C            0 'foo'\n"
      "0006    | OP_ADD_C            1 'bar'\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
};

DUMP_SRC(AddStrings, addStrings, 4);

SourceToDump logical[] = {
  { true, "true and false;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_JUMP_IF_FALSE    1 -> 6\n"
      "0004    | OP_POP\n"
      "0005    | OP_FALSE\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "false or true;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_JUMP_IF_FALSE    1 -> 7\n"
      "0004    | OP_JUMP             4 -> 9\n"
      "0007    | OP_POP\n"
      "0008    | OP_TRUE\n"
      "0009    | OP_POP\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
  { true, "0 == 1 and 2 or 3;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_CONSTANT         1 '1'\n"
      "0004    | OP_EQUAL\n"
      "0005    | OP_JUMP_IF_FALSE    5 -> 11\n"
      "0008    | OP_POP\n"
      "0009    | OP_CONSTANT         2 '2'\n"
      "0011    | OP_JUMP_IF_FALSE   11 -> 17\n"
      "0014    | OP_JUMP            14 -> 20\n"
      "0017    | OP_POP\n"
      "0018    | OP_CONSTANT         3 '3'\n"
      "0020    | OP_POP\n"
      "0021    | OP_NIL\n"
      "0022    | OP_RETURN\n" },
  { true, "0 or 1 and 2 == 3;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_JUMP_IF_FALSE    2 -> 8\n"
      "0005    | OP_JUMP             5 -> 20\n"
      "0008    | OP_POP\n"
      "0009    | OP_CONSTANT         1 '1'\n"
      "0011    | OP_JUMP_IF_FALSE   11 -> 20\n"
      "0014    | OP_POP\n"
      "0015    | OP_CONSTANT         2 '2'\n"
      "0017    | OP_CONSTANT         3 '3'\n"
      "0019    | OP_EQUAL\n"
      "0020    | OP_POP\n"
      "0021    | OP_NIL\n"
      "0022    | OP_RETURN\n" },
};

DUMP_SRC(Logical, logical, 4);

SourceToDump functions[] = {
  { false, "fun", "Expect function name." },
  { false, "fun a", "Expect '(' after function name." },
  { false, "fun a(", "Expect parameter name." },
  { false, "fun a(,", "Expect parameter name." },
  { false, "fun a()", "Expect '{' before function body." },
  { false, "fun a(x", "Expect ')' after parameters." },
  { false, "fun a(x,", "Expect parameter name." },
  { false, "fun a(x,)", "Expect '{' before function body." },
  { false, "fun a(x,,)", "Expect parameter name." },
  { false, "fun a(x,y){", "Expect '}' after block." },
  { false, "fun a(x,y,){", "Expect '}' after block." },
  { false, "return", "Can't return from top-level code." },
  { false, "fun a(){return", "Expect expression." },
  { false, "fun a(){return;", "Expect '}' after block." },
  { false, "a(", "Expect expression." },
  { false, "a(0", "Expect ')' after arguments." },
  { false, "a(0,,);", "Expect expression." },
  { false, "(fun a(){})();", "Expect '(' after 'fun'." },
  { true, "fun a(){print 1;}a();",
      "== a ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn a>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       0 'a'\n"
      "0007    | OP_CALL             0\n"
      "0009    | OP_POP\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
  { true, "(fun(){print 1;})();",
      "== () ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '<fn ()>'\n"
      "0002    | OP_CALL             0\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "fun a(x){print x;}a(1);",
      "== a ==\n"
      "0000    1 OP_GET_LOCAL        1\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn a>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       0 'a'\n"
      "0007    | OP_CONSTANT         2 '1'\n"
      "0009    | OP_CALL             1\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "(fun(x){print x;})(x);",
      "== () ==\n"
      "0000    1 OP_GET_LOCAL        1\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '<fn ()>'\n"
      "0002    | OP_GET_GLOBAL       1 'x'\n"
      "0005    | OP_CALL             1\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "fun a(){return 1;}print a();",
      "== a ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_RETURN\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn a>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       0 'a'\n"
      "0007    | OP_CALL             0\n"
      "0009    | OP_PRINT\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
  { true, "print(fun(){return 1;})();",
      "== () ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_RETURN\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '<fn ()>'\n"
      "0002    | OP_CALL             0\n"
      "0004    | OP_PRINT\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "fun a(x,y){return x+y;}print a(3,a(2,1),);",
      "== a ==\n"
      "0000    1 OP_GET_LOCAL        1\n"
      "0002    | OP_GET_LOCAL        2\n"
      "0004    | OP_ADD\n"
      "0005    | OP_RETURN\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn a>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       0 'a'\n"
      "0007    | OP_CONSTANT         2 '3'\n"
      "0009    | OP_GET_GLOBAL       0 'a'\n"
      "0012    | OP_CONSTANT         3 '2'\n"
      "0014    | OP_CONSTANT         4 '1'\n"
      "0016    | OP_CALL             2\n"
      "0018    | OP_CALL             2\n"
      "0020    | OP_PRINT\n"
      "0021    | OP_NIL\n"
      "0022    | OP_RETURN\n" },
  { true, "var x=1;fun a(){fun b(){print x;}b();}a();",
      "== b ==\n"
      "0000    1 OP_GET_GLOBAL       0 'x'\n"
      "0003    | OP_PRINT\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== a ==\n"
      "0000    1 OP_CONSTANT         0 '<fn b>'\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_CALL             0\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '1'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'x'\n"
      "0004    | OP_CONSTANT         3 '<fn a>'\n"
      "0006    | OP_DEFINE_GLOBAL    2 'a'\n"
      "0008    | OP_GET_GLOBAL       2 'a'\n"
      "0011    | OP_CALL             0\n"
      "0013    | OP_POP\n"
      "0014    | OP_NIL\n"
      "0015    | OP_RETURN\n" },
};

DUMP_SRC(Functions, functions, 26);

SourceToDump closures[] = {
  { true,
      "fun counter(n) {"
      "  fun incAndPrint() {"
      "    var prevN = n;"
      "    print n;"
      "    n = n + 1;"
      "    return prevN;"
      "  }"
      "  return incAndPrint;"
      "}"
      "var c = counter(1);"
      "c(); c(); c();",
      "== incAndPrint ==\n"
      "0000    1 OP_GET_UPVALUE      0\n"
      "0002    | OP_GET_UPVALUE      0\n"
      "0004    | OP_PRINT\n"
      "0005    | OP_GET_UPVALUE      0\n"
      "0007    | OP_ADD_C            0 '1'\n"
      "0009    | OP_SET_UPVALUE      0\n"
      "0011    | OP_POP\n"
      "0012    | OP_GET_LOCAL        1\n"
      "0014    | OP_RETURN\n"
      "0015    | OP_NIL\n"
      "0016    | OP_RETURN\n"
      "== counter ==\n"
      "0000    1 OP_CLOSURE          0 <fn incAndPrint>\n"
      "0002      |                     local 1\n"
      "0004    | OP_GET_LOCAL        2\n"
      "0006    | OP_RETURN\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn counter>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'counter'\n"
      "0004    | OP_GET_GLOBAL       0 'counter'\n"
      "0007    | OP_CONSTANT         3 '1'\n"
      "0009    | OP_CALL             1\n"
      "0011    | OP_DEFINE_GLOBAL    2 'c'\n"
      "0013    | OP_GET_GLOBAL       2 'c'\n"
      "0016    | OP_CALL             0\n"
      "0018    | OP_POP\n"
      "0019    | OP_GET_GLOBAL       2 'c'\n"
      "0022    | OP_CALL             0\n"
      "0024    | OP_POP\n"
      "0025    | OP_GET_GLOBAL       2 'c'\n"
      "0028    | OP_CALL             0\n"
      "0030    | OP_POP\n"
      "0031    | OP_NIL\n"
      "0032    | OP_RETURN\n" },
  { true,
      "{"
      "  var x = 1;"
      "  var y = 2;"
      "  fun f(a, b) {"
      "    fun g() {"
      "      print a + b + x + y;"
      "    }"
      "    return g;"
      "  }"
      "}"
      "f(3, 4);",
      "== g ==\n"
      "0000    1 OP_GET_UPVALUE      0\n"
      "0002    | OP_GET_UPVALUE      1\n"
      "0004    | OP_ADD\n"
      "0005    | OP_GET_UPVALUE      2\n"
      "0007    | OP_ADD\n"
      "0008    | OP_GET_UPVALUE      3\n"
      "0010    | OP_ADD\n"
      "0011    | OP_PRINT\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n"
      "== f ==\n"
      "0000    1 OP_CLOSURE          0 <fn g>\n"
      "0002      |                     local 1\n"
      "0004      |                     local 2\n"
      "0006      |                     upvalue 0\n"
      "0008      |                     upvalue 1\n"
      "0010    | OP_GET_LOCAL        3\n"
      "0012    | OP_RETURN\n"
      "0013    | OP_NIL\n"
      "0014    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_CLOSURE          2 <fn f>\n"
      "0006      |                     local 1\n"
      "0008      |                     local 2\n"
      "0010    | OP_POP\n"
      "0011    | OP_CLOSE_UPVALUE\n"
      "0012    | OP_CLOSE_UPVALUE\n"
      "0013    | OP_GET_GLOBAL       3 'f'\n"
      "0016    | OP_CONSTANT         4 '3'\n"
      "0018    | OP_CONSTANT         5 '4'\n"
      "0020    | OP_CALL             2\n"
      "0022    | OP_POP\n"
      "0023    | OP_NIL\n"
      "0024    | OP_RETURN\n" },
  { true,
      "fun outer(x) {"
      "  fun middle(y) {"
      "    fun inner(z) {"
      "      return x + y + z;"
      "    }"
      "    return inner;"
      "  }"
      "  return middle;"
      "}"
      "print outer(1)(2)(3);",
      "== inner ==\n"
      "0000    1 OP_GET_UPVALUE      0\n"
      "0002    | OP_GET_UPVALUE      1\n"
      "0004    | OP_ADD\n"
      "0005    | OP_GET_LOCAL        1\n"
      "0007    | OP_ADD\n"
      "0008    | OP_RETURN\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n"
      "== middle ==\n"
      "0000    1 OP_CLOSURE          0 <fn inner>\n"
      "0002      |                     upvalue 0\n"
      "0004      |                     local 1\n"
      "0006    | OP_GET_LOCAL        2\n"
      "0008    | OP_RETURN\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n"
      "== outer ==\n"
      "0000    1 OP_CLOSURE          0 <fn middle>\n"
      "0002      |                     local 1\n"
      "0004    | OP_GET_LOCAL        2\n"
      "0006    | OP_RETURN\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn outer>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'outer'\n"
      "0004    | OP_GET_GLOBAL       0 'outer'\n"
      "0007    | OP_CONSTANT         2 '1'\n"
      "0009    | OP_CALL             1\n"
      "0011    | OP_CONSTANT         3 '2'\n"
      "0013    | OP_CALL             1\n"
      "0015    | OP_CONSTANT         4 '3'\n"
      "0017    | OP_CALL             1\n"
      "0019    | OP_PRINT\n"
      "0020    | OP_NIL\n"
      "0021    | OP_RETURN\n" },
  { true,
      "var f; var g; var h;"
      "{"
      "  var x = \"x\"; var y = \"y\"; var z = \"z\";"
      "  fun ff() { print z; } f = ff;"
      "  fun gg() { print x; } g = gg;"
      "  fun hh() { print y; } h = hh;"
      "}"
      "f(); g(); h();",
      "== ff ==\n"
      "0000    1 OP_GET_UPVALUE      0\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== gg ==\n"
      "0000    1 OP_GET_UPVALUE      0\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== hh ==\n"
      "0000    1 OP_GET_UPVALUE      0\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_DEFINE_GLOBAL    0 'f'\n"
      "0003    | OP_NIL\n"
      "0004    | OP_DEFINE_GLOBAL    1 'g'\n"
      "0006    | OP_NIL\n"
      "0007    | OP_DEFINE_GLOBAL    2 'h'\n"
      "0009    | OP_CONSTANT         3 'x'\n"
      "0011    | OP_CONSTANT         4 'y'\n"
      "0013    | OP_CONSTANT         5 'z'\n"
      "0015    | OP_CLOSURE          6 <fn ff>\n"
      "0017      |                     local 3\n"
      "0019    | OP_GET_LOCAL        4\n"
      "0021    | OP_SET_GLOBAL       0 'f'\n"
      "0024    | OP_POP\n"
      "0025    | OP_CLOSURE          7 <fn gg>\n"
      "0027      |                     local 1\n"
      "0029    | OP_GET_LOCAL        5\n"
      "0031    | OP_SET_GLOBAL       1 'g'\n"
      "0034    | OP_POP\n"
      "0035    | OP_CLOSURE          8 <fn hh>\n"
      "0037      |                     local 2\n"
      "0039    | OP_GET_LOCAL        6\n"
      "0041    | OP_SET_GLOBAL       2 'h'\n"
      "0044    | OP_POP\n"
      "0045    | OP_POP\n"
      "0046    | OP_POP\n"
      "0047    | OP_POP\n"
      "0048    | OP_CLOSE_UPVALUE\n"
      "0049    | OP_CLOSE_UPVALUE\n"
      "0050    | OP_CLOSE_UPVALUE\n"
      "0051    | OP_GET_GLOBAL       0 'f'\n"
      "0054    | OP_CALL             0\n"
      "0056    | OP_POP\n"
      "0057    | OP_GET_GLOBAL       1 'g'\n"
      "0060    | OP_CALL             0\n"
      "0062    | OP_POP\n"
      "0063    | OP_GET_GLOBAL       2 'h'\n"
      "0066    | OP_CALL             0\n"
      "0068    | OP_POP\n"
      "0069    | OP_NIL\n"
      "0070    | OP_RETURN\n" },
};

DUMP_SRC(Closures, closures, 4);

SourceToDump varDecl[] = {
  { false, "var", "Expect variable name." },
  { false, "var 0", "Expect variable name." },
  { false, "var foo", "Expect ';' after variable declaration." },
  { true, "var foo;",
      "== <script> ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_DEFINE_GLOBAL    0 'foo'\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n" },
  { true, "var foo=0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '0'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'foo'\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "{var foo;}",
      "== <script> ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_POP\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n" },
  { true, "{var foo=0;}",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_POP\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n" },
};

DUMP_SRC(VarDecl, varDecl, 7);

SourceToDump localVars[] = {
  { false, "{var x=x;}",
      "Can't read local variable in its own initializer." },
  { false, "{var x;var x;}",
      "Already a variable with this name in this scope." },
  { true, "{var foo=123;print foo;}",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '123'\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_PRINT\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "{var a=1;var foo=2;print a+foo;}",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_GET_LOCAL        1\n"
      "0006    | OP_GET_LOCAL        2\n"
      "0008    | OP_ADD\n"
      "0009    | OP_PRINT\n"
      "0010    | OP_POP\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "var a=1;{var a=2;print a;}print a;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '1'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_CONSTANT         2 '2'\n"
      "0006    | OP_GET_LOCAL        1\n"
      "0008    | OP_PRINT\n"
      "0009    | OP_POP\n"
      "0010    | OP_GET_GLOBAL       0 'a'\n"
      "0013    | OP_PRINT\n"
      "0014    | OP_NIL\n"
      "0015    | OP_RETURN\n" },
  { true, "{var a=1;{var a=2;print a;}print a;}",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_CONSTANT         1 '2'\n"
      "0004    | OP_GET_LOCAL        2\n"
      "0006    | OP_PRINT\n"
      "0007    | OP_POP\n"
      "0008    | OP_GET_LOCAL        1\n"
      "0010    | OP_PRINT\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "var a;{var b=a;var c=b;}",
      "== <script> ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0003    | OP_GET_GLOBAL       0 'a'\n"
      "0006    | OP_GET_LOCAL        1\n"
      "0008    | OP_POP\n"
      "0009    | OP_POP\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
};

DUMP_SRC(LocalVars, localVars, 7);

SourceToDump for_[] = {
  { true, "for(;;)0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_POP\n"
      "0003    | OP_LOOP             3 -> 0\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "for(var a=0;;)1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_CONSTANT         1 '1'\n"
      "0004    | OP_POP\n"
      "0005    | OP_LOOP             5 -> 2\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "for(0;;)1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_POP\n"
      "0003    | OP_CONSTANT         1 '1'\n"
      "0005    | OP_POP\n"
      "0006    | OP_LOOP             6 -> 3\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "for(;false;)0;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 10\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0006    | OP_POP\n"
      "0007    | OP_LOOP             7 -> 0\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
  { true, "for(;;0)1;",
      "== <script> ==\n"
      "0000    1 OP_JUMP             0 -> 9\n"
      "0003    | OP_CONSTANT         0 '0'\n"
      "0005    | OP_POP\n"
      "0006    | OP_LOOP             6 -> 0\n"
      "0009    | OP_CONSTANT         1 '1'\n"
      "0011    | OP_POP\n"
      "0012    | OP_LOOP            12 -> 3\n"
      "0015    | OP_NIL\n"
      "0016    | OP_RETURN\n" },
  { true, "for(var i=0;i<5;i=i+1)print i;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_LESS_C           1 '5'\n"
      "0006    | OP_PJMP_IF_FALSE    6 -> 28\n"
      "0009    | OP_JUMP             9 -> 22\n"
      "0012    | OP_GET_LOCAL        1\n"
      "0014    | OP_ADD_C            2 '1'\n"
      "0016    | OP_SET_LOCAL        1\n"
      "0018    | OP_POP\n"
      "0019    | OP_LOOP            19 -> 2\n"
      "0022    | OP_GET_LOCAL        1\n"
      "0024    | OP_PRINT\n"
      "0025    | OP_LOOP            25 -> 12\n"
      "0028    | OP_POP\n"
      "0029    | OP_NIL\n"
      "0030    | OP_RETURN\n" },
};

DUMP_SRC(For, for_, 6);

SourceToDump if_[] = {
  { true, "if(true)0;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 7\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "if(false)0;else 1;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 10\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0006    | OP_POP\n"
      "0007    | OP_JUMP             7 -> 13\n"
      "0010    | OP_CONSTANT         1 '1'\n"
      "0012    | OP_POP\n"
      "0013    | OP_NIL\n"
      "0014    | OP_RETURN\n" },
};

DUMP_SRC(If, if_, 2);

SourceToDump while_[] = {
  { true, "while(false)0;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 10\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0006    | OP_POP\n"
      "0007    | OP_LOOP             7 -> 0\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
};

DUMP_SRC(While, while_, 1);

SourceToDump classes[] = {
  { false, "1+f.x=2;", "Invalid assignment target." },
  { false, "1+f[\"x\"]=2;", "Invalid assignment target." },
  { false, "class F{", "Expect '}' after class body." },
  { true, "class F{}",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0004    | OP_GET_GLOBAL       0 'F'\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "fun g(){class F{}}",
      "== g ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn g>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'g'\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "class F{}var f=F();f.x=1;print f.x;",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0004    | OP_GET_GLOBAL       0 'F'\n"
      "0007    | OP_POP\n"
      "0008    | OP_GET_GLOBAL       0 'F'\n"
      "0011    | OP_CALL             0\n"
      "0013    | OP_DEFINE_GLOBAL    1 'f'\n"
      "0015    | OP_GET_GLOBAL       1 'f'\n"
      "0018    | OP_CONSTANT         3 '1'\n"
      "0020    | OP_SET_PROPERTY     2 'x'\n"
      "0022    | OP_POP\n"
      "0023    | OP_GET_GLOBAL       1 'f'\n"
      "0026    | OP_GET_PROPERTY     2 'x'\n"
      "0028    | OP_PRINT\n"
      "0029    | OP_NIL\n"
      "0030    | OP_RETURN\n" },
  { true, "fun g(){class F{}var f=F();f.x=1;print f.x;}",
      "== g ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_POP\n"
      "0005    | OP_GET_LOCAL        1\n"
      "0007    | OP_CALL             0\n"
      "0009    | OP_GET_LOCAL        2\n"
      "0011    | OP_CONSTANT         2 '1'\n"
      "0013    | OP_SET_PROPERTY     1 'x'\n"
      "0015    | OP_POP\n"
      "0016    | OP_GET_LOCAL        2\n"
      "0018    | OP_GET_PROPERTY     1 'x'\n"
      "0020    | OP_PRINT\n"
      "0021    | OP_NIL\n"
      "0022    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn g>'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'g'\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "class F{}var f=F();f[\"x\"]=1;print f[\"x\"];",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0004    | OP_GET_GLOBAL       0 'F'\n"
      "0007    | OP_POP\n"
      "0008    | OP_GET_GLOBAL       0 'F'\n"
      "0011    | OP_CALL             0\n"
      "0013    | OP_DEFINE_GLOBAL    1 'f'\n"
      "0015    | OP_GET_GLOBAL       1 'f'\n"
      "0018    | OP_CONSTANT         2 'x'\n"
      "0020    | OP_CONSTANT         3 '1'\n"
      "0022    | OP_SET_INDEX\n"
      "0023    | OP_POP\n"
      "0024    | OP_GET_GLOBAL       1 'f'\n"
      "0027    | OP_CONSTANT         2 'x'\n"
      "0029    | OP_GET_INDEX\n"
      "0030    | OP_PRINT\n"
      "0031    | OP_NIL\n"
      "0032    | OP_RETURN\n" },
};

DUMP_SRC(Classes, classes, 8);

SourceToDump methods[] = {
  { false, "class F{0", "Expect method name." },
  { false, "this;", "Can't use 'this' outside of a class." },
  { false, "fun f(){this;}", "Can't use 'this' outside of a class." },
  { false, "class F{init(){return 0;}}",
      "Can't return a value from an initializer." },
  { true, "class F{inin(){return 0;}}",
      "== inin ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0002    | OP_RETURN\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0004    | OP_GET_GLOBAL       0 'F'\n"
      "0007    | OP_CONSTANT         2 '<fn inin>'\n"
      "0009    | OP_METHOD           1 'inin'\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true,
      "class F {"
      "  init(n) { this.n = n; }"
      "  get() { return n; }"
      "  set(nn) { this.n = nn; }"
      "}"
      "var f = F(1); print f.get(); f.set(2); print f.get();"
      "var g = f.get; var s = f.set;"
      "print g(); s(3); print g();",
      "== init ==\n"
      "0000    1 OP_GET_LOCAL        0\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_SET_PROPERTY     0 'n'\n"
      "0006    | OP_POP\n"
      "0007    | OP_GET_LOCAL        0\n"
      "0009    | OP_RETURN\n"
      "== get ==\n"
      "0000    1 OP_GET_GLOBAL       0 'n'\n"
      "0003    | OP_RETURN\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== set ==\n"
      "0000    1 OP_GET_LOCAL        0\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_SET_PROPERTY     0 'n'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0004    | OP_GET_GLOBAL       0 'F'\n"
      "0007    | OP_CONSTANT         2 '<fn init>'\n"
      "0009    | OP_METHOD           1 'init'\n"
      "0011    | OP_CONSTANT         4 '<fn get>'\n"
      "0013    | OP_METHOD           3 'get'\n"
      "0015    | OP_CONSTANT         6 '<fn set>'\n"
      "0017    | OP_METHOD           5 'set'\n"
      "0019    | OP_POP\n"
      "0020    | OP_GET_GLOBAL       0 'F'\n"
      "0023    | OP_CONSTANT         8 '1'\n"
      "0025    | OP_CALL             1\n"
      "0027    | OP_DEFINE_GLOBAL    7 'f'\n"
      "0029    | OP_GET_GLOBAL       7 'f'\n"
      "0032    | OP_INVOKE        (0 args)    3 'get'\n"
      "0035    | OP_PRINT\n"
      "0036    | OP_GET_GLOBAL       7 'f'\n"
      "0039    | OP_CONSTANT         9 '2'\n"
      "0041    | OP_INVOKE        (1 args)    5 'set'\n"
      "0044    | OP_POP\n"
      "0045    | OP_GET_GLOBAL       7 'f'\n"
      "0048    | OP_INVOKE        (0 args)    3 'get'\n"
      "0051    | OP_PRINT\n"
      "0052    | OP_GET_GLOBAL       7 'f'\n"
      "0055    | OP_GET_PROPERTY     3 'get'\n"
      "0057    | OP_DEFINE_GLOBAL   10 'g'\n"
      "0059    | OP_GET_GLOBAL       7 'f'\n"
      "0062    | OP_GET_PROPERTY     5 'set'\n"
      "0064    | OP_DEFINE_GLOBAL   11 's'\n"
      "0066    | OP_GET_GLOBAL      10 'g'\n"
      "0069    | OP_CALL             0\n"
      "0071    | OP_PRINT\n"
      "0072    | OP_GET_GLOBAL      11 's'\n"
      "0075    | OP_CONSTANT        12 '3'\n"
      "0077    | OP_CALL             1\n"
      "0079    | OP_POP\n"
      "0080    | OP_GET_GLOBAL      10 'g'\n"
      "0083    | OP_CALL             0\n"
      "0085    | OP_PRINT\n"
      "0086    | OP_NIL\n"
      "0087    | OP_RETURN\n" },
};

DUMP_SRC(Methods, methods, 6);

SourceToDump superclasses[] = {
  { false, "class A<", "Expect superclass name." },
  { false, "class A<A", "A class can't inherit from itself." },
  { false, "super", "Can't use 'super' outside of a class." },
  { false, "class A{f(){super;}}",
      "Can't use 'super' in a class with no superclass." },
  { true, "class A{f(){}}class B<A{f(){super.f();}}",
      "== f ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_RETURN\n"
      "== f ==\n"
      "0000    1 OP_GET_LOCAL        0\n"
      "0002    | OP_GET_UPVALUE      0\n"
      "0004    | OP_SUPER_INVOKE  (0 args)    0 'f'\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'A'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'A'\n"
      "0004    | OP_GET_GLOBAL       0 'A'\n"
      "0007    | OP_CONSTANT         2 '<fn f>'\n"
      "0009    | OP_METHOD           1 'f'\n"
      "0011    | OP_POP\n"
      "0012    | OP_CLASS            3 'B'\n"
      "0014    | OP_DEFINE_GLOBAL    3 'B'\n"
      "0016    | OP_GET_GLOBAL       0 'A'\n"
      "0019    | OP_GET_GLOBAL       3 'B'\n"
      "0022    | OP_INHERIT\n"
      "0023    | OP_GET_GLOBAL       3 'B'\n"
      "0026    | OP_CLOSURE          4 <fn f>\n"
      "0028      |                     local 1\n"
      "0030    | OP_METHOD           1 'f'\n"
      "0032    | OP_POP\n"
      "0033    | OP_CLOSE_UPVALUE\n"
      "0034    | OP_NIL\n"
      "0035    | OP_RETURN\n" },
  { true, "class A{f(){}}class B<A{f(){var ff=super.f;ff();}}",
      "== f ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_RETURN\n"
      "== f ==\n"
      "0000    1 OP_GET_LOCAL        0\n"
      "0002    | OP_GET_UPVALUE      0\n"
      "0004    | OP_GET_SUPER        0 'f'\n"
      "0006    | OP_GET_LOCAL        1\n"
      "0008    | OP_CALL             0\n"
      "0010    | OP_POP\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'A'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'A'\n"
      "0004    | OP_GET_GLOBAL       0 'A'\n"
      "0007    | OP_CONSTANT         2 '<fn f>'\n"
      "0009    | OP_METHOD           1 'f'\n"
      "0011    | OP_POP\n"
      "0012    | OP_CLASS            3 'B'\n"
      "0014    | OP_DEFINE_GLOBAL    3 'B'\n"
      "0016    | OP_GET_GLOBAL       0 'A'\n"
      "0019    | OP_GET_GLOBAL       3 'B'\n"
      "0022    | OP_INHERIT\n"
      "0023    | OP_GET_GLOBAL       3 'B'\n"
      "0026    | OP_CLOSURE          4 <fn f>\n"
      "0028      |                     local 1\n"
      "0030    | OP_METHOD           1 'f'\n"
      "0032    | OP_POP\n"
      "0033    | OP_CLOSE_UPVALUE\n"
      "0034    | OP_NIL\n"
      "0035    | OP_RETURN\n" },
};

DUMP_SRC(Superclasses, superclasses, 6);

SourceToDump error[] = {
  { false, "print ;", "Expect expression." },
  { false, "print #", "Unexpected character." },
  { false, "print 0", "Expect ';' after value." },
  { false, "print 0 1", "Expect ';' after value." },
  { false, "print 0 1;print 2;", "Expect ';' after value." },
  { false, "print 0 1 print 2;", "Expect ';' after value." },
  { false, "0", "Expect ';' after expression." },
  { false, "{", "Expect '}' after block." },
};

DUMP_SRC(Error, error, 8);

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
