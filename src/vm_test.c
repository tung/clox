#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "utest.h"

#include "chunk.h"
#include "list.h"
#include "membuf.h"
#include "object.h"
#include "value.h"

#define memBufSuffix(mb, constStr) \
  ((mb).buf + (mb).size + 1 - sizeof(constStr))
#define ufx utest_fixture

#define NIL NIL_LIT
#define B BOOL_LIT
#define N NUMBER_LIT

// We need enough of ObjString here for the VM to process them.
// We don't need to set .obj.next since no memory management occurs
// during interpretation for now.
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

struct VM {
  MemBuf out;
  MemBuf err;
  VM vm;
  Chunk chunk;
};

UTEST_F_SETUP(VM) {
  initMemBuf(&ufx->out);
  initMemBuf(&ufx->err);
  initVM(&ufx->vm, ufx->out.fptr, ufx->err.fptr);
  initChunk(&ufx->chunk);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(VM) {
  freeMemBuf(&ufx->out);
  freeMemBuf(&ufx->err);
  freeVM(&ufx->vm);
  freeChunk(&ufx->chunk);
  ASSERT_TRUE(1);
}

UTEST_F(VM, Empty) {
  ASSERT_EQ(0, ufx->vm.stackTop - ufx->vm.stack);
}

UTEST_F(VM, PushPop) {
  push(&ufx->vm, NUMBER_VAL(1.2));
  push(&ufx->vm, NUMBER_VAL(3.4));
  push(&ufx->vm, NUMBER_VAL(5.6));
  ASSERT_EQ(3, ufx->vm.stackTop - ufx->vm.stack);
  EXPECT_VALEQ(NUMBER_VAL(5.6), pop(&ufx->vm));
  EXPECT_VALEQ(NUMBER_VAL(3.4), pop(&ufx->vm));
  EXPECT_VALEQ(NUMBER_VAL(1.2), pop(&ufx->vm));
  ASSERT_EQ(0, ufx->vm.stackTop - ufx->vm.stack);
}

UTEST_F(VM, InterpretOk) {
  InterpretResult ires;

  ires = interpret(&ufx->vm, "print 1 + 2;");
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  ires = interpret(&ufx->vm, "print \"foo\" + \"bar\" + \"baz\";");
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  ires = interpret(&ufx->vm, "print 1 + 2;");
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);
}

UTEST_F(VM, InterpretErrorOk) {
  InterpretResult ires;

  ires = interpret(&ufx->vm, "var x = 1");
  EXPECT_EQ((InterpretResult)INTERPRET_COMPILE_ERROR, ires);

  ires = interpret(&ufx->vm, "var x = 1;");
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);
}

UTEST_F(VM, InterpretCompileError) {
  InterpretResult ires = interpret(&ufx->vm, "#");
  EXPECT_EQ((InterpretResult)INTERPRET_COMPILE_ERROR, ires);
}

UTEST_F(VM, UnknownOp) {
  writeChunk(&ufx->chunk, 255, 1);

  InterpretResult ires = interpretChunk(&ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  fflush(ufx->err.fptr);
  const char errMsg[] = "Unknown opcode 255\n";
  EXPECT_STREQ(errMsg, memBufSuffix(ufx->err, errMsg));
}

UTEST_F(VM, InterpretEmpty) {
  InterpretResult ires = interpretChunk(&ufx->vm, &ufx->chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  fflush(ufx->err.fptr);
  const char errMsg[] = "missing OP_RETURN\n";
  EXPECT_STREQ(errMsg, memBufSuffix(ufx->err, errMsg));
}

typedef struct {
  const char* msgSuffix;
  InterpretResult ires;
  int codeSize;
  uint8_t* code;
  int valueSize;
  Value* values;
} ResultFromChunk;

struct VMInterpret {
  ResultFromChunk* cases;
};

UTEST_I_SETUP(VMInterpret) {
  (void)utest_index;
  (void)utest_fixture;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(VMInterpret) {
  ResultFromChunk* expected = &ufx->cases[utest_index];

  MemBuf out, err;
  VM vm;
  Chunk chunk;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);
  initChunk(&chunk);

  // Prepare the chunk.
  for (int i = 0; i < expected->codeSize; ++i) {
    writeChunk(&chunk, expected->code[i], i >> 1);
  }
  for (int i = 0; i < expected->valueSize; ++i) {
    addConstant(&chunk, expected->values[i]);
  }

  // Interpret the chunk.
  InterpretResult ires = interpretChunk(&vm, &chunk);
  EXPECT_EQ(expected->ires, ires);

  fflush(out.fptr);
  fflush(err.fptr);

  // Compare output to expected output.
  size_t msgSuffixLen = strlen(expected->msgSuffix);
  if (strlen(out.buf) >= msgSuffixLen) {
    EXPECT_STREQ(
        expected->msgSuffix, out.buf + out.size - msgSuffixLen);
  } else {
    EXPECT_STREQ(expected->msgSuffix, out.buf);
  }

  // If INTERPRET_OK was expected but not achieved, show errors.
  if (expected->ires == INTERPRET_OK && ires != INTERPRET_OK) {
    EXPECT_STREQ("", err.buf);
  }

  freeMemBuf(&out);
  freeMemBuf(&err);
  freeVM(&vm);
  freeChunk(&chunk);
}

#define VM_INTERPRET(name, data, count) \
  UTEST_I(VMInterpret, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

ResultFromChunk opConstant[] = {
  { "nil\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_RETURN),
      LIST(Value, NIL) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_RETURN),
      LIST(Value, B(false)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_RETURN),
      LIST(Value, B(true)) },
  { "2.5\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_RETURN),
      LIST(Value, N(2.5)) },
};

VM_INTERPRET(OpConstant, opConstant, 4);

ResultFromChunk opLiterals[] = {
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_PRINT, OP_RETURN), LIST(Value) },
  { "nil\n", INTERPRET_OK, LIST(uint8_t, OP_NIL, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "true\n", INTERPRET_OK, LIST(uint8_t, OP_TRUE, OP_PRINT, OP_RETURN),
      LIST(Value) },
};

VM_INTERPRET(OpLiterals, opLiterals, 3);

ResultFromChunk opPop[] = {
  { "0\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_POP, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0), N(1.0)) },
};

VM_INTERPRET(OpPop, opPop, 1);

ResultFromChunk opLocals[] = {
  { "false\ntrue\nfalse\ntrue\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_FALSE, OP_GET_LOCAL, 0, OP_GET_LOCAL, 1,
          OP_PRINT, OP_PRINT, OP_PRINT, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "true\ntrue\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_TRUE, OP_SET_LOCAL, 0, OP_PRINT,
          OP_PRINT, OP_RETURN),
      LIST(Value) },
};

VM_INTERPRET(OpLocals, opLocals, 2);

ResultFromChunk opGlobals[] = {
  { "", INTERPRET_RUNTIME_ERROR, LIST(uint8_t, OP_GET_GLOBAL, 0),
      LIST(Value, S("foo")) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_SET_GLOBAL, 0), LIST(Value, S("foo")) },
  { "123\n456\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 1, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL,
          0, OP_PRINT, OP_CONSTANT, 2, OP_SET_GLOBAL, 0, OP_POP,
          OP_GET_GLOBAL, 0, OP_PRINT, OP_RETURN),
      LIST(Value, S("foo"), N(123.0), N(456.0)) },
};

VM_INTERPRET(OpGlobals, opGlobals, 3);

ResultFromChunk opEqual[] = {
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_EQUAL, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_FALSE, OP_EQUAL, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_FALSE, OP_EQUAL, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_TRUE, OP_EQUAL, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_TRUE, OP_EQUAL, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_EQUAL, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(1.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_EQUAL, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(1.0), N(1.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_EQUAL, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(1.0), N(2.0)) },
};

VM_INTERPRET(OpEqual, opEqual, 8);

ResultFromChunk opGreater[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_GREATER, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_GREATER, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT, OP_RETURN),
      LIST(Value, N(1.0), N(2.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT, OP_RETURN),
      LIST(Value, N(2.0), N(2.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT, OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_INTERPRET(OpGreater, opGreater, 5);

ResultFromChunk opLess[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_LESS, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_LESS, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(1.0), N(2.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(2.0), N(2.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_INTERPRET(OpLess, opLess, 5);

ResultFromChunk opAdd[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_ADD, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(
          uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT, OP_RETURN),
      LIST(Value, N(0.0)) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(
          uint8_t, OP_CONSTANT, 0, OP_NIL, OP_ADD, OP_PRINT, OP_RETURN),
      LIST(Value, N(0.0)) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(
          uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT, OP_RETURN),
      LIST(Value, N(0.0)) },
  { "5\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_INTERPRET(OpAdd, opAdd, 5);

ResultFromChunk opAddConcat[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(
          uint8_t, OP_CONSTANT, 0, OP_NIL, OP_ADD, OP_PRINT, OP_RETURN),
      LIST(Value, S("")) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(
          uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT, OP_RETURN),
      LIST(Value, S("")) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0), S("")) },
  { "foo\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_RETURN),
      LIST(Value, S("foo"), S("")) },
  { "foo\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_RETURN),
      LIST(Value, S(""), S("foo")) },
  { "foobar\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_RETURN),
      LIST(Value, S("foo"), S("bar")) },
  { "foobarfoobar\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_CONSTANT, 3, OP_ADD, OP_ADD, OP_PRINT, OP_RETURN),
      LIST(Value, S("foo"), S("bar"), S("foo"), S("bar")) },
};

VM_INTERPRET(OpAddConcat, opAddConcat, 7);

ResultFromChunk opSubtract[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_SUBTRACT, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_SUBTRACT, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0)) },
  { "1\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT,
          OP_PRINT, OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_INTERPRET(OpSubtract, opSubtract, 3);

ResultFromChunk opMultiply[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_MULTIPLY, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_MULTIPLY, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0)) },
  { "6\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_PRINT, OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_INTERPRET(OpMultiply, opMultiply, 3);

ResultFromChunk opDivide[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_DIVIDE, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_DIVIDE, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0)) },
  { "1.5\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_INTERPRET(OpDivide, opDivide, 3);

ResultFromChunk opNot[] = {
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_NIL, OP_NOT, OP_PRINT, OP_RETURN), LIST(Value) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_NOT, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_NOT, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NOT, OP_PRINT, OP_RETURN),
      LIST(Value, N(0.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_NOT, OP_NOT, OP_PRINT, OP_RETURN),
      LIST(Value) },
};

VM_INTERPRET(OpNot, opNot, 5);

ResultFromChunk opNegate[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NEGATE, OP_PRINT, OP_RETURN),
      LIST(Value) },
  { "-1\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_PRINT, OP_RETURN),
      LIST(Value, N(1.0)) },
  { "1\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(1.0)) },
};

VM_INTERPRET(OpNegate, opNegate, 3);

ResultFromChunk opJump[] = {
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_JUMP, 0, 2, OP_NIL, OP_PRINT, OP_TRUE, OP_PRINT,
          OP_RETURN),
      LIST(Value) },
};

VM_INTERPRET(OpJump, opJump, 1);

ResultFromChunk opJumpIfFalse[] = {
  { "0\n2\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_JUMP_IF_FALSE,
          0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT, 2, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
  { "0\n2\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_FALSE,
          OP_JUMP_IF_FALSE, 0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT,
          2, OP_PRINT, OP_RETURN),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
  { "0\n1\n2\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_TRUE, OP_JUMP_IF_FALSE,
          0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT, 2, OP_PRINT,
          OP_RETURN),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
};

VM_INTERPRET(OpJumpIfFalse, opJumpIfFalse, 3);

ResultFromChunk opLoop[] = {
  { "0\n1\n2\n3\n4\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_LOCAL, 0, OP_CONSTANT, 1,
          OP_LESS, OP_JUMP_IF_FALSE, 0, 15, OP_POP, OP_GET_LOCAL, 0,
          OP_PRINT, OP_GET_LOCAL, 0, OP_CONSTANT, 2, OP_ADD,
          OP_SET_LOCAL, 0, OP_POP, OP_LOOP, 0, 23, OP_POP, OP_RETURN),
      LIST(Value, N(0.0), N(5.0), N(1.0)) },
};

VM_INTERPRET(OpLoop, opLoop, 1);

UTEST_MAIN();
