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
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
};

DUMP_SRC(Numbers, numbers, 1);

SourceToDump strings[] = {
  { true, "\"\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 ''\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "\"foo\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
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
      "0003    | OP_SET_GLOBAL       0 'foo'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
};

DUMP_SRC(VarSet, varSet, 2);

SourceToDump unary[] = {
  { true, "-1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "--1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_NEGATE\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
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
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "(-1);",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "-(1);",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_POP\n"
      "0005    | OP_NIL\n"
      "0006    | OP_RETURN\n" },
  { true, "-(-1);",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_NEGATE\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
};

DUMP_SRC(Grouping, grouping, 5);

SourceToDump binaryNums[] = {
  { true, "3 + 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0003    | OP_ADD_C            1 '2'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "3 - 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0003    | OP_SUBTRACT_C       1 '2'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "3 * 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_MULTIPLY\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "3 / 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_DIVIDE\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "3 % 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_MODULO\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "4 + 3 % 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '4'\n"
      "0003    | OP_CONSTANT         1 '3'\n"
      "0006    | OP_CONSTANT         2 '2'\n"
      "0009    | OP_MODULO\n"
      "0010    | OP_ADD\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "4 + 3 - 2 + 1 - 0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '4'\n"
      "0003    | OP_ADD_C            1 '3'\n"
      "0006    | OP_SUBTRACT_C       2 '2'\n"
      "0009    | OP_ADD_C            3 '1'\n"
      "0012    | OP_SUBTRACT_C       4 '0'\n"
      "0015    | OP_POP\n"
      "0016    | OP_NIL\n"
      "0017    | OP_RETURN\n" },
  { true, "4 / 3 * 2 / 1 * 0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '4'\n"
      "0003    | OP_CONSTANT         1 '3'\n"
      "0006    | OP_DIVIDE\n"
      "0007    | OP_CONSTANT         2 '2'\n"
      "0010    | OP_MULTIPLY\n"
      "0011    | OP_CONSTANT         3 '1'\n"
      "0014    | OP_DIVIDE\n"
      "0015    | OP_CONSTANT         4 '0'\n"
      "0018    | OP_MULTIPLY\n"
      "0019    | OP_POP\n"
      "0020    | OP_NIL\n"
      "0021    | OP_RETURN\n" },
  { true, "3 * 2 + 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_MULTIPLY\n"
      "0007    | OP_ADD_C            2 '1'\n"
      "0010    | OP_POP\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
  { true, "3 + 2 * 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '3'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_CONSTANT         2 '1'\n"
      "0009    | OP_MULTIPLY\n"
      "0010    | OP_ADD\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "(-1 + 2) * 3 - -4;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_NEGATE\n"
      "0004    | OP_ADD_C            1 '2'\n"
      "0007    | OP_CONSTANT         2 '3'\n"
      "0010    | OP_MULTIPLY\n"
      "0011    | OP_CONSTANT         3 '4'\n"
      "0014    | OP_NEGATE\n"
      "0015    | OP_SUBTRACT\n"
      "0016    | OP_POP\n"
      "0017    | OP_NIL\n"
      "0018    | OP_RETURN\n" },
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
      "0003    | OP_CONSTANT         1 '1'\n"
      "0006    | OP_GREATER\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "0 >= 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_LESS_C           1 '1'\n"
      "0006    | OP_NOT\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "0 < 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_LESS_C           1 '1'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "0 <= 1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_CONSTANT         1 '1'\n"
      "0006    | OP_GREATER\n"
      "0007    | OP_NOT\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "0 + 1 < 2 == true;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_ADD_C            1 '1'\n"
      "0006    | OP_LESS_C           2 '2'\n"
      "0009    | OP_TRUE\n"
      "0010    | OP_EQUAL\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "true == 0 < 1 + 2;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_CONSTANT         0 '0'\n"
      "0004    | OP_CONSTANT         1 '1'\n"
      "0007    | OP_ADD_C            2 '2'\n"
      "0010    | OP_LESS\n"
      "0011    | OP_EQUAL\n"
      "0012    | OP_POP\n"
      "0013    | OP_NIL\n"
      "0014    | OP_RETURN\n" },
  { true, "0 >= 1 + 2;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_CONSTANT         1 '1'\n"
      "0006    | OP_ADD_C            2 '2'\n"
      "0009    | OP_LESS\n"
      "0010    | OP_NOT\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
};

DUMP_SRC(BinaryCompare, binaryCompare, 9);

SourceToDump addStrings[] = {
  { true, "\"\" + \"\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 ''\n"
      "0003    | OP_ADD_C            0 ''\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "\"foo\" + \"bar\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0003    | OP_ADD_C            1 'bar'\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "\"foo\" + \"bar\" + \"baz\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0003    | OP_ADD_C            1 'bar'\n"
      "0006    | OP_ADD_C            2 'baz'\n"
      "0009    | OP_POP\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
  { true, "\"foo\" + \"bar\" + \"foo\" + \"bar\";",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 'foo'\n"
      "0003    | OP_ADD_C            1 'bar'\n"
      "0006    | OP_ADD_C            0 'foo'\n"
      "0009    | OP_ADD_C            1 'bar'\n"
      "0012    | OP_POP\n"
      "0013    | OP_NIL\n"
      "0014    | OP_RETURN\n" },
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
      "0003    | OP_CONSTANT         1 '1'\n"
      "0006    | OP_EQUAL\n"
      "0007    | OP_JUMP_IF_FALSE    7 -> 14\n"
      "0010    | OP_POP\n"
      "0011    | OP_CONSTANT         2 '2'\n"
      "0014    | OP_JUMP_IF_FALSE   14 -> 20\n"
      "0017    | OP_JUMP            17 -> 24\n"
      "0020    | OP_POP\n"
      "0021    | OP_CONSTANT         3 '3'\n"
      "0024    | OP_POP\n"
      "0025    | OP_NIL\n"
      "0026    | OP_RETURN\n" },
  { true, "0 or 1 and 2 == 3;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_JUMP_IF_FALSE    3 -> 9\n"
      "0006    | OP_JUMP             6 -> 24\n"
      "0009    | OP_POP\n"
      "0010    | OP_CONSTANT         1 '1'\n"
      "0013    | OP_JUMP_IF_FALSE   13 -> 24\n"
      "0016    | OP_POP\n"
      "0017    | OP_CONSTANT         2 '2'\n"
      "0020    | OP_CONSTANT         3 '3'\n"
      "0023    | OP_EQUAL\n"
      "0024    | OP_POP\n"
      "0025    | OP_NIL\n"
      "0026    | OP_RETURN\n" },
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
      "0003    | OP_PRINT\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn a>'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0006    | OP_GET_GLOBAL       0 'a'\n"
      "0009    | OP_CALL             0\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "(fun(){print 1;})();",
      "== () ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_PRINT\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '<fn ()>'\n"
      "0003    | OP_CALL             0\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "fun a(x){print x;}a(1);",
      "== a ==\n"
      "0000    1 OP_GET_LOCAL        1\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn a>'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0006    | OP_GET_GLOBAL       0 'a'\n"
      "0009    | OP_CONSTANT         2 '1'\n"
      "0012    | OP_CALL             1\n"
      "0014    | OP_POP\n"
      "0015    | OP_NIL\n"
      "0016    | OP_RETURN\n" },
  { true, "(fun(x){print x;})(x);",
      "== () ==\n"
      "0000    1 OP_GET_LOCAL        1\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '<fn ()>'\n"
      "0003    | OP_GET_GLOBAL       1 'x'\n"
      "0006    | OP_CALL             1\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "fun a(){return 1;}print a();",
      "== a ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_RETURN\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn a>'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0006    | OP_GET_GLOBAL       0 'a'\n"
      "0009    | OP_CALL             0\n"
      "0011    | OP_PRINT\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "print(fun(){return 1;})();",
      "== () ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_RETURN\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '<fn ()>'\n"
      "0003    | OP_CALL             0\n"
      "0005    | OP_PRINT\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
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
      "0003    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0006    | OP_GET_GLOBAL       0 'a'\n"
      "0009    | OP_CONSTANT         2 '3'\n"
      "0012    | OP_GET_GLOBAL       0 'a'\n"
      "0015    | OP_CONSTANT         3 '2'\n"
      "0018    | OP_CONSTANT         4 '1'\n"
      "0021    | OP_CALL             2\n"
      "0023    | OP_CALL             2\n"
      "0025    | OP_PRINT\n"
      "0026    | OP_NIL\n"
      "0027    | OP_RETURN\n" },
  { true, "var x=1;fun a(){fun b(){print x;}b();}a();",
      "== b ==\n"
      "0000    1 OP_GET_GLOBAL       0 'x'\n"
      "0003    | OP_PRINT\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== a ==\n"
      "0000    1 OP_CONSTANT         0 '<fn b>'\n"
      "0003    | OP_GET_LOCAL        1\n"
      "0005    | OP_CALL             0\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '1'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'x'\n"
      "0006    | OP_CONSTANT         3 '<fn a>'\n"
      "0009    | OP_DEFINE_GLOBAL    2 'a'\n"
      "0012    | OP_GET_GLOBAL       2 'a'\n"
      "0015    | OP_CALL             0\n"
      "0017    | OP_POP\n"
      "0018    | OP_NIL\n"
      "0019    | OP_RETURN\n" },
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
      "0010    | OP_SET_UPVALUE      0\n"
      "0012    | OP_POP\n"
      "0013    | OP_GET_LOCAL        1\n"
      "0015    | OP_RETURN\n"
      "0016    | OP_NIL\n"
      "0017    | OP_RETURN\n"
      "== counter ==\n"
      "0000    1 OP_CLOSURE          0 <fn incAndPrint>\n"
      "0003      |                     local 1\n"
      "0005    | OP_GET_LOCAL        2\n"
      "0007    | OP_RETURN\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn counter>'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'counter'\n"
      "0006    | OP_GET_GLOBAL       0 'counter'\n"
      "0009    | OP_CONSTANT         3 '1'\n"
      "0012    | OP_CALL             1\n"
      "0014    | OP_DEFINE_GLOBAL    2 'c'\n"
      "0017    | OP_GET_GLOBAL       2 'c'\n"
      "0020    | OP_CALL             0\n"
      "0022    | OP_POP\n"
      "0023    | OP_GET_GLOBAL       2 'c'\n"
      "0026    | OP_CALL             0\n"
      "0028    | OP_POP\n"
      "0029    | OP_GET_GLOBAL       2 'c'\n"
      "0032    | OP_CALL             0\n"
      "0034    | OP_POP\n"
      "0035    | OP_NIL\n"
      "0036    | OP_RETURN\n" },
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
      "0003      |                     local 1\n"
      "0005      |                     local 2\n"
      "0007      |                     upvalue 0\n"
      "0009      |                     upvalue 1\n"
      "0011    | OP_GET_LOCAL        3\n"
      "0013    | OP_RETURN\n"
      "0014    | OP_NIL\n"
      "0015    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_CLOSURE          2 <fn f>\n"
      "0009      |                     local 1\n"
      "0011      |                     local 2\n"
      "0013    | OP_POP\n"
      "0014    | OP_CLOSE_UPVALUE\n"
      "0015    | OP_CLOSE_UPVALUE\n"
      "0016    | OP_GET_GLOBAL       3 'f'\n"
      "0019    | OP_CONSTANT         4 '3'\n"
      "0022    | OP_CONSTANT         5 '4'\n"
      "0025    | OP_CALL             2\n"
      "0027    | OP_POP\n"
      "0028    | OP_NIL\n"
      "0029    | OP_RETURN\n" },
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
      "0003      |                     upvalue 0\n"
      "0005      |                     local 1\n"
      "0007    | OP_GET_LOCAL        2\n"
      "0009    | OP_RETURN\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n"
      "== outer ==\n"
      "0000    1 OP_CLOSURE          0 <fn middle>\n"
      "0003      |                     local 1\n"
      "0005    | OP_GET_LOCAL        2\n"
      "0007    | OP_RETURN\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn outer>'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'outer'\n"
      "0006    | OP_GET_GLOBAL       0 'outer'\n"
      "0009    | OP_CONSTANT         2 '1'\n"
      "0012    | OP_CALL             1\n"
      "0014    | OP_CONSTANT         3 '2'\n"
      "0017    | OP_CALL             1\n"
      "0019    | OP_CONSTANT         4 '3'\n"
      "0022    | OP_CALL             1\n"
      "0024    | OP_PRINT\n"
      "0025    | OP_NIL\n"
      "0026    | OP_RETURN\n" },
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
      "0004    | OP_NIL\n"
      "0005    | OP_DEFINE_GLOBAL    1 'g'\n"
      "0008    | OP_NIL\n"
      "0009    | OP_DEFINE_GLOBAL    2 'h'\n"
      "0012    | OP_CONSTANT         3 'x'\n"
      "0015    | OP_CONSTANT         4 'y'\n"
      "0018    | OP_CONSTANT         5 'z'\n"
      "0021    | OP_CLOSURE          6 <fn ff>\n"
      "0024      |                     local 3\n"
      "0026    | OP_GET_LOCAL        4\n"
      "0028    | OP_SET_GLOBAL       0 'f'\n"
      "0031    | OP_POP\n"
      "0032    | OP_CLOSURE          7 <fn gg>\n"
      "0035      |                     local 1\n"
      "0037    | OP_GET_LOCAL        5\n"
      "0039    | OP_SET_GLOBAL       1 'g'\n"
      "0042    | OP_POP\n"
      "0043    | OP_CLOSURE          8 <fn hh>\n"
      "0046      |                     local 2\n"
      "0048    | OP_GET_LOCAL        6\n"
      "0050    | OP_SET_GLOBAL       2 'h'\n"
      "0053    | OP_POP\n"
      "0054    | OP_POP\n"
      "0055    | OP_POP\n"
      "0056    | OP_POP\n"
      "0057    | OP_CLOSE_UPVALUE\n"
      "0058    | OP_CLOSE_UPVALUE\n"
      "0059    | OP_CLOSE_UPVALUE\n"
      "0060    | OP_GET_GLOBAL       0 'f'\n"
      "0063    | OP_CALL             0\n"
      "0065    | OP_POP\n"
      "0066    | OP_GET_GLOBAL       1 'g'\n"
      "0069    | OP_CALL             0\n"
      "0071    | OP_POP\n"
      "0072    | OP_GET_GLOBAL       2 'h'\n"
      "0075    | OP_CALL             0\n"
      "0077    | OP_POP\n"
      "0078    | OP_NIL\n"
      "0079    | OP_RETURN\n" },
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
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "var foo=0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '0'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'foo'\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "{var foo;}",
      "== <script> ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_POP\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n" },
  { true, "{var foo=0;}",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
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
      "0003    | OP_GET_LOCAL        1\n"
      "0005    | OP_PRINT\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "{var a=1;var foo=2;print a+foo;}",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_GET_LOCAL        1\n"
      "0008    | OP_GET_LOCAL        2\n"
      "0010    | OP_ADD\n"
      "0011    | OP_PRINT\n"
      "0012    | OP_POP\n"
      "0013    | OP_POP\n"
      "0014    | OP_NIL\n"
      "0015    | OP_RETURN\n" },
  { true, "var a=1;{var a=2;print a;}print a;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '1'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0006    | OP_CONSTANT         2 '2'\n"
      "0009    | OP_GET_LOCAL        1\n"
      "0011    | OP_PRINT\n"
      "0012    | OP_POP\n"
      "0013    | OP_GET_GLOBAL       0 'a'\n"
      "0016    | OP_PRINT\n"
      "0017    | OP_NIL\n"
      "0018    | OP_RETURN\n" },
  { true, "{var a=1;{var a=2;print a;}print a;}",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0003    | OP_CONSTANT         1 '2'\n"
      "0006    | OP_GET_LOCAL        2\n"
      "0008    | OP_PRINT\n"
      "0009    | OP_POP\n"
      "0010    | OP_GET_LOCAL        1\n"
      "0012    | OP_PRINT\n"
      "0013    | OP_POP\n"
      "0014    | OP_NIL\n"
      "0015    | OP_RETURN\n" },
  { true, "var a;{var b=a;var c=b;}",
      "== <script> ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       0 'a'\n"
      "0007    | OP_GET_LOCAL        1\n"
      "0009    | OP_POP\n"
      "0010    | OP_POP\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
};

DUMP_SRC(LocalVars, localVars, 7);

SourceToDump for_[] = {
  { true, "for(;;)0;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_POP\n"
      "0004    | OP_LOOP             4 -> 0\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n" },
  { true, "for(var a=0;;)1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_CONSTANT         1 '1'\n"
      "0006    | OP_POP\n"
      "0007    | OP_LOOP             7 -> 3\n"
      "0010    | OP_POP\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
  { true, "for(0;;)1;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_POP\n"
      "0004    | OP_CONSTANT         1 '1'\n"
      "0007    | OP_POP\n"
      "0008    | OP_LOOP             8 -> 4\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
  { true, "for(;false;)0;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 11\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_LOOP             8 -> 0\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
  { true, "for(;;0)1;",
      "== <script> ==\n"
      "0000    1 OP_JUMP             0 -> 10\n"
      "0003    | OP_CONSTANT         0 '0'\n"
      "0006    | OP_POP\n"
      "0007    | OP_LOOP             7 -> 0\n"
      "0010    | OP_CONSTANT         1 '1'\n"
      "0013    | OP_POP\n"
      "0014    | OP_LOOP            14 -> 3\n"
      "0017    | OP_NIL\n"
      "0018    | OP_RETURN\n" },
  { true, "for(var i=0;i<5;i=i+1)print i;",
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         0 '0'\n"
      "0003    | OP_GET_LOCAL        1\n"
      "0005    | OP_LESS_C           1 '5'\n"
      "0008    | OP_PJMP_IF_FALSE    8 -> 31\n"
      "0011    | OP_JUMP            11 -> 25\n"
      "0014    | OP_GET_LOCAL        1\n"
      "0016    | OP_ADD_C            2 '1'\n"
      "0019    | OP_SET_LOCAL        1\n"
      "0021    | OP_POP\n"
      "0022    | OP_LOOP            22 -> 3\n"
      "0025    | OP_GET_LOCAL        1\n"
      "0027    | OP_PRINT\n"
      "0028    | OP_LOOP            28 -> 14\n"
      "0031    | OP_POP\n"
      "0032    | OP_NIL\n"
      "0033    | OP_RETURN\n" },
};

DUMP_SRC(For, for_, 6);

SourceToDump if_[] = {
  { true, "if(true)0;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 8\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "if(false)0;else 1;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 11\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_JUMP             8 -> 15\n"
      "0011    | OP_CONSTANT         1 '1'\n"
      "0014    | OP_POP\n"
      "0015    | OP_NIL\n"
      "0016    | OP_RETURN\n" },
};

DUMP_SRC(If, if_, 2);

SourceToDump while_[] = {
  { true, "while(false)0;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_PJMP_IF_FALSE    1 -> 11\n"
      "0004    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_LOOP             8 -> 0\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
};

DUMP_SRC(While, while_, 1);

SourceToDump classes[] = {
  { false, "1+f.x=2;", "Invalid assignment target." },
  { false, "1+f[\"x\"]=2;", "Invalid assignment target." },
  { false, "class F{", "Expect '}' after class body." },
  { true, "class F{}",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0006    | OP_GET_GLOBAL       0 'F'\n"
      "0009    | OP_POP\n"
      "0010    | OP_NIL\n"
      "0011    | OP_RETURN\n" },
  { true, "fun g(){class F{}}",
      "== g ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0003    | OP_GET_LOCAL        1\n"
      "0005    | OP_POP\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn g>'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'g'\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "class F{}var f=F();f.x=1;print f.x;",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0006    | OP_GET_GLOBAL       0 'F'\n"
      "0009    | OP_POP\n"
      "0010    | OP_GET_GLOBAL       0 'F'\n"
      "0013    | OP_CALL             0\n"
      "0015    | OP_DEFINE_GLOBAL    1 'f'\n"
      "0018    | OP_GET_GLOBAL       1 'f'\n"
      "0021    | OP_CONSTANT         3 '1'\n"
      "0024    | OP_SET_PROPERTY     2 'x'\n"
      "0027    | OP_POP\n"
      "0028    | OP_GET_GLOBAL       1 'f'\n"
      "0031    | OP_GET_PROPERTY     2 'x'\n"
      "0034    | OP_PRINT\n"
      "0035    | OP_NIL\n"
      "0036    | OP_RETURN\n" },
  { true, "fun g(){class F{}var f=F();f.x=1;print f.x;}",
      "== g ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0003    | OP_GET_LOCAL        1\n"
      "0005    | OP_POP\n"
      "0006    | OP_GET_LOCAL        1\n"
      "0008    | OP_CALL             0\n"
      "0010    | OP_GET_LOCAL        2\n"
      "0012    | OP_CONSTANT         2 '1'\n"
      "0015    | OP_SET_PROPERTY     1 'x'\n"
      "0018    | OP_POP\n"
      "0019    | OP_GET_LOCAL        2\n"
      "0021    | OP_GET_PROPERTY     1 'x'\n"
      "0024    | OP_PRINT\n"
      "0025    | OP_NIL\n"
      "0026    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '<fn g>'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'g'\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n" },
  { true, "class F{}var f=F();f[\"x\"]=1;print f[\"x\"];",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0006    | OP_GET_GLOBAL       0 'F'\n"
      "0009    | OP_POP\n"
      "0010    | OP_GET_GLOBAL       0 'F'\n"
      "0013    | OP_CALL             0\n"
      "0015    | OP_DEFINE_GLOBAL    1 'f'\n"
      "0018    | OP_GET_GLOBAL       1 'f'\n"
      "0021    | OP_CONSTANT         2 'x'\n"
      "0024    | OP_CONSTANT         3 '1'\n"
      "0027    | OP_SET_INDEX\n"
      "0028    | OP_POP\n"
      "0029    | OP_GET_GLOBAL       1 'f'\n"
      "0032    | OP_CONSTANT         2 'x'\n"
      "0035    | OP_GET_INDEX\n"
      "0036    | OP_PRINT\n"
      "0037    | OP_NIL\n"
      "0038    | OP_RETURN\n" },
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
      "0003    | OP_RETURN\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0006    | OP_GET_GLOBAL       0 'F'\n"
      "0009    | OP_CONSTANT         2 '<fn inin>'\n"
      "0012    | OP_METHOD           1 'inin'\n"
      "0015    | OP_POP\n"
      "0016    | OP_NIL\n"
      "0017    | OP_RETURN\n" },
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
      "0007    | OP_POP\n"
      "0008    | OP_GET_LOCAL        0\n"
      "0010    | OP_RETURN\n"
      "== get ==\n"
      "0000    1 OP_GET_GLOBAL       0 'n'\n"
      "0003    | OP_RETURN\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n"
      "== set ==\n"
      "0000    1 OP_GET_LOCAL        0\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_SET_PROPERTY     0 'n'\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0006    | OP_GET_GLOBAL       0 'F'\n"
      "0009    | OP_CONSTANT         2 '<fn init>'\n"
      "0012    | OP_METHOD           1 'init'\n"
      "0015    | OP_CONSTANT         4 '<fn get>'\n"
      "0018    | OP_METHOD           3 'get'\n"
      "0021    | OP_CONSTANT         6 '<fn set>'\n"
      "0024    | OP_METHOD           5 'set'\n"
      "0027    | OP_POP\n"
      "0028    | OP_GET_GLOBAL       0 'F'\n"
      "0031    | OP_CONSTANT         8 '1'\n"
      "0034    | OP_CALL             1\n"
      "0036    | OP_DEFINE_GLOBAL    7 'f'\n"
      "0039    | OP_GET_GLOBAL       7 'f'\n"
      "0042    | OP_INVOKE        (0 args)    3 'get'\n"
      "0046    | OP_PRINT\n"
      "0047    | OP_GET_GLOBAL       7 'f'\n"
      "0050    | OP_CONSTANT         9 '2'\n"
      "0053    | OP_INVOKE        (1 args)    5 'set'\n"
      "0057    | OP_POP\n"
      "0058    | OP_GET_GLOBAL       7 'f'\n"
      "0061    | OP_INVOKE        (0 args)    3 'get'\n"
      "0065    | OP_PRINT\n"
      "0066    | OP_GET_GLOBAL       7 'f'\n"
      "0069    | OP_GET_PROPERTY     3 'get'\n"
      "0072    | OP_DEFINE_GLOBAL   10 'g'\n"
      "0075    | OP_GET_GLOBAL       7 'f'\n"
      "0078    | OP_GET_PROPERTY     5 'set'\n"
      "0081    | OP_DEFINE_GLOBAL   11 's'\n"
      "0084    | OP_GET_GLOBAL      10 'g'\n"
      "0087    | OP_CALL             0\n"
      "0089    | OP_PRINT\n"
      "0090    | OP_GET_GLOBAL      11 's'\n"
      "0093    | OP_CONSTANT        12 '3'\n"
      "0096    | OP_CALL             1\n"
      "0098    | OP_POP\n"
      "0099    | OP_GET_GLOBAL      10 'g'\n"
      "0102    | OP_CALL             0\n"
      "0104    | OP_PRINT\n"
      "0105    | OP_NIL\n"
      "0106    | OP_RETURN\n" },
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
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'A'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'A'\n"
      "0006    | OP_GET_GLOBAL       0 'A'\n"
      "0009    | OP_CONSTANT         2 '<fn f>'\n"
      "0012    | OP_METHOD           1 'f'\n"
      "0015    | OP_POP\n"
      "0016    | OP_CLASS            3 'B'\n"
      "0019    | OP_DEFINE_GLOBAL    3 'B'\n"
      "0022    | OP_GET_GLOBAL       0 'A'\n"
      "0025    | OP_GET_GLOBAL       3 'B'\n"
      "0028    | OP_INHERIT\n"
      "0029    | OP_GET_GLOBAL       3 'B'\n"
      "0032    | OP_CLOSURE          4 <fn f>\n"
      "0035      |                     local 1\n"
      "0037    | OP_METHOD           1 'f'\n"
      "0040    | OP_POP\n"
      "0041    | OP_CLOSE_UPVALUE\n"
      "0042    | OP_NIL\n"
      "0043    | OP_RETURN\n" },
  { true, "class A{f(){}}class B<A{f(){var ff=super.f;ff();}}",
      "== f ==\n"
      "0000    1 OP_NIL\n"
      "0001    | OP_RETURN\n"
      "== f ==\n"
      "0000    1 OP_GET_LOCAL        0\n"
      "0002    | OP_GET_UPVALUE      0\n"
      "0004    | OP_GET_SUPER        0 'f'\n"
      "0007    | OP_GET_LOCAL        1\n"
      "0009    | OP_CALL             0\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'A'\n"
      "0003    | OP_DEFINE_GLOBAL    0 'A'\n"
      "0006    | OP_GET_GLOBAL       0 'A'\n"
      "0009    | OP_CONSTANT         2 '<fn f>'\n"
      "0012    | OP_METHOD           1 'f'\n"
      "0015    | OP_POP\n"
      "0016    | OP_CLASS            3 'B'\n"
      "0019    | OP_DEFINE_GLOBAL    3 'B'\n"
      "0022    | OP_GET_GLOBAL       0 'A'\n"
      "0025    | OP_GET_GLOBAL       3 'B'\n"
      "0028    | OP_INHERIT\n"
      "0029    | OP_GET_GLOBAL       3 'B'\n"
      "0032    | OP_CLOSURE          4 <fn f>\n"
      "0035      |                     local 1\n"
      "0037    | OP_METHOD           1 'f'\n"
      "0040    | OP_POP\n"
      "0041    | OP_CLOSE_UPVALUE\n"
      "0042    | OP_NIL\n"
      "0043    | OP_RETURN\n" },
};

DUMP_SRC(Superclasses, superclasses, 6);

SourceToDump lists[] = {
  { false, "[", "Expect expression." },
  { false, "[,", "Expect expression." },
  { false, "[0", "Expect ']' after list." },
  { false, "[0,", "Expect expression." },
  { true, "[];",
      "== <script> ==\n"
      "0000    1 OP_LIST_INIT\n"
      "0001    | OP_POP\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n" },
  { true, "[nil];",
      "== <script> ==\n"
      "0000    1 OP_LIST_INIT\n"
      "0001    | OP_NIL\n"
      "0002    | OP_LIST_DATA\n"
      "0003    | OP_POP\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "[nil,false,true,];",
      "== <script> ==\n"
      "0000    1 OP_LIST_INIT\n"
      "0001    | OP_NIL\n"
      "0002    | OP_LIST_DATA\n"
      "0003    | OP_FALSE\n"
      "0004    | OP_LIST_DATA\n"
      "0005    | OP_TRUE\n"
      "0006    | OP_LIST_DATA\n"
      "0007    | OP_POP\n"
      "0008    | OP_NIL\n"
      "0009    | OP_RETURN\n" },
  { true, "[nil,true or false,3*2+1,\"hi\",[]];",
      "== <script> ==\n"
      "0000    1 OP_LIST_INIT\n"
      "0001    | OP_NIL\n"
      "0002    | OP_LIST_DATA\n"
      "0003    | OP_TRUE\n"
      "0004    | OP_JUMP_IF_FALSE    4 -> 10\n"
      "0007    | OP_JUMP             7 -> 12\n"
      "0010    | OP_POP\n"
      "0011    | OP_FALSE\n"
      "0012    | OP_LIST_DATA\n"
      "0013    | OP_CONSTANT         0 '3'\n"
      "0016    | OP_CONSTANT         1 '2'\n"
      "0019    | OP_MULTIPLY\n"
      "0020    | OP_ADD_C            2 '1'\n"
      "0023    | OP_LIST_DATA\n"
      "0024    | OP_CONSTANT         3 'hi'\n"
      "0027    | OP_LIST_DATA\n"
      "0028    | OP_LIST_INIT\n"
      "0029    | OP_LIST_DATA\n"
      "0030    | OP_POP\n"
      "0031    | OP_NIL\n"
      "0032    | OP_RETURN\n" },
};

DUMP_SRC(Lists, lists, 8);

SourceToDump maps[] = {
  { false, "({)", "Expect identifier or '['." },
  { false, "({,)", "Expect identifier or '['." },
  { false, "({a)", "Expect ':' after map key." },
  { false, "({a:)", "Expect expression." },
  { false, "({a:,)", "Expect expression." },
  { false, "({a:1)", "Expect '}' after map." },
  { false, "({a:1,)", "Expect identifier or '['." },
  { false, "({[)", "Expect expression." },
  { false, "({[])", "Expect expression." },
  { false, "({[\"a\")", "Expect ']' after expression." },
  { false, "({[\"a\"])", "Expect ':' after map key." },
  { true, "({});",
      "== <script> ==\n"
      "0000    1 OP_MAP_INIT\n"
      "0001    | OP_POP\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n" },
  { true, "({a:1});",
      "== <script> ==\n"
      "0000    1 OP_MAP_INIT\n"
      "0001    | OP_CONSTANT         0 'a'\n"
      "0004    | OP_CONSTANT         1 '1'\n"
      "0007    | OP_MAP_DATA\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "({a:1,});",
      "== <script> ==\n"
      "0000    1 OP_MAP_INIT\n"
      "0001    | OP_CONSTANT         0 'a'\n"
      "0004    | OP_CONSTANT         1 '1'\n"
      "0007    | OP_MAP_DATA\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "({[\"a\"+\"b\"]:1+2});",
      "== <script> ==\n"
      "0000    1 OP_MAP_INIT\n"
      "0001    | OP_CONSTANT         0 'a'\n"
      "0004    | OP_ADD_C            1 'b'\n"
      "0007    | OP_CONSTANT         2 '1'\n"
      "0010    | OP_ADD_C            3 '2'\n"
      "0013    | OP_MAP_DATA\n"
      "0014    | OP_POP\n"
      "0015    | OP_NIL\n"
      "0016    | OP_RETURN\n" },
  { true, "({a:1,b:2});",
      "== <script> ==\n"
      "0000    1 OP_MAP_INIT\n"
      "0001    | OP_CONSTANT         0 'a'\n"
      "0004    | OP_CONSTANT         1 '1'\n"
      "0007    | OP_MAP_DATA\n"
      "0008    | OP_CONSTANT         2 'b'\n"
      "0011    | OP_CONSTANT         3 '2'\n"
      "0014    | OP_MAP_DATA\n"
      "0015    | OP_POP\n"
      "0016    | OP_NIL\n"
      "0017    | OP_RETURN\n" },
  { true, "({a:1,b:{c:2}});",
      "== <script> ==\n"
      "0000    1 OP_MAP_INIT\n"
      "0001    | OP_CONSTANT         0 'a'\n"
      "0004    | OP_CONSTANT         1 '1'\n"
      "0007    | OP_MAP_DATA\n"
      "0008    | OP_CONSTANT         2 'b'\n"
      "0011    | OP_MAP_INIT\n"
      "0012    | OP_CONSTANT         3 'c'\n"
      "0015    | OP_CONSTANT         4 '2'\n"
      "0018    | OP_MAP_DATA\n"
      "0019    | OP_MAP_DATA\n"
      "0020    | OP_POP\n"
      "0021    | OP_NIL\n"
      "0022    | OP_RETURN\n" },
};

DUMP_SRC(Maps, maps, 17);

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

SourceToDump constants[] = {
  { true,
      "print\n"
      "  0 +   1 +   2 +   3 +   4 +   5 +   6 +   7 +   8 +   9 +\n"
      " 10 +  11 +  12 +  13 +  14 +  15 +  16 +  17 +  18 +  19 +\n"
      " 20 +  21 +  22 +  23 +  24 +  25 +  26 +  27 +  28 +  29 +\n"
      " 30 +  31 +  32 +  33 +  34 +  35 +  36 +  37 +  38 +  39 +\n"
      " 40 +  41 +  42 +  43 +  44 +  45 +  46 +  47 +  48 +  49 +\n"
      " 50 +  51 +  52 +  53 +  54 +  55 +  56 +  57 +  58 +  59 +\n"
      " 60 +  61 +  62 +  63 +  64 +  65 +  66 +  67 +  68 +  69 +\n"
      " 70 +  71 +  72 +  73 +  74 +  75 +  76 +  77 +  78 +  79 +\n"
      " 80 +  81 +  82 +  83 +  84 +  85 +  86 +  87 +  88 +  89 +\n"
      " 90 +  91 +  92 +  93 +  94 +  95 +  96 +  97 +  98 +  99 +\n"
      "100 + 101 + 102 + 103 + 104 + 105 + 106 + 107 + 108 + 109 +\n"
      "110 + 111 + 112 + 113 + 114 + 115 + 116 + 117 + 118 + 119 +\n"
      "120 + 121 + 122 + 123 + 124 + 125 + 126 + 127 + 128 + 129 +\n"
      "130 + 131 + 132 + 133 + 134 + 135 + 136 + 137 + 138 + 139 +\n"
      "140 + 141 + 142 + 143 + 144 + 145 + 146 + 147 + 148 + 149 +\n"
      "150 + 151 + 152 + 153 + 154 + 155 + 156 + 157 + 158 + 159 +\n"
      "160 + 161 + 162 + 163 + 164 + 165 + 166 + 167 + 168 + 169 +\n"
      "170 + 171 + 172 + 173 + 174 + 175 + 176 + 177 + 178 + 179 +\n"
      "180 + 181 + 182 + 183 + 184 + 185 + 186 + 187 + 188 + 189 +\n"
      "190 + 191 + 192 + 193 + 194 + 195 + 196 + 197 + 198 + 199 +\n"
      "200 + 201 + 202 + 203 + 204 + 205 + 206 + 207 + 208 + 209 +\n"
      "210 + 211 + 212 + 213 + 214 + 215 + 216 + 217 + 218 + 219 +\n"
      "220 + 221 + 222 + 223 + 224 + 225 + 226 + 227 + 228 + 229 +\n"
      "230 + 231 + 232 + 233 + 234 + 235 + 236 + 237 + 238 + 239 +\n"
      "240 + 241 + 242 + 243 + 244 + 245 + 246 + 247 + 248 + 249 +\n"
      "250 + 251 + 252 + 253 + 254 + 255 + 256 + 257 + 258 + 259 ;\n",
      "== <script> ==\n"
      "0000    2 OP_CONSTANT         0 '0'\n"
      "0003    | OP_ADD_C            1 '1'\n"
      "0006    | OP_ADD_C            2 '2'\n"
      "0009    | OP_ADD_C            3 '3'\n"
      "0012    | OP_ADD_C            4 '4'\n"
      "0015    | OP_ADD_C            5 '5'\n"
      "0018    | OP_ADD_C            6 '6'\n"
      "0021    | OP_ADD_C            7 '7'\n"
      "0024    | OP_ADD_C            8 '8'\n"
      "0027    | OP_ADD_C            9 '9'\n"
      "0030    3 OP_ADD_C           10 '10'\n"
      "0033    | OP_ADD_C           11 '11'\n"
      "0036    | OP_ADD_C           12 '12'\n"
      "0039    | OP_ADD_C           13 '13'\n"
      "0042    | OP_ADD_C           14 '14'\n"
      "0045    | OP_ADD_C           15 '15'\n"
      "0048    | OP_ADD_C           16 '16'\n"
      "0051    | OP_ADD_C           17 '17'\n"
      "0054    | OP_ADD_C           18 '18'\n"
      "0057    | OP_ADD_C           19 '19'\n"
      "0060    4 OP_ADD_C           20 '20'\n"
      "0063    | OP_ADD_C           21 '21'\n"
      "0066    | OP_ADD_C           22 '22'\n"
      "0069    | OP_ADD_C           23 '23'\n"
      "0072    | OP_ADD_C           24 '24'\n"
      "0075    | OP_ADD_C           25 '25'\n"
      "0078    | OP_ADD_C           26 '26'\n"
      "0081    | OP_ADD_C           27 '27'\n"
      "0084    | OP_ADD_C           28 '28'\n"
      "0087    | OP_ADD_C           29 '29'\n"
      "0090    5 OP_ADD_C           30 '30'\n"
      "0093    | OP_ADD_C           31 '31'\n"
      "0096    | OP_ADD_C           32 '32'\n"
      "0099    | OP_ADD_C           33 '33'\n"
      "0102    | OP_ADD_C           34 '34'\n"
      "0105    | OP_ADD_C           35 '35'\n"
      "0108    | OP_ADD_C           36 '36'\n"
      "0111    | OP_ADD_C           37 '37'\n"
      "0114    | OP_ADD_C           38 '38'\n"
      "0117    | OP_ADD_C           39 '39'\n"
      "0120    6 OP_ADD_C           40 '40'\n"
      "0123    | OP_ADD_C           41 '41'\n"
      "0126    | OP_ADD_C           42 '42'\n"
      "0129    | OP_ADD_C           43 '43'\n"
      "0132    | OP_ADD_C           44 '44'\n"
      "0135    | OP_ADD_C           45 '45'\n"
      "0138    | OP_ADD_C           46 '46'\n"
      "0141    | OP_ADD_C           47 '47'\n"
      "0144    | OP_ADD_C           48 '48'\n"
      "0147    | OP_ADD_C           49 '49'\n"
      "0150    7 OP_ADD_C           50 '50'\n"
      "0153    | OP_ADD_C           51 '51'\n"
      "0156    | OP_ADD_C           52 '52'\n"
      "0159    | OP_ADD_C           53 '53'\n"
      "0162    | OP_ADD_C           54 '54'\n"
      "0165    | OP_ADD_C           55 '55'\n"
      "0168    | OP_ADD_C           56 '56'\n"
      "0171    | OP_ADD_C           57 '57'\n"
      "0174    | OP_ADD_C           58 '58'\n"
      "0177    | OP_ADD_C           59 '59'\n"
      "0180    8 OP_ADD_C           60 '60'\n"
      "0183    | OP_ADD_C           61 '61'\n"
      "0186    | OP_ADD_C           62 '62'\n"
      "0189    | OP_ADD_C           63 '63'\n"
      "0192    | OP_ADD_C           64 '64'\n"
      "0195    | OP_ADD_C           65 '65'\n"
      "0198    | OP_ADD_C           66 '66'\n"
      "0201    | OP_ADD_C           67 '67'\n"
      "0204    | OP_ADD_C           68 '68'\n"
      "0207    | OP_ADD_C           69 '69'\n"
      "0210    9 OP_ADD_C           70 '70'\n"
      "0213    | OP_ADD_C           71 '71'\n"
      "0216    | OP_ADD_C           72 '72'\n"
      "0219    | OP_ADD_C           73 '73'\n"
      "0222    | OP_ADD_C           74 '74'\n"
      "0225    | OP_ADD_C           75 '75'\n"
      "0228    | OP_ADD_C           76 '76'\n"
      "0231    | OP_ADD_C           77 '77'\n"
      "0234    | OP_ADD_C           78 '78'\n"
      "0237    | OP_ADD_C           79 '79'\n"
      "0240   10 OP_ADD_C           80 '80'\n"
      "0243    | OP_ADD_C           81 '81'\n"
      "0246    | OP_ADD_C           82 '82'\n"
      "0249    | OP_ADD_C           83 '83'\n"
      "0252    | OP_ADD_C           84 '84'\n"
      "0255    | OP_ADD_C           85 '85'\n"
      "0258    | OP_ADD_C           86 '86'\n"
      "0261    | OP_ADD_C           87 '87'\n"
      "0264    | OP_ADD_C           88 '88'\n"
      "0267    | OP_ADD_C           89 '89'\n"
      "0270   11 OP_ADD_C           90 '90'\n"
      "0273    | OP_ADD_C           91 '91'\n"
      "0276    | OP_ADD_C           92 '92'\n"
      "0279    | OP_ADD_C           93 '93'\n"
      "0282    | OP_ADD_C           94 '94'\n"
      "0285    | OP_ADD_C           95 '95'\n"
      "0288    | OP_ADD_C           96 '96'\n"
      "0291    | OP_ADD_C           97 '97'\n"
      "0294    | OP_ADD_C           98 '98'\n"
      "0297    | OP_ADD_C           99 '99'\n"
      "0300   12 OP_ADD_C          100 '100'\n"
      "0303    | OP_ADD_C          101 '101'\n"
      "0306    | OP_ADD_C          102 '102'\n"
      "0309    | OP_ADD_C          103 '103'\n"
      "0312    | OP_ADD_C          104 '104'\n"
      "0315    | OP_ADD_C          105 '105'\n"
      "0318    | OP_ADD_C          106 '106'\n"
      "0321    | OP_ADD_C          107 '107'\n"
      "0324    | OP_ADD_C          108 '108'\n"
      "0327    | OP_ADD_C          109 '109'\n"
      "0330   13 OP_ADD_C          110 '110'\n"
      "0333    | OP_ADD_C          111 '111'\n"
      "0336    | OP_ADD_C          112 '112'\n"
      "0339    | OP_ADD_C          113 '113'\n"
      "0342    | OP_ADD_C          114 '114'\n"
      "0345    | OP_ADD_C          115 '115'\n"
      "0348    | OP_ADD_C          116 '116'\n"
      "0351    | OP_ADD_C          117 '117'\n"
      "0354    | OP_ADD_C          118 '118'\n"
      "0357    | OP_ADD_C          119 '119'\n"
      "0360   14 OP_ADD_C          120 '120'\n"
      "0363    | OP_ADD_C          121 '121'\n"
      "0366    | OP_ADD_C          122 '122'\n"
      "0369    | OP_ADD_C          123 '123'\n"
      "0372    | OP_ADD_C          124 '124'\n"
      "0375    | OP_ADD_C          125 '125'\n"
      "0378    | OP_ADD_C          126 '126'\n"
      "0381    | OP_ADD_C          127 '127'\n"
      "0384    | OP_ADD_C          128 '128'\n"
      "0387    | OP_ADD_C          129 '129'\n"
      "0390   15 OP_ADD_C          130 '130'\n"
      "0393    | OP_ADD_C          131 '131'\n"
      "0396    | OP_ADD_C          132 '132'\n"
      "0399    | OP_ADD_C          133 '133'\n"
      "0402    | OP_ADD_C          134 '134'\n"
      "0405    | OP_ADD_C          135 '135'\n"
      "0408    | OP_ADD_C          136 '136'\n"
      "0411    | OP_ADD_C          137 '137'\n"
      "0414    | OP_ADD_C          138 '138'\n"
      "0417    | OP_ADD_C          139 '139'\n"
      "0420   16 OP_ADD_C          140 '140'\n"
      "0423    | OP_ADD_C          141 '141'\n"
      "0426    | OP_ADD_C          142 '142'\n"
      "0429    | OP_ADD_C          143 '143'\n"
      "0432    | OP_ADD_C          144 '144'\n"
      "0435    | OP_ADD_C          145 '145'\n"
      "0438    | OP_ADD_C          146 '146'\n"
      "0441    | OP_ADD_C          147 '147'\n"
      "0444    | OP_ADD_C          148 '148'\n"
      "0447    | OP_ADD_C          149 '149'\n"
      "0450   17 OP_ADD_C          150 '150'\n"
      "0453    | OP_ADD_C          151 '151'\n"
      "0456    | OP_ADD_C          152 '152'\n"
      "0459    | OP_ADD_C          153 '153'\n"
      "0462    | OP_ADD_C          154 '154'\n"
      "0465    | OP_ADD_C          155 '155'\n"
      "0468    | OP_ADD_C          156 '156'\n"
      "0471    | OP_ADD_C          157 '157'\n"
      "0474    | OP_ADD_C          158 '158'\n"
      "0477    | OP_ADD_C          159 '159'\n"
      "0480   18 OP_ADD_C          160 '160'\n"
      "0483    | OP_ADD_C          161 '161'\n"
      "0486    | OP_ADD_C          162 '162'\n"
      "0489    | OP_ADD_C          163 '163'\n"
      "0492    | OP_ADD_C          164 '164'\n"
      "0495    | OP_ADD_C          165 '165'\n"
      "0498    | OP_ADD_C          166 '166'\n"
      "0501    | OP_ADD_C          167 '167'\n"
      "0504    | OP_ADD_C          168 '168'\n"
      "0507    | OP_ADD_C          169 '169'\n"
      "0510   19 OP_ADD_C          170 '170'\n"
      "0513    | OP_ADD_C          171 '171'\n"
      "0516    | OP_ADD_C          172 '172'\n"
      "0519    | OP_ADD_C          173 '173'\n"
      "0522    | OP_ADD_C          174 '174'\n"
      "0525    | OP_ADD_C          175 '175'\n"
      "0528    | OP_ADD_C          176 '176'\n"
      "0531    | OP_ADD_C          177 '177'\n"
      "0534    | OP_ADD_C          178 '178'\n"
      "0537    | OP_ADD_C          179 '179'\n"
      "0540   20 OP_ADD_C          180 '180'\n"
      "0543    | OP_ADD_C          181 '181'\n"
      "0546    | OP_ADD_C          182 '182'\n"
      "0549    | OP_ADD_C          183 '183'\n"
      "0552    | OP_ADD_C          184 '184'\n"
      "0555    | OP_ADD_C          185 '185'\n"
      "0558    | OP_ADD_C          186 '186'\n"
      "0561    | OP_ADD_C          187 '187'\n"
      "0564    | OP_ADD_C          188 '188'\n"
      "0567    | OP_ADD_C          189 '189'\n"
      "0570   21 OP_ADD_C          190 '190'\n"
      "0573    | OP_ADD_C          191 '191'\n"
      "0576    | OP_ADD_C          192 '192'\n"
      "0579    | OP_ADD_C          193 '193'\n"
      "0582    | OP_ADD_C          194 '194'\n"
      "0585    | OP_ADD_C          195 '195'\n"
      "0588    | OP_ADD_C          196 '196'\n"
      "0591    | OP_ADD_C          197 '197'\n"
      "0594    | OP_ADD_C          198 '198'\n"
      "0597    | OP_ADD_C          199 '199'\n"
      "0600   22 OP_ADD_C          200 '200'\n"
      "0603    | OP_ADD_C          201 '201'\n"
      "0606    | OP_ADD_C          202 '202'\n"
      "0609    | OP_ADD_C          203 '203'\n"
      "0612    | OP_ADD_C          204 '204'\n"
      "0615    | OP_ADD_C          205 '205'\n"
      "0618    | OP_ADD_C          206 '206'\n"
      "0621    | OP_ADD_C          207 '207'\n"
      "0624    | OP_ADD_C          208 '208'\n"
      "0627    | OP_ADD_C          209 '209'\n"
      "0630   23 OP_ADD_C          210 '210'\n"
      "0633    | OP_ADD_C          211 '211'\n"
      "0636    | OP_ADD_C          212 '212'\n"
      "0639    | OP_ADD_C          213 '213'\n"
      "0642    | OP_ADD_C          214 '214'\n"
      "0645    | OP_ADD_C          215 '215'\n"
      "0648    | OP_ADD_C          216 '216'\n"
      "0651    | OP_ADD_C          217 '217'\n"
      "0654    | OP_ADD_C          218 '218'\n"
      "0657    | OP_ADD_C          219 '219'\n"
      "0660   24 OP_ADD_C          220 '220'\n"
      "0663    | OP_ADD_C          221 '221'\n"
      "0666    | OP_ADD_C          222 '222'\n"
      "0669    | OP_ADD_C          223 '223'\n"
      "0672    | OP_ADD_C          224 '224'\n"
      "0675    | OP_ADD_C          225 '225'\n"
      "0678    | OP_ADD_C          226 '226'\n"
      "0681    | OP_ADD_C          227 '227'\n"
      "0684    | OP_ADD_C          228 '228'\n"
      "0687    | OP_ADD_C          229 '229'\n"
      "0690   25 OP_ADD_C          230 '230'\n"
      "0693    | OP_ADD_C          231 '231'\n"
      "0696    | OP_ADD_C          232 '232'\n"
      "0699    | OP_ADD_C          233 '233'\n"
      "0702    | OP_ADD_C          234 '234'\n"
      "0705    | OP_ADD_C          235 '235'\n"
      "0708    | OP_ADD_C          236 '236'\n"
      "0711    | OP_ADD_C          237 '237'\n"
      "0714    | OP_ADD_C          238 '238'\n"
      "0717    | OP_ADD_C          239 '239'\n"
      "0720   26 OP_ADD_C          240 '240'\n"
      "0723    | OP_ADD_C          241 '241'\n"
      "0726    | OP_ADD_C          242 '242'\n"
      "0729    | OP_ADD_C          243 '243'\n"
      "0732    | OP_ADD_C          244 '244'\n"
      "0735    | OP_ADD_C          245 '245'\n"
      "0738    | OP_ADD_C          246 '246'\n"
      "0741    | OP_ADD_C          247 '247'\n"
      "0744    | OP_ADD_C          248 '248'\n"
      "0747    | OP_ADD_C          249 '249'\n"
      "0750   27 OP_ADD_C          250 '250'\n"
      "0753    | OP_ADD_C          251 '251'\n"
      "0756    | OP_ADD_C          252 '252'\n"
      "0759    | OP_ADD_C          253 '253'\n"
      "0762    | OP_ADD_C          254 '254'\n"
      "0765    | OP_ADD_C          255 '255'\n"
      "0768    | OP_ADD_C          256 '256'\n"
      "0771    | OP_ADD_C          257 '257'\n"
      "0774    | OP_ADD_C          258 '258'\n"
      "0777    | OP_ADD_C          259 '259'\n"
      "0780    | OP_PRINT\n"
      "0781   28 OP_NIL\n"
      "0782    | OP_RETURN\n" },
};

DUMP_SRC(Constants, constants, 1);

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
