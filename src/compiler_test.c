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
      .chars = str, \
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
  SourceToChunk* cases;
};

UTEST_I_SETUP(CompileExpr) {
  (void)utest_index;
  (void)utest_fixture;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(CompileExpr) {
  SourceToChunk* expected = &ufx->cases[utest_index];

  // Fixture setup.
  GC gc;
  Table strings;
  MemBuf out, err;
  initGC(&gc);
  initTable(&strings, 0.75);
  initMemBuf(&out);
  initMemBuf(&err);

  // Prepare expected/actual err memstreams.
  MemBuf xErr, aErr;
  initMemBuf(&xErr);
  initMemBuf(&aErr);

  // If success is expected, assemble, dump and free our expected chunk.
  if (expected->result) {
    Chunk expectChunk;
    initChunk(&expectChunk);
    for (int i = 0; i < expected->codeSize; ++i) {
      writeChunk(&gc, &expectChunk, expected->code[i], 1);
    }
    writeChunk(&gc, &expectChunk, OP_PRINT, 1);
    writeChunk(&gc, &expectChunk, OP_NIL, 1);
    writeChunk(&gc, &expectChunk, OP_RETURN, 1);
    for (int i = 0; i < expected->valueSize; ++i) {
      addConstant(&gc, &expectChunk, expected->values[i]);
    }
    disassembleChunk(xErr.fptr, &expectChunk, "CompileExpr");
    freeChunk(&gc, &expectChunk);
  }

  // Prepare expression as "print ${expr};" for compile function.
  char srcBuf[256];
  snprintf(srcBuf, sizeof(srcBuf) - 1, "print %s;", expected->src);
  srcBuf[sizeof(srcBuf) - 1] = '\0';

  ObjFunction* result =
      compile(out.fptr, err.fptr, srcBuf, &gc, &strings);

  EXPECT_EQ(expected->result, !!result);

  // If success was expected but not achieved, print any compile errors.
  if (expected->result && !result) {
    fflush(err.fptr);
    EXPECT_STREQ("", err.buf);
  }

  // If compile succeeded, dump the actual chunk.
  if (result) {
    disassembleChunk(aErr.fptr, &result->chunk, "CompileExpr");
  }

  // Compare err memstreams.
  fflush(xErr.fptr);
  fflush(aErr.fptr);
  EXPECT_STREQ(xErr.buf, aErr.buf);

  // Clean up memstreams.
  freeMemBuf(&xErr);
  freeMemBuf(&aErr);

  // Fixture teardown.
  freeTable(&gc, &strings);
  freeGC(&gc);
  freeMemBuf(&out);
  freeMemBuf(&err);
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
  { "false", true, LIST(uint8_t, OP_FALSE), LIST(Value) },
  { "nil", true, LIST(uint8_t, OP_NIL), LIST(Value) },
  { "true", true, LIST(uint8_t, OP_TRUE), LIST(Value) },
};

COMPILE_EXPRS(Literal, exprLiteral, 3);

SourceToChunk exprNumber[] = {
  { "123", true, LIST(uint8_t, OP_CONSTANT, 0), LIST(Value, N(123.0)) },
};

COMPILE_EXPRS(Number, exprNumber, 1);

SourceToChunk exprString[] = {
  { "\"\"", true, LIST(uint8_t, OP_CONSTANT, 0), LIST(Value, S("")) },
  { "\"foo\"", true, LIST(uint8_t, OP_CONSTANT, 0),
      LIST(Value, S("foo")) },
};

COMPILE_EXPRS(String, exprString, 2);

SourceToChunk exprVarGet[] = {
  { "foo", true, LIST(uint8_t, OP_GET_GLOBAL, 0),
      LIST(Value, S("foo")) },
};

COMPILE_EXPRS(VarGet, exprVarGet, 1);

SourceToChunk exprVarSet[] = {
  { "foo = 0", true, LIST(uint8_t, OP_CONSTANT, 1, OP_SET_GLOBAL, 0),
      LIST(Value, S("foo"), N(0.0)) },
  { "foo + bar = 0", false, LIST(uint8_t), LIST(Value) },
};

COMPILE_EXPRS(VarSet, exprVarSet, 2);

SourceToChunk exprUnary[] = {
  { "-1", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE),
      LIST(Value, N(1.0)) },
  { "--1", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE),
      LIST(Value, N(1.0)) },
  { "!false", true, LIST(uint8_t, OP_FALSE, OP_NOT), LIST(Value) },
  { "!!false", true, LIST(uint8_t, OP_FALSE, OP_NOT, OP_NOT),
      LIST(Value) },
};

COMPILE_EXPRS(Unary, exprUnary, 4);

SourceToChunk exprGrouping[] = {
  { "(", false, LIST(uint8_t), LIST(Value) },
  { "(1)", true, LIST(uint8_t, OP_CONSTANT, 0), LIST(Value, N(1.0)) },
  { "(-1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE),
      LIST(Value, N(1.0)) },
  { "-(1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE),
      LIST(Value, N(1.0)) },
  { "-(-1)", true, LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE),
      LIST(Value, N(1.0)) },
};

COMPILE_EXPRS(Grouping, exprGrouping, 5);

SourceToChunk exprBinaryNums[] = {
  { "3 + 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD),
      LIST(Value, N(3.0), N(2.0)) },
  { "3 - 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT),
      LIST(Value, N(3.0), N(2.0)) },
  { "3 * 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY),
      LIST(Value, N(3.0), N(2.0)) },
  { "3 / 2", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE),
      LIST(Value, N(3.0), N(2.0)) },
  { "4 + 3 - 2 + 1 - 0", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_SUBTRACT, OP_CONSTANT, 3, OP_ADD, OP_CONSTANT, 4,
          OP_SUBTRACT),
      LIST(Value, N(4.0), N(3.0), N(2.0), N(1.0), N(0.0)) },
  { "4 / 3 * 2 / 1 * 0", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_DIVIDE,
          OP_CONSTANT, 4, OP_MULTIPLY),
      LIST(Value, N(4.0), N(3.0), N(2.0), N(1.0), N(0.0)) },
  { "3 * 2 + 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_CONSTANT, 2, OP_ADD),
      LIST(Value, N(3.0), N(2.0), N(1.0)) },
  { "3 + 2 * 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
          OP_MULTIPLY, OP_ADD),
      LIST(Value, N(3.0), N(2.0), N(1.0)) },
  { "(-1 + 2) * 3 - -4", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_CONSTANT, 1, OP_ADD,
          OP_CONSTANT, 2, OP_MULTIPLY, OP_CONSTANT, 3, OP_NEGATE,
          OP_SUBTRACT),
      LIST(Value, N(1.0), N(2.0), N(3.0), N(4.0)) },
};

COMPILE_EXPRS(BinaryNums, exprBinaryNums, 9);

SourceToChunk exprBinaryCompare[] = {
  { "true != true", true,
      LIST(uint8_t, OP_TRUE, OP_TRUE, OP_EQUAL, OP_NOT), LIST(Value) },
  { "true == true", true, LIST(uint8_t, OP_TRUE, OP_TRUE, OP_EQUAL),
      LIST(Value) },
  { "0 > 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER),
      LIST(Value, N(0.0), N(1.0)) },
  { "0 >= 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_NOT),
      LIST(Value, N(0.0), N(1.0)) },
  { "0 < 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS),
      LIST(Value, N(0.0), N(1.0)) },
  { "0 <= 1", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER, OP_NOT),
      LIST(Value, N(0.0), N(1.0)) },
  { "0 + 1 < 2 == true", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_LESS, OP_TRUE, OP_EQUAL),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
  { "true == 0 < 1 + 2", true,
      LIST(uint8_t, OP_TRUE, OP_CONSTANT, 0, OP_CONSTANT, 1,
          OP_CONSTANT, 2, OP_ADD, OP_LESS, OP_EQUAL),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
};

COMPILE_EXPRS(BinaryCompare, exprBinaryCompare, 8);

SourceToChunk exprConcatStrings[] = {
  { "\"\" + \"\"", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD),
      LIST(Value, S(""), S("")) },
  { "\"foo\" + \"bar\"", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD),
      LIST(Value, S("foo"), S("bar")) },
  { "\"foo\" + \"bar\" + \"baz\"", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_ADD),
      LIST(Value, S("foo"), S("bar"), S("baz")) },
};

COMPILE_EXPRS(ConcatStrings, exprConcatStrings, 3);

SourceToChunk exprLogical[] = {
  { "true and false", true,
      LIST(uint8_t, OP_TRUE, OP_JUMP_IF_FALSE, 0, 2, OP_POP, OP_FALSE),
      LIST(Value) },
  { "false or true", true,
      LIST(uint8_t, OP_FALSE, OP_JUMP_IF_FALSE, 0, 3, OP_JUMP, 0, 2,
          OP_POP, OP_TRUE),
      LIST(Value) },
  { "0 == 1 and 2 or 3", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_EQUAL,
          OP_JUMP_IF_FALSE, 0, 3, OP_POP, OP_CONSTANT, 2,
          OP_JUMP_IF_FALSE, 0, 3, OP_JUMP, 0, 3, OP_POP, OP_CONSTANT,
          3),
      LIST(Value, N(0.0), N(1.0), N(2.0), N(3.0)) },
  { "0 or 1 and 2 == 3", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_JUMP_IF_FALSE, 0, 3, OP_JUMP, 0,
          12, OP_POP, OP_CONSTANT, 1, OP_JUMP_IF_FALSE, 0, 6, OP_POP,
          OP_CONSTANT, 2, OP_CONSTANT, 3, OP_EQUAL),
      LIST(Value, N(0.0), N(1.0), N(2.0), N(3.0)) },
};

COMPILE_EXPRS(Logical, exprLogical, 4);

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

SourceToDump functions[] = {
  { false, "fun", "Expect function name." },
  { false, "fun a", "Expect '(' after function name." },
  { false, "fun a(", "Expect parameter name." },
  { false, "fun a()", "Expect '{' before function body." },
  { false, "fun a(x", "Expect ')' after parameters." },
  { false, "fun a(x,", "Expect parameter name." },
  { false, "fun a(x,y){", "Expect '}' after block." },
  { false, "return", "Can't return from top-level code." },
  { false, "fun a(){return", "Expect expression." },
  { false, "fun a(){return;", "Expect '}' after block." },
  { false, "a(", "Expect expression." },
  { false, "a(0", "Expect ')' after arguments." },
  { true, "fun a(){print 1;}a();",
      "== a ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLOSURE          1 <fn a>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       2 'a'\n"
      "0006    | OP_CALL             0\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "fun a(x){print x;}a(1);",
      "== a ==\n"
      "0000    1 OP_GET_LOCAL        1\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLOSURE          1 <fn a>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       2 'a'\n"
      "0006    | OP_CONSTANT         3 '1'\n"
      "0008    | OP_CALL             1\n"
      "0010    | OP_POP\n"
      "0011    | OP_NIL\n"
      "0012    | OP_RETURN\n" },
  { true, "fun a(){return 1;}print a();",
      "== a ==\n"
      "0000    1 OP_CONSTANT         0 '1'\n"
      "0002    | OP_RETURN\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLOSURE          1 <fn a>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       2 'a'\n"
      "0006    | OP_CALL             0\n"
      "0008    | OP_PRINT\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
  { true, "fun a(x,y){return x+y;}print a(3,a(2,1));",
      "== a ==\n"
      "0000    1 OP_GET_LOCAL        1\n"
      "0002    | OP_GET_LOCAL        2\n"
      "0004    | OP_ADD\n"
      "0005    | OP_RETURN\n"
      "0006    | OP_NIL\n"
      "0007    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLOSURE          1 <fn a>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'a'\n"
      "0004    | OP_GET_GLOBAL       2 'a'\n"
      "0006    | OP_CONSTANT         3 '3'\n"
      "0008    | OP_GET_GLOBAL       4 'a'\n"
      "0010    | OP_CONSTANT         5 '2'\n"
      "0012    | OP_CONSTANT         6 '1'\n"
      "0014    | OP_CALL             2\n"
      "0016    | OP_CALL             2\n"
      "0018    | OP_PRINT\n"
      "0019    | OP_NIL\n"
      "0020    | OP_RETURN\n" },
  { true, "var x=1;fun a(){fun b(){print x;}b();}a();",
      "== b ==\n"
      "0000    1 OP_GET_GLOBAL       0 'x'\n"
      "0002    | OP_PRINT\n"
      "0003    | OP_NIL\n"
      "0004    | OP_RETURN\n"
      "== a ==\n"
      "0000    1 OP_CLOSURE          0 <fn b>\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_CALL             0\n"
      "0006    | OP_POP\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CONSTANT         1 '1'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'x'\n"
      "0004    | OP_CLOSURE          3 <fn a>\n"
      "0006    | OP_DEFINE_GLOBAL    2 'a'\n"
      "0008    | OP_GET_GLOBAL       4 'a'\n"
      "0010    | OP_CALL             0\n"
      "0012    | OP_POP\n"
      "0013    | OP_NIL\n"
      "0014    | OP_RETURN\n" },
};

DUMP_SRC(Functions, functions, 17);

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
      "0007    | OP_CONSTANT         0 '1'\n"
      "0009    | OP_ADD\n"
      "0010    | OP_SET_UPVALUE      0\n"
      "0012    | OP_POP\n"
      "0013    | OP_GET_LOCAL        1\n"
      "0015    | OP_RETURN\n"
      "0016    | OP_NIL\n"
      "0017    | OP_RETURN\n"
      "== counter ==\n"
      "0000    1 OP_CLOSURE          0 <fn incAndPrint>\n"
      "0002      |                     local 1\n"
      "0004    | OP_GET_LOCAL        2\n"
      "0006    | OP_RETURN\n"
      "0007    | OP_NIL\n"
      "0008    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLOSURE          1 <fn counter>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'counter'\n"
      "0004    | OP_GET_GLOBAL       3 'counter'\n"
      "0006    | OP_CONSTANT         4 '1'\n"
      "0008    | OP_CALL             1\n"
      "0010    | OP_DEFINE_GLOBAL    2 'c'\n"
      "0012    | OP_GET_GLOBAL       5 'c'\n"
      "0014    | OP_CALL             0\n"
      "0016    | OP_POP\n"
      "0017    | OP_GET_GLOBAL       6 'c'\n"
      "0019    | OP_CALL             0\n"
      "0021    | OP_POP\n"
      "0022    | OP_GET_GLOBAL       7 'c'\n"
      "0024    | OP_CALL             0\n"
      "0026    | OP_POP\n"
      "0027    | OP_NIL\n"
      "0028    | OP_RETURN\n" },
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
      "0015    | OP_CONSTANT         4 '3'\n"
      "0017    | OP_CONSTANT         5 '4'\n"
      "0019    | OP_CALL             2\n"
      "0021    | OP_POP\n"
      "0022    | OP_NIL\n"
      "0023    | OP_RETURN\n" },
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
      "0000    1 OP_CLOSURE          1 <fn outer>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'outer'\n"
      "0004    | OP_GET_GLOBAL       2 'outer'\n"
      "0006    | OP_CONSTANT         3 '1'\n"
      "0008    | OP_CALL             1\n"
      "0010    | OP_CONSTANT         4 '2'\n"
      "0012    | OP_CALL             1\n"
      "0014    | OP_CONSTANT         5 '3'\n"
      "0016    | OP_CALL             1\n"
      "0018    | OP_PRINT\n"
      "0019    | OP_NIL\n"
      "0020    | OP_RETURN\n" },
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
      "0021    | OP_SET_GLOBAL       7 'f'\n"
      "0023    | OP_POP\n"
      "0024    | OP_CLOSURE          8 <fn gg>\n"
      "0026      |                     local 1\n"
      "0028    | OP_GET_LOCAL        5\n"
      "0030    | OP_SET_GLOBAL       9 'g'\n"
      "0032    | OP_POP\n"
      "0033    | OP_CLOSURE         10 <fn hh>\n"
      "0035      |                     local 2\n"
      "0037    | OP_GET_LOCAL        6\n"
      "0039    | OP_SET_GLOBAL      11 'h'\n"
      "0041    | OP_POP\n"
      "0042    | OP_POP\n"
      "0043    | OP_POP\n"
      "0044    | OP_POP\n"
      "0045    | OP_CLOSE_UPVALUE\n"
      "0046    | OP_CLOSE_UPVALUE\n"
      "0047    | OP_CLOSE_UPVALUE\n"
      "0048    | OP_GET_GLOBAL      12 'f'\n"
      "0050    | OP_CALL             0\n"
      "0052    | OP_POP\n"
      "0053    | OP_GET_GLOBAL      13 'g'\n"
      "0055    | OP_CALL             0\n"
      "0057    | OP_POP\n"
      "0058    | OP_GET_GLOBAL      14 'h'\n"
      "0060    | OP_CALL             0\n"
      "0062    | OP_POP\n"
      "0063    | OP_NIL\n"
      "0064    | OP_RETURN\n" },
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
      "0010    | OP_GET_GLOBAL       3 'a'\n"
      "0012    | OP_PRINT\n"
      "0013    | OP_NIL\n"
      "0014    | OP_RETURN\n" },
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
      "0003    | OP_GET_GLOBAL       1 'a'\n"
      "0005    | OP_GET_LOCAL        1\n"
      "0007    | OP_POP\n"
      "0008    | OP_POP\n"
      "0009    | OP_NIL\n"
      "0010    | OP_RETURN\n" },
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
      "0001    | OP_JUMP_IF_FALSE    1 -> 11\n"
      "0004    | OP_POP\n"
      "0005    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_LOOP             8 -> 0\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
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
      "0004    | OP_CONSTANT         1 '5'\n"
      "0006    | OP_LESS\n"
      "0007    | OP_JUMP_IF_FALSE    7 -> 31\n"
      "0010    | OP_POP\n"
      "0011    | OP_JUMP            11 -> 25\n"
      "0014    | OP_GET_LOCAL        1\n"
      "0016    | OP_CONSTANT         2 '1'\n"
      "0018    | OP_ADD\n"
      "0019    | OP_SET_LOCAL        1\n"
      "0021    | OP_POP\n"
      "0022    | OP_LOOP            22 -> 2\n"
      "0025    | OP_GET_LOCAL        1\n"
      "0027    | OP_PRINT\n"
      "0028    | OP_LOOP            28 -> 14\n"
      "0031    | OP_POP\n"
      "0032    | OP_POP\n"
      "0033    | OP_NIL\n"
      "0034    | OP_RETURN\n" },
};

DUMP_SRC(For, for_, 6);

SourceToDump if_[] = {
  { true, "if(true)0;",
      "== <script> ==\n"
      "0000    1 OP_TRUE\n"
      "0001    | OP_JUMP_IF_FALSE    1 -> 11\n"
      "0004    | OP_POP\n"
      "0005    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_JUMP             8 -> 12\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
  { true, "if(false)0;else 1;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_JUMP_IF_FALSE    1 -> 11\n"
      "0004    | OP_POP\n"
      "0005    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_JUMP             8 -> 15\n"
      "0011    | OP_POP\n"
      "0012    | OP_CONSTANT         1 '1'\n"
      "0014    | OP_POP\n"
      "0015    | OP_NIL\n"
      "0016    | OP_RETURN\n" },
};

DUMP_SRC(If, if_, 2);

SourceToDump while_[] = {
  { true, "while(false)0;",
      "== <script> ==\n"
      "0000    1 OP_FALSE\n"
      "0001    | OP_JUMP_IF_FALSE    1 -> 11\n"
      "0004    | OP_POP\n"
      "0005    | OP_CONSTANT         0 '0'\n"
      "0007    | OP_POP\n"
      "0008    | OP_LOOP             8 -> 0\n"
      "0011    | OP_POP\n"
      "0012    | OP_NIL\n"
      "0013    | OP_RETURN\n" },
};

DUMP_SRC(While, while_, 1);

SourceToDump classes[] = {
  { false, "1+f.x=2;", "Invalid assignment target." },
  { true, "class F{}",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "fun g(){class F{}}",
      "== g ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_NIL\n"
      "0003    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLOSURE          1 <fn g>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'g'\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
  { true, "class F{}var f=F();f.x=1;print f.x;",
      "== <script> ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_DEFINE_GLOBAL    0 'F'\n"
      "0004    | OP_GET_GLOBAL       2 'F'\n"
      "0006    | OP_CALL             0\n"
      "0008    | OP_DEFINE_GLOBAL    1 'f'\n"
      "0010    | OP_GET_GLOBAL       3 'f'\n"
      "0012    | OP_CONSTANT         5 '1'\n"
      "0014    | OP_SET_PROPERTY     4 'x'\n"
      "0016    | OP_POP\n"
      "0017    | OP_GET_GLOBAL       6 'f'\n"
      "0019    | OP_GET_PROPERTY     7 'x'\n"
      "0021    | OP_PRINT\n"
      "0022    | OP_NIL\n"
      "0023    | OP_RETURN\n" },
  { true, "fun g(){class F{}var f=F();f.x=1;print f.x;}",
      "== g ==\n"
      "0000    1 OP_CLASS            0 'F'\n"
      "0002    | OP_GET_LOCAL        1\n"
      "0004    | OP_CALL             0\n"
      "0006    | OP_GET_LOCAL        2\n"
      "0008    | OP_CONSTANT         2 '1'\n"
      "0010    | OP_SET_PROPERTY     1 'x'\n"
      "0012    | OP_POP\n"
      "0013    | OP_GET_LOCAL        2\n"
      "0015    | OP_GET_PROPERTY     3 'x'\n"
      "0017    | OP_PRINT\n"
      "0018    | OP_NIL\n"
      "0019    | OP_RETURN\n"
      "== <script> ==\n"
      "0000    1 OP_CLOSURE          1 <fn g>\n"
      "0002    | OP_DEFINE_GLOBAL    0 'g'\n"
      "0004    | OP_NIL\n"
      "0005    | OP_RETURN\n" },
};

DUMP_SRC(Classes, classes, 5);

SourceToDump error[] = {
  { false, "print 0", "Expect ';' after value." },
  { false, "print 0 1;print 2;", "Expect ';' after value." },
  { false, "print 0 1 print 2;", "Expect ';' after value." },
  { false, "0", "Expect ';' after expression." },
  { false, "{", "Expect '}' after block." },
};

DUMP_SRC(Error, error, 5);

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
