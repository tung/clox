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
  Obj* objects;
  Table strings;
  MemBuf out;
  MemBuf err;
  SourceToChunk* cases;
};

UTEST_I_SETUP(CompileExpr) {
  (void)utest_index;
  ufx->objects = NULL;
  initTable(&ufx->strings, 0.75);
  initMemBuf(&ufx->out);
  initMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(CompileExpr) {
  SourceToChunk* expected = &ufx->cases[utest_index];

  // Prepare expected/actual err memstreams.
  MemBuf xErr, aErr;
  initMemBuf(&xErr);
  initMemBuf(&aErr);

  // If success is expected, assemble, dump and free our expected chunk.
  if (expected->result) {
    Chunk expectChunk;
    initChunk(&expectChunk);
    for (int i = 0; i < expected->codeSize; ++i) {
      writeChunk(&expectChunk, expected->code[i], 1);
    }
    writeChunk(&expectChunk, OP_PRINT, 1);
    writeChunk(&expectChunk, OP_NIL, 1);
    writeChunk(&expectChunk, OP_RETURN, 1);
    for (int i = 0; i < expected->valueSize; ++i) {
      addConstant(&expectChunk, expected->values[i]);
    }
    disassembleChunk(xErr.fptr, &expectChunk, "CompileExpr");
    freeChunk(&expectChunk);
  }

  // Prepare expression as "print ${expr};" for compile function.
  char srcBuf[256];
  snprintf(srcBuf, sizeof(srcBuf) - 1, "print %s;", expected->src);
  srcBuf[sizeof(srcBuf) - 1] = '\0';

  ObjFunction* result = compile(ufx->out.fptr, ufx->err.fptr, srcBuf,
      &ufx->objects, &ufx->strings);

  EXPECT_EQ(expected->result, !!result);

  // If success was expected but not achieved, print any compile errors.
  if (expected->result && !result) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
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
  freeTable(&ufx->strings);
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

struct CompileStmt {
  Obj* objects;
  Table strings;
  MemBuf out;
  MemBuf err;
  SourceToChunk* cases;
};

UTEST_I_SETUP(CompileStmt) {
  (void)utest_index;
  ufx->objects = NULL;
  initTable(&ufx->strings, 0.75);
  initMemBuf(&ufx->out);
  initMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(CompileStmt) {
  SourceToChunk* expected = &ufx->cases[utest_index];

  // Prepare expected/actual out/err memstreams.
  MemBuf xErr, aErr;
  initMemBuf(&xErr);
  initMemBuf(&aErr);

  // If success is expected, assemble, dump and free our expected chunk.
  if (expected->result) {
    Chunk expectChunk;
    initChunk(&expectChunk);
    for (int i = 0; i < expected->codeSize; ++i) {
      writeChunk(&expectChunk, expected->code[i], 1);
    }
    writeChunk(&expectChunk, OP_NIL, 1);
    writeChunk(&expectChunk, OP_RETURN, 1);
    for (int i = 0; i < expected->valueSize; ++i) {
      addConstant(&expectChunk, expected->values[i]);
    }
    disassembleChunk(xErr.fptr, &expectChunk, "CompileExpr");
    freeChunk(&expectChunk);
  }

  ObjFunction* result = compile(ufx->out.fptr, ufx->err.fptr,
      expected->src, &ufx->objects, &ufx->strings);

  EXPECT_EQ(expected->result, !!result);

  // If success was expected but not achieved, print any compile errors.
  if (expected->result && !result) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
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
  freeTable(&ufx->strings);
  freeObjects(ufx->objects);
  freeMemBuf(&ufx->out);
  freeMemBuf(&ufx->err);
}

#define COMPILE_STMTS(name, data, count) \
  UTEST_I(CompileStmt, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

SourceToChunk stmtFunctions[] = {
  { "fun", false, LIST(uint8_t), LIST(Value) },
  { "fun a", false, LIST(uint8_t), LIST(Value) },
  { "fun a()", false, LIST(uint8_t), LIST(Value) },
  { "fun a(x", false, LIST(uint8_t), LIST(Value) },
  { "fun a(x,", false, LIST(uint8_t), LIST(Value) },
  { "fun a(x,y){", false, LIST(uint8_t), LIST(Value) },
  { "return", false, LIST(uint8_t), LIST(Value) },
  { "fun a(){return", false, LIST(uint8_t), LIST(Value) },
  { "fun a(){return;", false, LIST(uint8_t), LIST(Value) },
  { "fun a(){return 0", false, LIST(uint8_t), LIST(Value) },
  { "a(", false, LIST(uint8_t), LIST(Value) },
  { "a(0", false, LIST(uint8_t), LIST(Value) },
};

COMPILE_STMTS(Functions, stmtFunctions, 12);

SourceToChunk stmtVarDecl[] = {
  { "var foo;", true, LIST(uint8_t, OP_NIL, OP_DEFINE_GLOBAL, 0),
      LIST(Value, S("foo")) },
  { "var foo = 0;", true,
      LIST(uint8_t, OP_CONSTANT, 1, OP_DEFINE_GLOBAL, 0),
      LIST(Value, S("foo"), N(0.0)) },
  { "{ var foo; }", true, LIST(uint8_t, OP_NIL, OP_POP), LIST(Value) },
  { "{ var foo = 0; }", true, LIST(uint8_t, OP_CONSTANT, 0, OP_POP),
      LIST(Value, N(0.0)) },
};

COMPILE_STMTS(VarDecl, stmtVarDecl, 4);

SourceToChunk stmtLocalVars[] = {
  { "{ var foo = 123; print foo; }", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_PRINT, OP_POP),
      LIST(Value, N(123.0)) },
  { "{ var a = 1; var foo = 2; print a + foo; }", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_LOCAL, 1,
          OP_GET_LOCAL, 2, OP_ADD, OP_PRINT, OP_POP, OP_POP),
      LIST(Value, N(1.0), N(2.0)) },
  { "var a = 1; { var a = 2; print a; } print a;", true,
      LIST(uint8_t, OP_CONSTANT, 1, OP_DEFINE_GLOBAL, 0, OP_CONSTANT, 2,
          OP_GET_LOCAL, 1, OP_PRINT, OP_POP, OP_GET_GLOBAL, 3,
          OP_PRINT),
      LIST(Value, S("a"), N(1.0), N(2.0), S("a")) },
  { "{ var a = 1; { var a = 2; print a; } print a; }", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_LOCAL, 2,
          OP_PRINT, OP_POP, OP_GET_LOCAL, 1, OP_PRINT, OP_POP),
      LIST(Value, N(1.0), N(2.0)) },
  { "var a; { var b = a; var c = b; }", true,
      LIST(uint8_t, OP_NIL, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 1,
          OP_GET_LOCAL, 1, OP_POP, OP_POP),
      LIST(Value, S("a"), S("a")) },
};

COMPILE_STMTS(LocalVars, stmtLocalVars, 5);

SourceToChunk stmtFor[] = {
  { "for (;;) 0;", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_POP, OP_LOOP, 0, 6),
      LIST(Value, N(0.0)) },
  { "for (var a = 0;;) 1;", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_POP, OP_LOOP, 0,
          6, OP_POP),
      LIST(Value, N(0.0), N(1.0)) },
  { "for (0;;) 1;", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_POP, OP_CONSTANT, 1, OP_POP,
          OP_LOOP, 0, 6),
      LIST(Value, N(0.0), N(1.0)) },
  { "for (; false;) 0;", true,
      LIST(uint8_t, OP_FALSE, OP_JUMP_IF_FALSE, 0, 7, OP_POP,
          OP_CONSTANT, 0, OP_POP, OP_LOOP, 0, 11, OP_POP),
      LIST(Value, N(0.0)) },
  { "for (;; 0) 1;", true,
      LIST(uint8_t, OP_JUMP, 0, 6, OP_CONSTANT, 0, OP_POP, OP_LOOP, 0,
          9, OP_CONSTANT, 1, OP_POP, OP_LOOP, 0, 12),
      LIST(Value, N(0.0), N(1.0)) },
  { "for (var i = 0; i < 5; i = i + 1) print i;", true,
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_CONSTANT, 1,
          OP_LESS, OP_JUMP_IF_FALSE, 0, 21, OP_POP, OP_JUMP, 0, 11,
          OP_GET_LOCAL, 1, OP_CONSTANT, 2, OP_ADD, OP_SET_LOCAL, 1,
          OP_POP, OP_LOOP, 0, 23, OP_GET_LOCAL, 1, OP_PRINT, OP_LOOP, 0,
          17, OP_POP, OP_POP),
      LIST(Value, N(0.0), N(5.0), N(1.0)) },
};

COMPILE_STMTS(For, stmtFor, 6);

SourceToChunk stmtIf[] = {
  { "if (true) 0;", true,
      LIST(uint8_t, OP_TRUE, OP_JUMP_IF_FALSE, 0, 7, OP_POP,
          OP_CONSTANT, 0, OP_POP, OP_JUMP, 0, 1, OP_POP),
      LIST(Value, N(0.0)) },
  { "if (false) 0; else 1;", true,
      LIST(uint8_t, OP_FALSE, OP_JUMP_IF_FALSE, 0, 7, OP_POP,
          OP_CONSTANT, 0, OP_POP, OP_JUMP, 0, 4, OP_POP, OP_CONSTANT, 1,
          OP_POP),
      LIST(Value, N(0.0), N(1.0)) },
};

COMPILE_STMTS(If, stmtIf, 2);

SourceToChunk stmtWhile[] = {
  { "while (false) 0;", true,
      LIST(uint8_t, OP_FALSE, OP_JUMP_IF_FALSE, 0, 7, OP_POP,
          OP_CONSTANT, 0, OP_POP, OP_LOOP, 0, 11, OP_POP),
      LIST(Value, N(0.0)) },
};

COMPILE_STMTS(While, stmtWhile, 1);

struct Compile {
  Chunk chunk;
  Obj* objects;
  Table strings;
  MemBuf out;
  MemBuf err;
};

UTEST_F_SETUP(Compile) {
  initChunk(&ufx->chunk);
  ufx->objects = NULL;
  initTable(&ufx->strings, 0.75);
  initMemBuf(&ufx->out);
  initMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(Compile) {
  freeChunk(&ufx->chunk);
  freeTable(&ufx->strings);
  freeObjects(ufx->objects);
  freeMemBuf(&ufx->out);
  freeMemBuf(&ufx->err);
  ASSERT_TRUE(1);
}

UTEST_F(Compile, PrintErrorEOF) {
  EXPECT_FALSE(compile(ufx->out.fptr, ufx->err.fptr, "print 0",
      &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at end: Expect ';' after value.\n", ufx->err.buf);
}

UTEST_F(Compile, PrintErrorSyncSemicolon) {
  EXPECT_FALSE(compile(ufx->out.fptr, ufx->err.fptr,
      "print 0 1; print 2;", &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at '1': Expect ';' after value.\n", ufx->err.buf);
}

UTEST_F(Compile, PrintErrorSyncPrint) {
  EXPECT_FALSE(compile(ufx->out.fptr, ufx->err.fptr,
      "print 0 1 print 2;", &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at '1': Expect ';' after value.\n", ufx->err.buf);
}

UTEST_F(Compile, ExprErrorNoSemicolon) {
  EXPECT_FALSE(compile(
      ufx->out.fptr, ufx->err.fptr, "0", &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ("[line 1] Error at end: Expect ';' after expression.\n",
      ufx->err.buf);
}

UTEST_F(Compile, VarDeclErrorNoName) {
  EXPECT_FALSE(compile(ufx->out.fptr, ufx->err.fptr, "var 0",
      &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at '0': Expect variable name.\n", ufx->err.buf);
}

UTEST_F(Compile, VarDeclErrorNoSemicolon) {
  EXPECT_FALSE(compile(ufx->out.fptr, ufx->err.fptr, "var foo",
      &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at end: Expect ';' after variable declaration.\n",
      ufx->err.buf);
}

UTEST_F(Compile, VarDeclErrorLocalInitSelf) {
  EXPECT_FALSE(compile(ufx->out.fptr, ufx->err.fptr, "{ var x = x; }",
      &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at 'x': "
      "Can't read local variable in its own initializer.\n",
      ufx->err.buf);
}

UTEST_F(Compile, VarDeclErrorLocalDuplicate) {
  EXPECT_FALSE(compile(ufx->out.fptr, ufx->err.fptr,
      "{ var x; var x; }", &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at 'x': "
      "Already a variable with this name in this scope.\n",
      ufx->err.buf);
}

UTEST_F(Compile, BlockErrorNoRightBrace) {
  EXPECT_FALSE(compile(
      ufx->out.fptr, ufx->err.fptr, "{", &ufx->objects, &ufx->strings));
  fflush(ufx->err.fptr);
  EXPECT_STREQ(
      "[line 1] Error at end: Expect '}' after block.\n", ufx->err.buf);
}

UTEST_MAIN();
