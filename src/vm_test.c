#include "vm.h"

#include <assert.h>
#include <string.h>

#include "utest.h"

#include "list.h"
#include "membuf.h"
#include "memory.h"
#include "object.h"

#define ufx utest_fixture

typedef enum {
  LIT_NIL,
  LIT_TRUE,
  LIT_FALSE,
  LIT_NUMBER,
  LIT_STRING,
  LIT_FUNCTION,
} LitType;

typedef struct {
  LitType type;
  union {
    double number;
    const char* string;
    int functionIndex;
  } as;
} Lit;

typedef struct {
  const char* name;
  int arity;
  int upvalueCount;
  int opcodeCount;
  uint8_t* opcodes;
  int litCount;
  Lit* lits;
} LitFun;

// clang-format off
#define NIL { .type = LIT_NIL }
#define B_TRUE { .type = LIT_TRUE }
#define B_FALSE { .type = LIT_FALSE }
#define N(n) { .type = LIT_NUMBER, .as.number = (n) }
#define S(s) { .type = LIT_STRING, .as.string = (s) }
#define F(f) { .type = LIT_FUNCTION, .as.functionIndex = (f) }
// clang-format on

static size_t fillFun(GC* gc, Table* strings, ObjFunction* fun,
    int opcodeCount, uint8_t* opcodes, int litCount, Lit* lits,
    int funCount, ObjFunction** funs) {
  size_t temps = 0;

  for (int o = 0; o < opcodeCount; ++o) {
    writeChunk(gc, &fun->chunk, opcodes[o], o);
  }
  for (int l = 0; l < litCount; ++l) {
    Lit* lit = &lits[l];
    switch (lit->type) {
      case LIT_NIL: addConstant(gc, &fun->chunk, NIL_VAL); break;
      case LIT_TRUE:
        addConstant(gc, &fun->chunk, BOOL_VAL(true));
        break;
      case LIT_FALSE:
        addConstant(gc, &fun->chunk, BOOL_VAL(false));
        break;
      case LIT_NUMBER:
        addConstant(gc, &fun->chunk, NUMBER_VAL(lit->as.number));
        break;
      case LIT_STRING: {
        ObjString* str = copyString(
            gc, strings, lit->as.string, strlen(lit->as.string));
        pushTemp(gc, OBJ_VAL(str));
        temps++;
        addConstant(gc, &fun->chunk, OBJ_VAL(str));
        break;
      }
      case LIT_FUNCTION:
        assert(lit->as.functionIndex < funCount);
        addConstant(
            gc, &fun->chunk, OBJ_VAL(funs[lit->as.functionIndex]));
        break;
    }
  }

  return temps;
}

typedef struct {
  InterpretResult ires;
  const char* msg;
  int funCount;
  LitFun* funs;
  int opcodeCount;
  uint8_t* opcodes;
  int litCount;
  Lit* lits;
} VMCase;

struct VM {
  VMCase* cases;
};

UTEST_I_SETUP(VM) {
  (void)utest_index;
  (void)utest_fixture;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(VM) {
  VMCase* testCase = &ufx->cases[utest_index];

  MemBuf out, err;
  VM vm;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  size_t temps = 0;

  ObjFunction** funs = calloc(testCase->funCount, sizeof(ObjFunction*));
  if (funs != NULL) {
    // Create functions.
    for (int f = 0; f < testCase->funCount; ++f) {
      LitFun* fun = &testCase->funs[f];
      funs[f] = newFunction(&vm.gc);
      pushTemp(&vm.gc, OBJ_VAL(funs[f]));
      temps++;
      // Function is rooted and thus so is the name string.
      funs[f]->name =
          copyString(&vm.gc, &vm.strings, fun->name, strlen(fun->name));
      funs[f]->arity = fun->arity;
      funs[f]->upvalueCount = fun->upvalueCount;
    }

    // Fill function chunks.
    for (int f = 0; f < testCase->funCount; ++f) {
      LitFun* litFun = &testCase->funs[f];
      temps += fillFun(&vm.gc, &vm.strings, funs[f],
          litFun->opcodeCount, litFun->opcodes, litFun->litCount,
          litFun->lits, testCase->funCount, funs);
    }
  }

  // Create "<script>" function and fill its chunk.
  ObjFunction* scriptFun = newFunction(&vm.gc);
  pushTemp(&vm.gc, OBJ_VAL(scriptFun));
  temps++;
  temps += fillFun(&vm.gc, &vm.strings, scriptFun,
      testCase->opcodeCount, testCase->opcodes, testCase->litCount,
      testCase->lits, testCase->funCount, funs);

  // Push "<script>" function onto the VM stack.
  push(&vm, OBJ_VAL(scriptFun));

  // Everything should be rooted to "<script>", so pop all temps.
  while (temps > 0) {
    popTemp(&vm.gc);
    temps--;
  }

  // Run the pushed "<script>" function.
  InterpretResult ires = interpretCall(&vm, (Obj*)scriptFun, 0);
  EXPECT_EQ((InterpretResult)testCase->ires, ires);

  // Check output/error.
  fflush(out.fptr);
  fflush(err.fptr);
  if (testCase->ires == INTERPRET_OK) {
    EXPECT_STREQ(testCase->msg, out.buf);
    EXPECT_STREQ("", err.buf);
  } else {
    const char* findMsg = strstr(err.buf, testCase->msg);
    if (testCase->msg && testCase->msg[0] && findMsg) {
      EXPECT_STRNEQ(testCase->msg, findMsg, strlen(testCase->msg));
    } else {
      EXPECT_STREQ(testCase->msg, err.buf);
    }
  }

  if (funs != NULL) {
    free(funs);
  }
  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

#define VM_TEST(name, data, count) \
  UTEST_I(VM, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

UTEST(VM, Empty) {
  MemBuf out, err;
  VM vm;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  EXPECT_EQ(0, vm.stackTop - vm.stack);

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

UTEST(VM, PushPop) {
  MemBuf out, err;
  VM vm;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  push(&vm, NUMBER_VAL(1.2));
  push(&vm, NUMBER_VAL(3.4));
  push(&vm, NUMBER_VAL(5.6));
  EXPECT_EQ(3, vm.stackTop - vm.stack);
  EXPECT_VALEQ(NUMBER_VAL(5.6), pop(&vm));
  EXPECT_VALEQ(NUMBER_VAL(3.4), pop(&vm));
  EXPECT_VALEQ(NUMBER_VAL(1.2), pop(&vm));
  EXPECT_EQ(0, vm.stackTop - vm.stack);

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

UTEST(VM, ManyConstants) {
  MemBuf out, err;
  VM vm;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  ObjFunction* scriptFun = newFunction(&vm.gc);
  pushTemp(&vm.gc, OBJ_VAL(scriptFun));

  // print 0 // (cont.)
  writeChunk(&vm.gc, &scriptFun->chunk, OP_CONSTANT, 1);
  writeChunk(&vm.gc, &scriptFun->chunk, 0, 1);
  writeChunk(&vm.gc, &scriptFun->chunk, 0, 1);
  // ... + 1 + 2 ... + 259 (cont.)
  for (int i = 1; i <= 259; ++i) {
    writeChunk(&vm.gc, &scriptFun->chunk, OP_ADD_C, i);
    writeChunk(&vm.gc, &scriptFun->chunk, (uint8_t)(i >> 8), i);
    writeChunk(&vm.gc, &scriptFun->chunk, (uint8_t)(i & 0xff), i);
  }
  // ... == (259 * 260 / 2);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_CONSTANT, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, (uint8_t)(259 >> 8), 260);
  writeChunk(&vm.gc, &scriptFun->chunk, (uint8_t)(259 & 0xff), 260);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_CONSTANT, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, (uint8_t)(260 >> 8), 260);
  writeChunk(&vm.gc, &scriptFun->chunk, (uint8_t)(260 & 0xff), 260);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_MULTIPLY, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_CONSTANT, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, 0, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, 2, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_DIVIDE, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_EQUAL, 260);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_PRINT, 261);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_NIL, 261);
  writeChunk(&vm.gc, &scriptFun->chunk, OP_RETURN, 261);

  // Add constants 0 through 260 with matched-up indices.
  for (int i = 0; i <= 260; ++i) {
    addConstant(&vm.gc, &scriptFun->chunk, NUMBER_VAL((double)i));
  }

  push(&vm, OBJ_VAL(scriptFun));

  popTemp(&vm.gc);

  InterpretResult ires = interpretCall(&vm, (Obj*)scriptFun, 0);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(out.fptr);
  fflush(err.fptr);
  EXPECT_STREQ("true\n", out.buf);
  EXPECT_STREQ("", err.buf);

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

VMCase unknownOp[] = {
  { INTERPRET_RUNTIME_ERROR, "Unknown opcode 255", LIST(LitFun),
      LIST(uint8_t, 255), LIST(Lit) },
};

VM_TEST(UnknownOp, unknownOp, 1);

VMCase printScript[] = {
  { INTERPRET_OK, "<script>\n", LIST(LitFun),
      LIST(uint8_t, OP_GET_LOCAL, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
};

VM_TEST(PrintScript, printScript, 1);

VMCase opConstant[] = {
  { INTERPRET_OK, "nil\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, B_FALSE) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, B_TRUE) },
  { INTERPRET_OK, "2.5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(2.5)) },
};

VM_TEST(OpConstant, opConstant, 4);

VMCase opLiterals[] = {
  { INTERPRET_OK, "nil\n", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_PRINT, OP_NIL, OP_RETURN), LIST(Lit) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_FALSE, OP_PRINT, OP_NIL, OP_RETURN), LIST(Lit) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_TRUE, OP_PRINT, OP_NIL, OP_RETURN), LIST(Lit) },
};

VM_TEST(OpLiterals, opLiterals, 3);

VMCase opPop[] = {
  { INTERPRET_OK, "0\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_POP,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), N(1.0)) },
};

VM_TEST(OpPop, opPop, 1);

VMCase opLocals[] = {
  { INTERPRET_OK, "false\ntrue\nfalse\ntrue\n", LIST(LitFun),
      LIST(uint8_t, OP_TRUE, OP_FALSE, OP_GET_LOCAL, 1, OP_GET_LOCAL, 2,
          OP_PRINT, OP_PRINT, OP_PRINT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "true\ntrue\n", LIST(LitFun),
      LIST(uint8_t, OP_FALSE, OP_TRUE, OP_SET_LOCAL, 1, OP_PRINT,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
};

VM_TEST(OpLocals, opLocals, 2);

VMCase opGlobals[] = {
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'foo'.", LIST(LitFun),
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo")) },
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'f1234567'.",
      LIST(LitFun),
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("f1234567")) },
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'foo'.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_SET_GLOBAL, 0, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo")) },
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'f1234567'.",
      LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_SET_GLOBAL, 0, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("f1234567")) },
  { INTERPRET_OK, "123\n456\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 1, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_CONSTANT, 0, 2,
          OP_SET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), N(123.0), N(456.0)) },
  { INTERPRET_OK, "123\n456\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 1, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_CONSTANT, 0, 2,
          OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("foo"), N(123.0), N(456.0)) },
};

VM_TEST(OpGlobals, opGlobals, 6);

VMCase opEqual[] = {
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_EQUAL, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_FALSE, OP_FALSE, OP_EQUAL, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_TRUE, OP_FALSE, OP_EQUAL, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_FALSE, OP_TRUE, OP_EQUAL, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_TRUE, OP_TRUE, OP_EQUAL, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_EQUAL, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0)) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_EQUAL,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(1.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_EQUAL,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
};

VM_TEST(OpEqual, opEqual, 8);

VMCase opGreater[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_GREATER, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_GREATER, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_GREATER, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_GREATER,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_GREATER,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(2.0), N(2.0)) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_GREATER,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpGreater, opGreater, 6);

VMCase opLess[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_LESS, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_LESS, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_LESS, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_LESS,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_LESS,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(2.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_LESS,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpLess, opLess, 6);

VMCase opLessC[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_LESS_C, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_LESS_C, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_LESS_C, 0, 0, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_LESS_C, 0, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_LESS_C, 0, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(2.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_LESS_C, 0, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpLessC, opLessC, 6);

VMCase opAdd[] = {
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(
          uint8_t, OP_NIL, OP_NIL, OP_ADD, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_ADD, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_ADD, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_ADD,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpAdd, opAdd, 4);

VMCase opAddConcat[] = {
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_ADD, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("")) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_ADD, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("")) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_ADD,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_ADD,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_ADD,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("foo")) },
  { INTERPRET_OK, "foobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_ADD,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), S("bar")) },
  { INTERPRET_OK, "foobarfoobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_ADD,
          OP_CONSTANT, 0, 2, OP_CONSTANT, 0, 3, OP_ADD, OP_ADD,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), S("bar"), S("foo"), S("bar")) },
};

VM_TEST(OpAddConcat, opAddConcat, 7);

VMCase opAddC[] = {
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(
          uint8_t, OP_NIL, OP_ADD_C, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(
          uint8_t, OP_NIL, OP_ADD_C, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_PRINT),
      LIST(Lit, N(0.0), NIL) },
  { INTERPRET_OK, "5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpAddC, opAddC, 4);

VMCase opAddCConcat[] = {
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S(""), NIL) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(
          uint8_t, OP_NIL, OP_ADD_C, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("")) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("foo"), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S(""), S("foo")) },
  { INTERPRET_OK, "foobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("foo"), S("bar")) },
  { INTERPRET_OK, "foobarfoobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_ADD_C, 0, 1, OP_CONSTANT, 0,
          2, OP_ADD_C, 0, 3, OP_ADD, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), S("bar"), S("foo"), S("bar")) },
};

VM_TEST(OpAddCConcat, opAddCConcat, 7);

VMCase opSubtract[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_SUBTRACT, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_SUBTRACT, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_SUBTRACT, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_SUBTRACT,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpSubtract, opSubtract, 4);

VMCase opSubtractC[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_SUBTRACT_C, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_SUBTRACT_C, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_SUBTRACT_C, 0, 1, OP_PRINT),
      LIST(Lit, N(0.0), NIL) },
  { INTERPRET_OK, "1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_SUBTRACT_C, 0, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpSubtractC, opSubtractC, 4);

VMCase opMultiply[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_MULTIPLY, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_MULTIPLY, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_MULTIPLY, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "6\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_MULTIPLY,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpMultiply, opMultiply, 4);

VMCase opDivide[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_DIVIDE, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_DIVIDE, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_DIVIDE, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "1.5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_DIVIDE,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpDivide, opDivide, 4);

VMCase opModulo[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_MODULO, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_MODULO, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_MODULO, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "1.1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_MODULO,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(5.1), N(2.0)) },
};

VM_TEST(OpModulo, opModulo, 4);

VMCase opNot[] = {
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NOT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_FALSE, OP_NOT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_TRUE, OP_NOT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NOT, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_TRUE, OP_NOT, OP_NOT, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
};

VM_TEST(OpNot, opNot, 5);

VMCase opNegate[] = {
  { INTERPRET_RUNTIME_ERROR, "Operand must be a number.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NEGATE, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit) },
  { INTERPRET_OK, "-1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NEGATE, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(1.0)) },
  { INTERPRET_OK, "1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NEGATE, OP_NEGATE, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0)) },
};

VM_TEST(OpNegate, opNegate, 3);

VMCase opJump[] = {
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_JUMP, 0, 2, OP_NIL, OP_PRINT, OP_TRUE, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit) },
};

VM_TEST(OpJump, opJump, 1);

VMCase opJumpIfFalse[] = {
  { INTERPRET_OK, "3\nnil\n1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_JUMP_IF_FALSE, 0, 3,
          OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2, OP_PRINT, OP_PRINT,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0), N(3.0)) },
  { INTERPRET_OK, "3\nfalse\n1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_FALSE, OP_JUMP_IF_FALSE, 0, 3,
          OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2, OP_PRINT, OP_PRINT,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0), N(3.0)) },
  { INTERPRET_OK, "3\n2\ntrue\n1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_TRUE, OP_JUMP_IF_FALSE, 0, 3,
          OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2, OP_PRINT, OP_PRINT,
          OP_PRINT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0), N(3.0)) },
};

VM_TEST(OpJumpIfFalse, opJumpIfFalse, 3);

VMCase opPjmpIfFalse[] = {
  { INTERPRET_OK, "3\n1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_PJMP_IF_FALSE, 0, 3,
          OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2, OP_PRINT, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0), N(3.0)) },
  { INTERPRET_OK, "3\n1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_FALSE, OP_PJMP_IF_FALSE, 0, 3,
          OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2, OP_PRINT, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0), N(3.0)) },
  { INTERPRET_OK, "3\n2\n1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_TRUE, OP_PJMP_IF_FALSE, 0, 3,
          OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2, OP_PRINT, OP_PRINT,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0), N(3.0)) },
};

VM_TEST(OpPjmpIfFalse, opPjmpIfFalse, 3);

VMCase opLoop[] = {
  { INTERPRET_OK, "0\n1\n2\n3\n4\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_GET_LOCAL, 1, OP_CONSTANT, 0,
          1, OP_LESS, OP_JUMP_IF_FALSE, 0, 16, OP_POP, OP_GET_LOCAL, 1,
          OP_PRINT, OP_GET_LOCAL, 1, OP_CONSTANT, 0, 2, OP_ADD,
          OP_SET_LOCAL, 1, OP_POP, OP_LOOP, 0, 25, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0), N(5.0), N(1.0)) },
};

VM_TEST(OpLoop, opLoop, 1);

VMCase opCall[] = {
  // OpCall
  { INTERPRET_OK, "(\na\n1\n2\nA\n)\n",
      LIST(LitFun,
          // fun b(n) { print n; return n + 1; }
          { "b", 1, 0,
              LIST(uint8_t, OP_GET_LOCAL, 1, OP_PRINT, OP_GET_LOCAL, 1,
                  OP_CONSTANT, 0, 0, OP_ADD, OP_RETURN),
              LIST(Lit, N(1.0)) },
          // fun a() { print "a"; print b(1); print "A"; }
          { "a", 0, 0,
              LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_CLOSURE, 0,
                  1, OP_CONSTANT, 0, 2, OP_CALL, 1, OP_PRINT,
                  OP_CONSTANT, 0, 3, OP_PRINT, OP_NIL, OP_RETURN),
              LIST(Lit, S("a"), F(0), N(1.0), S("A")) }),
      // print "("; a(); print ")";
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_CLOSURE, 0, 1,
          OP_CALL, 0, OP_POP, OP_CONSTANT, 0, 2, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("("), F(1), S(")")) },
  // OpCallUncallableNil
  { INTERPRET_RUNTIME_ERROR, "Can only call functions and classes.",
      LIST(LitFun),
      // nil();
      LIST(uint8_t, OP_NIL, OP_CALL, 0, OP_NIL, OP_RETURN), LIST(Lit) },
  // OpCallUncallableString
  { INTERPRET_RUNTIME_ERROR, "Can only call functions and classes.",
      LIST(LitFun),
      // "foo"();
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo")) },
  // OpCallWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun,
          // fun a() {}
          { "a", 0, 0, LIST(uint8_t, OP_NIL, OP_RETURN), LIST(Lit) }),
      // a(nil);
      LIST(uint8_t, OP_CLOSURE, 0, 0, OP_NIL, OP_CALL, 1, OP_NIL,
          OP_RETURN),
      LIST(Lit, F(0)) },
  // FunNameInErrorMsg
  { INTERPRET_RUNTIME_ERROR, "] in myFunction",
      LIST(LitFun,
          { "myFunction", 0, 0,
              LIST(uint8_t, OP_NIL, OP_CALL, 0, OP_POP, OP_NIL,
                  OP_RETURN),
              LIST(Lit) }),
      // myFunction();
      LIST(uint8_t, OP_CLOSURE, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, F(0)) },
};

VM_TEST(OpCall, opCall, 5);

VMCase closures[] = {
  // Closures1
  // {
  //   var x; var y = 2;
  //   fun f(a, b) {
  //     x = 1;
  //     fun g() { print a + b + x + y; }
  //     return g;
  //   }
  //   f(3, 4)();
  // }
  { INTERPRET_OK, "10\n",
      LIST(LitFun,
          { "g", 0, 4,
              LIST(uint8_t, OP_GET_UPVALUE, 0, OP_GET_UPVALUE, 1,
                  OP_ADD, OP_GET_UPVALUE, 2, OP_ADD, OP_GET_UPVALUE, 3,
                  OP_ADD, OP_PRINT, OP_NIL, OP_RETURN),
              LIST(Lit) },
          { "f", 2, 2,
              LIST(uint8_t, OP_CONSTANT, 0, 0, OP_SET_UPVALUE, 0,
                  OP_POP, OP_CLOSURE, 0, 1, 1, 1, 1, 2, 0, 0, 0, 1,
                  OP_GET_LOCAL, 3, OP_RETURN, OP_NIL, OP_RETURN),
              LIST(Lit, N(1.0), F(0)) }),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, 0, OP_CLOSURE, 0, 1, 1, 1,
          1, 2, OP_GET_LOCAL, 3, OP_CONSTANT, 0, 2, OP_CONSTANT, 0, 3,
          OP_CALL, 2, OP_CALL, 0, OP_POP, OP_POP, OP_CLOSE_UPVALUE,
          OP_CLOSE_UPVALUE, OP_NIL, OP_RETURN),
      LIST(Lit, N(2.0), F(1), N(3.0), N(4.0)) },
  // Closures2
  // var f; var g; var h;
  // {
  //   var x = "x"; var y = "y"; var z = "z";
  //   fun ff() { print z; } f = ff;
  //   fun gg() { print x; } g = gg;
  //   fun hh() { print y; } h = hh;
  // }
  // f(); g(); h();
  { INTERPRET_OK, "z\nx\ny\n",
      LIST(LitFun,
          { "ff", 0, 1,
              LIST(uint8_t, OP_GET_UPVALUE, 0, OP_PRINT, OP_NIL,
                  OP_RETURN),
              LIST(Lit) },
          { "gg", 0, 1,
              LIST(uint8_t, OP_GET_UPVALUE, 0, OP_PRINT, OP_NIL,
                  OP_RETURN),
              LIST(Lit) },
          { "hh", 0, 1,
              LIST(uint8_t, OP_GET_UPVALUE, 0, OP_PRINT, OP_NIL,
                  OP_RETURN),
              LIST(Lit) }),
      LIST(uint8_t, OP_NIL, OP_DEFINE_GLOBAL, 0, 0, OP_NIL,
          OP_DEFINE_GLOBAL, 0, 1, OP_NIL, OP_DEFINE_GLOBAL, 0, 2,
          OP_CONSTANT, 0, 3, OP_CONSTANT, 0, 4, OP_CONSTANT, 0, 5,
          OP_CLOSURE, 0, 6, 1, 3, OP_GET_LOCAL, 4, OP_SET_GLOBAL, 0, 7,
          OP_POP, OP_CLOSURE, 0, 8, 1, 1, OP_GET_LOCAL, 5,
          OP_SET_GLOBAL, 0, 9, OP_POP, OP_CLOSURE, 0, 10, 1, 2,
          OP_GET_LOCAL, 6, OP_SET_GLOBAL, 0, 11, OP_POP, OP_POP, OP_POP,
          OP_POP, OP_CLOSE_UPVALUE, OP_CLOSE_UPVALUE, OP_CLOSE_UPVALUE,
          OP_GET_GLOBAL, 0, 12, OP_CALL, 0, OP_POP, OP_GET_GLOBAL, 0,
          13, OP_CALL, 0, OP_POP, OP_GET_GLOBAL, 0, 14, OP_CALL, 0,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("f"), S("g"), S("h"), S("x"), S("y"), S("z"), F(0),
          S("f"), F(1), S("g"), F(2), S("h"), S("f"), S("g"), S("h")) },
};

VM_TEST(Closures, closures, 2);

VMCase stringsIndex[] = {
  // StringsIndexSimple
  { INTERPRET_OK, "97\n98\n99\n", LIST(LitFun),
      // var s="abc";print s[0];print s[1];print s[2];
      LIST(uint8_t, OP_CONSTANT, 0, 1, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 2, OP_GET_INDEX,
          OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 3,
          OP_GET_INDEX, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0,
          4, OP_GET_INDEX, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("s"), S("abc"), N(0.0), N(1.0), N(2.0)) },
  // StringsIndexGetOutOfBounds
  { INTERPRET_RUNTIME_ERROR, "String index (0) out of bounds (0).",
      LIST(LitFun),
      // ""[0];
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_GET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S(""), N(0.0)) },
  // StringsIndexGetBadNumber
  { INTERPRET_RUNTIME_ERROR,
      "String index (0.5) must be a whole number.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_GET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), N(0.5)) },
  // StringsIndexSetInvalid
  { INTERPRET_RUNTIME_ERROR,
      "Can only set index of lists, maps and instances.", LIST(LitFun),
      // "a"["x"] = 1;
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_CONSTANT,
          0, 2, OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), S("x"), N(1.0)) },
};

VM_TEST(StringsIndex, stringsIndex, 4);

VMCase stringsParseNum[] = {
  // StringsParseNumSimple
  { INTERPRET_OK, "-123.456\n", LIST(LitFun),
      // print " -123.456 ".parsenum();
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_INVOKE, 0, 1, 0, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S(" -123.456 "), S("parsenum")) },
  // StringsParseNumWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // "".parsenum(nil);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_INVOKE, 0, 1, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("parsenum")) },
  // StringsParseNumFailure
  { INTERPRET_OK, "nil\n", LIST(LitFun),
      // print "123 z".parsenum();
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_INVOKE, 0, 1, 0, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("123 z"), S("parsenum")) },
};

VM_TEST(StringsParseNum, stringsParseNum, 3);

VMCase stringsSize[] = {
  // StringsSizeSimple1
  { INTERPRET_OK, "11\n", LIST(LitFun),
      // print "hello world".size();
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_INVOKE, 0, 1, 0, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("hello world"), S("size")) },
  // StringsSizeSimple2
  { INTERPRET_OK, "11\n", LIST(LitFun),
      // var s = "hello world".size; print s();
      LIST(uint8_t, OP_CONSTANT, 0, 1, OP_GET_PROPERTY, 0, 2,
          OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("s"), S("hello world"), S("size")) },
  // StringsSizeWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // "".size(nil);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_INVOKE, 0, 1, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("size")) },
};

VM_TEST(StringsSize, stringsSize, 3);

VMCase stringsSubstr[] = {
  // StringsSubstrTrivial
  { INTERPRET_OK, "true\n", LIST(LitFun),
      // print "".substr(0, 0) == "";
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2, OP_CONSTANT,
          0, 2, OP_INVOKE, 0, 1, 2, OP_CONSTANT, 0, 0, OP_EQUAL,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("substr"), N(0.0)) },
  // StringsSubstrSimple
  { INTERPRET_OK, "hello\n", LIST(LitFun),
      // print "hello".substr(0, -1);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2, OP_CONSTANT,
          0, 3, OP_NEGATE, OP_INVOKE, 0, 1, 2, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("hello"), S("substr"), N(0.0), N(1.0)) },
  // StringsSubstrMulti
  { INTERPRET_OK, "hello\nworld\n", LIST(LitFun),
      // var msg="hello world";
      // print msg.substr(0, 5); print msg.substr(-6, -1);
      LIST(uint8_t, OP_CONSTANT, 0, 1, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 3, OP_CONSTANT, 0, 4,
          OP_INVOKE, 0, 2, 2, OP_PRINT, OP_GET_GLOBAL, 0, 0,
          OP_CONSTANT, 0, 5, OP_NEGATE, OP_CONSTANT, 0, 6, OP_NEGATE,
          OP_INVOKE, 0, 2, 2, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("msg"), S("hello world"), S("substr"), N(0.0), N(5.0),
          N(6.0), N(1.0)) },
  // StringsSubstrStartIntMin
  { INTERPRET_OK, "ello\n", LIST(LitFun),
      // print "hello".substr(1, 2147483647);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2, OP_CONSTANT,
          0, 3, OP_INVOKE, 0, 1, 2, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("hello"), S("substr"), N(1.0), N(2147483647.0)) },
  // StringsSubstrEndIntMax
  { INTERPRET_OK, "hell\n", LIST(LitFun),
      // print "hello".substr(-2147483648, -2);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2, OP_CONSTANT,
          0, 3, OP_INVOKE, 0, 1, 2, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("hello"), S("substr"), N(-2147483648.0), N(-2.0)) },
  // StringsSubstrWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 2 arguments but got 0.",
      LIST(LitFun),
      // "".substr();
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_INVOKE, 0, 1, 0, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("substr")) },
  // StringsSubstrStartNonNumber
  { INTERPRET_RUNTIME_ERROR, "Start must be a number.", LIST(LitFun),
      // "".substr(nil, 0);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_CONSTANT, 0, 2,
          OP_INVOKE, 0, 1, 2, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("substr"), N(0.0)) },
  // StringsSubstrEndNonNumber
  { INTERPRET_RUNTIME_ERROR, "End must be a number.", LIST(LitFun),
      // "".substr(0, nil);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2, OP_NIL,
          OP_INVOKE, 0, 1, 2, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("substr"), N(0.0)) },
  // StringsSubstrStartBadNumber
  { INTERPRET_RUNTIME_ERROR, "Start (0.5) must be a whole number.",
      LIST(LitFun),
      // "a".substr(0.5, 1);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2, OP_CONSTANT,
          0, 3, OP_INVOKE, 0, 1, 2, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), S("substr"), N(0.5), N(1.0)) },
  // StringsSubstrEndBadNumber
  { INTERPRET_RUNTIME_ERROR, "End (0.5) must be a whole number.",
      LIST(LitFun),
      // "a".substr(0, 0.5);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2, OP_CONSTANT,
          0, 3, OP_INVOKE, 0, 1, 2, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), S("substr"), N(0.0), N(0.5)) },
};

VM_TEST(StringsSubstr, stringsSubstr, 10);

VMCase classesIndex[] = {
  // ClassesIndexSimple
  { INTERPRET_OK, "1\n", LIST(LitFun),
      // class F{} var f = F(); f["x"] = 1; print f["x"];
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_DEFINE_GLOBAL, 0, 1, OP_GET_GLOBAL, 0, 1, OP_CONSTANT, 0,
          2, OP_CONSTANT, 0, 3, OP_SET_INDEX, OP_POP, OP_GET_GLOBAL, 0,
          1, OP_CONSTANT, 0, 2, OP_GET_INDEX, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("F"), S("f"), S("x"), N(1.0)) },
  // ClassesIndexUndefined
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x'.", LIST(LitFun),
      // class F{} F()["x"];
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_CONSTANT, 0, 1, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("x")) },
  // ClassesIndexInvalidGet1
  { INTERPRET_RUNTIME_ERROR,
      "Can only index lists, maps, strings and instances.",
      LIST(LitFun),
      // 0["x"];
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_GET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("x")) },
  // ClassesIndexInvalidGet2
  { INTERPRET_RUNTIME_ERROR,
      "Can only index lists, maps, strings and instances.",
      LIST(LitFun),
      // class F{}F["x"];
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CONSTANT,
          0, 1, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("x")) },
  // ClassesIndexInvalidSet1
  { INTERPRET_RUNTIME_ERROR,
      "Can only set index of lists, maps and instances.", LIST(LitFun),
      // 0["x"] = 1;
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1, OP_CONSTANT,
          0, 2, OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("x"), N(1.0)) },
  // ClassesIndexInstanceGetBadIndex1
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} F()[0];
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_CONSTANT, 0, 1, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), N(0.0)) },
  // ClassesIndexInstanceGetBadIndex2
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} F()[F()];
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_GET_INDEX, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("F")) },
  // ClassesIndexInstanceSetBadIndex1
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} var f = F(); f[0] = 1;
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_DEFINE_GLOBAL, 0, 1, OP_GET_GLOBAL, 0, 1, OP_CONSTANT, 0,
          2, OP_CONSTANT, 0, 3, OP_SET_INDEX, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("F"), S("f"), N(0.0), N(1.0)) },
  // ClassesIndexInstanceSetBadIndex2
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} var f = F(); f[F()] = 1;
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_DEFINE_GLOBAL, 0, 1, OP_GET_GLOBAL, 0, 1, OP_GET_GLOBAL, 0,
          0, OP_CALL, 0, OP_CONSTANT, 0, 2, OP_SET_INDEX, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("f"), N(1.0)) },
};

VM_TEST(ClassesIndex, classesIndex, 9);

VMCase classes[] = {
  // ClassesPrint
  { INTERPRET_OK, "A1234567\nA1234567 instance\n", LIST(LitFun),
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 1, OP_POP, OP_GET_GLOBAL, 0, 2, OP_PRINT,
          OP_GET_GLOBAL, 0, 3, OP_CALL, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("A1234567"), S("A1234567"), S("A1234567"),
          S("A1234567")) },
  // ClassesSimple
  { INTERPRET_OK, "1\n", LIST(LitFun),
      // class F{} var f = F(); f.x = 1; print f.x;
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 2, OP_CALL, 0, OP_DEFINE_GLOBAL, 0, 1,
          OP_GET_GLOBAL, 0, 3, OP_CONSTANT, 0, 5, OP_SET_PROPERTY, 0, 4,
          OP_POP, OP_GET_GLOBAL, 0, 6, OP_GET_PROPERTY, 0, 7, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("f"), S("F"), S("f"), S("x"), N(1.0), S("f"),
          S("x")) },
  // ClassesInitWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // class F{} F(nil);
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 1, OP_POP, OP_GET_GLOBAL, 0, 2, OP_NIL,
          OP_CALL, 1, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("F"), S("F")) },
  // ClassesGetMissing
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x'.", LIST(LitFun),
      // class F{} var f = F(); f.x;
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 2, OP_CALL, 0, OP_DEFINE_GLOBAL, 0, 1,
          OP_GET_GLOBAL, 0, 3, OP_GET_PROPERTY, 0, 4, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("F"), S("f"), S("F"), S("f"), S("x")) },
  // ClassesGetNonInstance
  { INTERPRET_RUNTIME_ERROR,
      "Only lists and instances have properties.", LIST(LitFun),
      // 0.x;
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_GET_PROPERTY, 0, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("x")) },
  // ClassesSetNonInstance
  { INTERPRET_RUNTIME_ERROR, "Only instances have fields.",
      LIST(LitFun),
      // 0.x = 1;
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 2,
          OP_SET_PROPERTY, 0, 1, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("x"), N(1.0)) },
};

VM_TEST(Classes, classes, 6);

VMCase classesMethods[] = {
  // ClassesMethods
  // class F {
  //   init(n) { this.n = n; }
  //   get() { return this.n; }
  //   set(nn) { this.n = nn; }
  // }
  // var f = F(1); print f.get();  // 1
  // f.set(2); print f.get();      // 2
  // var g = f.get; var s = f.set;
  // print g();                    // 2
  // s(3); print g();              // 3
  { INTERPRET_OK, "1\n2\n2\n3\n",
      LIST(LitFun,
          { "init", 1, 0,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_LOCAL, 1,
                  OP_SET_PROPERTY, 0, 0, OP_POP, OP_GET_LOCAL, 0,
                  OP_RETURN),
              LIST(Lit, S("n")) },
          { "get", 0, 0,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_PROPERTY, 0, 0,
                  OP_RETURN),
              LIST(Lit, S("n")) },
          { "set", 1, 0,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_LOCAL, 1,
                  OP_SET_PROPERTY, 0, 0, OP_POP, OP_NIL, OP_RETURN),
              LIST(Lit, S("n")) }),
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 1, OP_CLOSURE, 0, 3, OP_METHOD, 0, 2,
          OP_CLOSURE, 0, 5, OP_METHOD, 0, 4, OP_CLOSURE, 0, 7,
          OP_METHOD, 0, 6, OP_POP, OP_GET_GLOBAL, 0, 9, OP_CONSTANT, 0,
          10, OP_CALL, 1, OP_DEFINE_GLOBAL, 0, 8, OP_GET_GLOBAL, 0, 11,
          OP_INVOKE, 0, 12, 0, OP_PRINT, OP_GET_GLOBAL, 0, 13,
          OP_CONSTANT, 0, 15, OP_INVOKE, 0, 14, 1, OP_POP,
          OP_GET_GLOBAL, 0, 16, OP_INVOKE, 0, 17, 0, OP_PRINT,
          OP_GET_GLOBAL, 0, 19, OP_GET_PROPERTY, 0, 20,
          OP_DEFINE_GLOBAL, 0, 18, OP_GET_GLOBAL, 0, 22,
          OP_GET_PROPERTY, 0, 23, OP_DEFINE_GLOBAL, 0, 21,
          OP_GET_GLOBAL, 0, 24, OP_CALL, 0, OP_PRINT, OP_GET_GLOBAL, 0,
          25, OP_CONSTANT, 0, 26, OP_CALL, 1, OP_POP, OP_GET_GLOBAL, 0,
          27, OP_CALL, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("F"), S("init"), F(0), S("get"), F(1),
          S("set"), F(2), S("f"), S("F"), N(1.0), S("f"), S("get"),
          S("f"), S("set"), N(2.0), S("f"), S("get"), S("g"), S("f"),
          S("get"), S("s"), S("f"), S("set"), S("g"), S("s"), N(3.0),
          S("g")) },
  // ClassesInvokeField
  { INTERPRET_OK, "1\n",
      LIST(LitFun,
          // fun blah() { print 1; }
          { "blah", 0, 0,
              LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL,
                  OP_RETURN),
              LIST(Lit, N(1.0)) }),
      // class F{} var f = F(); f.blah = blah; f.blah();
      LIST(uint8_t, OP_CLOSURE, 0, 1, OP_DEFINE_GLOBAL, 0, 0, OP_CLASS,
          0, 2, OP_DEFINE_GLOBAL, 0, 2, OP_GET_GLOBAL, 0, 3, OP_POP,
          OP_GET_GLOBAL, 0, 5, OP_CALL, 0, OP_DEFINE_GLOBAL, 0, 4,
          OP_GET_GLOBAL, 0, 6, OP_GET_GLOBAL, 0, 8, OP_SET_PROPERTY, 0,
          7, OP_POP, OP_GET_GLOBAL, 0, 9, OP_INVOKE, 0, 10, 0, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("blah"), F(0), S("F"), S("F"), S("f"), S("F"), S("f"),
          S("blah"), S("blah"), S("f"), S("blah")) },
  // ClassesInvokeNonInstance
  { INTERPRET_RUNTIME_ERROR,
      "Only lists, maps, strings and instances have methods.",
      LIST(LitFun),
      // 0.x();
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_INVOKE, 0, 1, 0, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("x")) },
  // ClassesInvokeMissing
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'oops'.", LIST(LitFun),
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 1, OP_POP, OP_GET_GLOBAL, 0, 3, OP_CALL, 0,
          OP_DEFINE_GLOBAL, 0, 2, OP_GET_GLOBAL, 0, 4, OP_INVOKE, 0, 5,
          0, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("F"), S("f"), S("F"), S("f"), S("oops")) },
};

VM_TEST(ClassesMethods, classesMethods, 4);

VMCase classesSuper[] = {
  // ClassesSuper
  // class A {
  //   f() { print 1; }
  //   g() { print 2; }
  //   h() { print 3; }
  // }
  // class B < A {
  //   g() { print 4; super.g(); }
  //   h() { var Ah = super.h; print 5; Ah(); }
  // }
  // var b = B(); b.f(); b.g(); b.h();
  { INTERPRET_OK, "1\n4\n2\n5\n3\n",
      LIST(LitFun,
          { "f", 0, 0,
              LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL,
                  OP_RETURN),
              LIST(Lit, N(1.0)) },
          { "g", 0, 0,
              LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL,
                  OP_RETURN),
              LIST(Lit, N(2.0)) },
          { "h", 0, 0,
              LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_NIL,
                  OP_RETURN),
              LIST(Lit, N(3.0)) },
          { "g", 0, 1,
              LIST(uint8_t, OP_CONSTANT, 0, 0, OP_PRINT, OP_GET_LOCAL,
                  0, OP_GET_UPVALUE, 0, OP_SUPER_INVOKE, 0, 1, 0,
                  OP_POP, OP_NIL, OP_RETURN),
              LIST(Lit, N(4.0), S("g")) },
          { "h", 0, 1,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_UPVALUE, 0,
                  OP_GET_SUPER, 0, 0, OP_CONSTANT, 0, 1, OP_PRINT,
                  OP_GET_LOCAL, 1, OP_CALL, 0, OP_POP, OP_NIL,
                  OP_RETURN),
              LIST(Lit, S("h"), N(5.0)) }),
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 1, OP_CLOSURE, 0, 3, OP_METHOD, 0, 2,
          OP_CLOSURE, 0, 5, OP_METHOD, 0, 4, OP_CLOSURE, 0, 7,
          OP_METHOD, 0, 6, OP_POP, OP_CLASS, 0, 8, OP_DEFINE_GLOBAL, 0,
          8, OP_GET_GLOBAL, 0, 9, OP_GET_GLOBAL, 0, 10, OP_INHERIT,
          OP_GET_GLOBAL, 0, 11, OP_CLOSURE, 0, 13, 1, 1, OP_METHOD, 0,
          12, OP_CLOSURE, 0, 15, 1, 1, OP_METHOD, 0, 14, OP_POP,
          OP_CLOSE_UPVALUE, OP_GET_GLOBAL, 0, 17, OP_CALL, 0,
          OP_DEFINE_GLOBAL, 0, 16, OP_GET_GLOBAL, 0, 18, OP_INVOKE, 0,
          19, 0, OP_POP, OP_GET_GLOBAL, 0, 20, OP_INVOKE, 0, 21, 0,
          OP_POP, OP_GET_GLOBAL, 0, 22, OP_INVOKE, 0, 23, 0, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("A"), S("A"), S("f"), F(0), S("g"), F(1), S("h"),
          F(2), S("B"), S("A"), S("B"), S("B"), S("g"), F(3), S("h"),
          F(4), S("b"), S("B"), S("b"), S("f"), S("b"), S("g"), S("b"),
          S("h")) },
  // ClassesSuperNonClass
  { INTERPRET_RUNTIME_ERROR, "Superclass must be a class.",
      LIST(LitFun),
      // { var A; class B < A {} }
      LIST(uint8_t, OP_NIL, OP_CLASS, 0, 0, OP_GET_LOCAL, 1,
          OP_GET_LOCAL, 2, OP_INHERIT, OP_GET_LOCAL, 2, OP_POP, OP_POP,
          OP_POP, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("B")) },
  // ClassesSuperInvokeMissing
  // {
  //   class A {}
  //   class B < A { f() { super.f(); } }
  //   B().f();
  // }
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'f'.",
      LIST(LitFun,
          { "f", 0, 1,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_UPVALUE, 0,
                  OP_SUPER_INVOKE, 0, 0, 0, OP_POP, OP_NIL, OP_RETURN),
              LIST(Lit, S("f")) }),
      LIST(uint8_t, OP_CLASS, 0, 0, OP_GET_LOCAL, 1, OP_POP, OP_CLASS,
          0, 1, OP_GET_LOCAL, 1, OP_GET_LOCAL, 2, OP_INHERIT,
          OP_GET_LOCAL, 2, OP_CLOSURE, 0, 3, 1, 3, OP_METHOD, 0, 2,
          OP_POP, OP_CLOSE_UPVALUE, OP_GET_LOCAL, 2, OP_CALL, 0,
          OP_INVOKE, 0, 4, 0, OP_POP, OP_POP, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("A"), S("B"), S("f"), F(0), S("f")) },
  // ClassesSuperGetMissing
  // {
  //   class A {}
  //   class B < A { f() { super.f; } }
  //   B().f();
  // }
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'f'.",
      LIST(LitFun,
          { "f", 0, 1,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_UPVALUE, 0,
                  OP_GET_SUPER, 0, 0, OP_POP, OP_NIL, OP_RETURN),
              LIST(Lit, S("f")) }),
      LIST(uint8_t, OP_CLASS, 0, 0, OP_GET_LOCAL, 1, OP_POP, OP_CLASS,
          0, 1, OP_GET_LOCAL, 1, OP_GET_LOCAL, 2, OP_INHERIT,
          OP_GET_LOCAL, 2, OP_CLOSURE, 0, 3, 1, 3, OP_METHOD, 0, 2,
          OP_POP, OP_CLOSE_UPVALUE, OP_GET_LOCAL, 2, OP_CALL, 0,
          OP_INVOKE, 0, 4, 0, OP_POP, OP_POP, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("A"), S("B"), S("f"), F(0), S("f")) },
};

VM_TEST(ClassesSuper, classesSuper, 4);

VMCase nativeArgc[] = {
  // NativeArgcTrivial
  { INTERPRET_OK, "0\n", LIST(LitFun),
      // print argc();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("argc")) },
  // NativeArgcWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // argc(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("argc")) },
};

VM_TEST(NativeArgc, nativeArgc, 2);

VMCase nativeArgv[] = {
  // NativeArgvWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // argv();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("argv")) },
  // NativeArgvNotNumber
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.", LIST(LitFun),
      // argv(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("argv")) },
  // NativeArgvOutOfBounds
  { INTERPRET_RUNTIME_ERROR, "Argument (1) out of bounds (0).",
      LIST(LitFun),
      // argv(1);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("argv"), N(1.0)) },
};

VM_TEST(NativeArgv, nativeArgv, 3);

UTEST(VM, NativeArgs) {
  MemBuf out, err;
  VM vm;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  const char* args[] = { "clox", "foo", "bar", "baz", NULL };
  argsVM(&vm, ARRAY_SIZE(args), args);

  size_t temps = 0;

  ObjFunction* scriptFun = newFunction(&vm.gc);
  pushTemp(&vm.gc, OBJ_VAL(scriptFun));
  temps++;
  temps += fillFun(&vm.gc, &vm.strings, scriptFun,
      // for(var i=0;i<argc();i=i+1)print argv(i);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_GET_LOCAL, 1, OP_GET_GLOBAL,
          0, 1, OP_CALL, 0, OP_LESS, OP_PJMP_IF_FALSE, 0, 25, OP_JUMP,
          0, 11, OP_GET_LOCAL, 1, OP_ADD_C, 0, 2, OP_SET_LOCAL, 1,
          OP_POP, OP_LOOP, 0, 25, OP_GET_GLOBAL, 0, 3, OP_GET_LOCAL, 1,
          OP_CALL, 1, OP_PRINT, OP_LOOP, 0, 22, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0), S("argc"), N(1.0), S("argv")),
      LIST(ObjFunction*));

  push(&vm, OBJ_VAL(scriptFun));

  while (temps > 0) {
    popTemp(&vm.gc);
    temps--;
  }

  InterpretResult ires = interpretCall(&vm, (Obj*)scriptFun, 0);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(out.fptr);
  fflush(err.fptr);
  EXPECT_STREQ("clox\nfoo\nbar\nbaz\n\n", out.buf);
  EXPECT_STREQ("", err.buf);

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

VMCase nativeCeil[] = {
  // NativeCeilSimple
  { INTERPRET_OK, "2\n", LIST(LitFun),
      // print ceil(1.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("ceil"), N(1.5)) },
  // NativeCeilWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // ceil();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("ceil")) },
  // NativeCeilNotNumber
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.", LIST(LitFun),
      // ceil(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("ceil")) },
};

VM_TEST(NativeCeil, nativeCeil, 3);

VMCase nativeChr[] = {
  // NativeChrSimple1
  { INTERPRET_OK, "a\n", LIST(LitFun),
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("chr"), N(97.0)) },
  // NativeChrSimple2
  { INTERPRET_OK, "1\n", LIST(LitFun),
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_INVOKE, 0, 2, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("chr"), N(0.0), S("size")) },
  // NativeChrWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // chr();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("chr")) },
  // NativeChrNotNumber
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.", LIST(LitFun),
      // chr(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("chr")) },
  // NativeChrNumberTooLow
  { INTERPRET_RUNTIME_ERROR, "Argument (-129) must be between ",
      LIST(LitFun),
      // chr(-129);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("chr"), N(-129.0)) },
  // NativeChrNumberTooHigh
  { INTERPRET_RUNTIME_ERROR, "Argument (256) must be between ",
      LIST(LitFun),
      // chr(256);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("chr"), N(256.0)) },
  // NativeChrBadNumber
  { INTERPRET_RUNTIME_ERROR, "Argument (0.5) must be a whole number.",
      LIST(LitFun),
      // chr(0.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("chr"), N(0.5)) },
};

VM_TEST(NativeChr, nativeChr, 7);

VMCase nativeClock[] = {
  // NativeClockSimple
  { INTERPRET_OK, "true\n", LIST(LitFun),
      // print clock() >= 0;
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_CONSTANT, 0, 1,
          OP_LESS, OP_NOT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("clock"), N(0.0)) },
  // NativeClockWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // clock(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("clock")) },
};

VM_TEST(NativeClock, nativeClock, 2);

VMCase nativeEprint[] = {
  // NativeEprintToStdErr
  { INTERPRET_RUNTIME_ERROR, "123.456\n", LIST(LitFun),
      // eprint(123.546); eprint();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("eprint"), N(123.456)) },
  // NativeEprintWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // eprint();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("eprint")) },
};

VM_TEST(NativeEprint, nativeEprint, 2);

VMCase nativeExit[] = {
  // NativeExitWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // exit();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("exit")) },
  // NativeExitNotNumber
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.", LIST(LitFun),
      // exit(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("exit")) },
  // NativeExitNumberTooLow
  { INTERPRET_RUNTIME_ERROR, "Argument (-1) must be between 0 and ",
      LIST(LitFun),
      // exit(-1);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_NEGATE,
          OP_CALL, 1, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("exit"), N(1.0)) },
  // NativeExitNumberTooHigh
  { INTERPRET_RUNTIME_ERROR, "Argument (1e+10) must be between 0 and ",
      LIST(LitFun),
      // exit(10000000000);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("exit"), N(10000000000.0)) },
  // NativeExitBadNumber
  { INTERPRET_RUNTIME_ERROR, "Argument (0.5) must be a whole number.",
      LIST(LitFun),
      // exit(0.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("exit"), N(0.5)) },
};

VM_TEST(NativeExit, nativeExit, 5);

VMCase nativeFloor[] = {
  // NativeFloorSimple
  { INTERPRET_OK, "1\n", LIST(LitFun),
      // print floor(1.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("floor"), N(1.5)) },
  // NativeFloorWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // floor();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("floor")) },
  // NativeFloorNotNumber
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.", LIST(LitFun),
      // floor(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("floor")) },
};

VM_TEST(NativeFloor, nativeFloor, 3);

VMCase nativeRound[] = {
  // NativeRoundSimple
  { INTERPRET_OK, "2\n", LIST(LitFun),
      // print round(1.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("round"), N(1.5)) },
  // NativeRoundWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // round();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("round")) },
  // NativeRoundNotNumber
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.", LIST(LitFun),
      // round(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("round")) },
};

VM_TEST(NativeRound, nativeRound, 3);

VMCase nativeStr[] = {
  // NativeStrSimple
  { INTERPRET_OK, "hi1\n", LIST(LitFun),
      // print "hi" + str(1);
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_GET_GLOBAL, 0, 1, OP_CONSTANT,
          0, 2, OP_CALL, 1, OP_ADD, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("hi"), S("str"), N(1.0)) },
  // NativeStrWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // str();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("str")) },
};

VM_TEST(NativeStr, nativeStr, 2);

VMCase nativeType[] = {
  // NativeTypeWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // type();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("type")) },
  // NativeTypeValues
  { INTERPRET_OK, "boolean\nnil\nnumber\n", LIST(LitFun),
      // print type(true); print type(nil); print type(0);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_TRUE, OP_CALL, 1, OP_PRINT,
          OP_GET_GLOBAL, 0, 0, OP_NIL, OP_CALL, 1, OP_PRINT,
          OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("type"), N(0.0)) },
  // NativeTypeObjects
  { INTERPRET_OK, "string\nlist\nmap\n", LIST(LitFun),
      // print type(""); print type([]); print type({});
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_LIST_INIT, OP_CALL, 1,
          OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_MAP_INIT, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("type"), S("")) },
  // NativeTypeFunctionClosure
  // fun f() {} print type(f);
  // { var x; fun g() { x; } print type(g); }
  { INTERPRET_OK, "function\nfunction\n",
      LIST(LitFun,
          { "f", 0, 0, LIST(uint8_t, OP_NIL, OP_RETURN), LIST(Lit) },
          { "g", 0, 1,
              LIST(uint8_t, OP_GET_UPVALUE, 0, OP_POP, OP_NIL,
                  OP_RETURN),
              LIST(Lit) }),
      LIST(uint8_t, OP_CONSTANT, 0, 1, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 2, OP_GET_GLOBAL, 0, 0, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_CLOSURE, 0, 3, 1, 1, OP_GET_GLOBAL, 0, 2,
          OP_GET_LOCAL, 2, OP_CALL, 1, OP_PRINT, OP_POP,
          OP_CLOSE_UPVALUE, OP_NIL, OP_RETURN),
      LIST(Lit, S("f"), F(0), S("type"), F(1)) },
  // NativeTypeNative
  { INTERPRET_OK, "native function\n", LIST(LitFun),
      // print type(type);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0, OP_CALL,
          1, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("type")) },
  // NativeTypeClass
  // class F { m() {} }
  // print type(F); print type(F()); print type(F().m);
  { INTERPRET_OK, "class\ninstance\nfunction\n",
      LIST(LitFun,
          { "m", 0, 0, LIST(uint8_t, OP_NIL, OP_RETURN), LIST(Lit) }),
      LIST(uint8_t, OP_CLASS, 0, 0, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 2, OP_METHOD, 0, 1,
          OP_POP, OP_GET_GLOBAL, 0, 3, OP_GET_GLOBAL, 0, 0, OP_CALL, 1,
          OP_PRINT, OP_GET_GLOBAL, 0, 3, OP_GET_GLOBAL, 0, 0, OP_CALL,
          0, OP_CALL, 1, OP_PRINT, OP_GET_GLOBAL, 0, 3, OP_GET_GLOBAL,
          0, 0, OP_CALL, 0, OP_GET_PROPERTY, 0, 1, OP_CALL, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("m"), F(0), S("type")) },
};

VM_TEST(NativeType, nativeType, 6);

UTEST(VM, NativeTypeUpvalue) {
  MemBuf out, err;
  VM vm;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  size_t temps = 0;

  ObjFunction* scriptFun = newFunction(&vm.gc);
  pushTemp(&vm.gc, OBJ_VAL(scriptFun));
  temps++;
  temps += fillFun(&vm.gc, &vm.strings, scriptFun,
      // print type(<upvalue>);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("type")), LIST(ObjFunction*));

  ObjUpvalue* upvalue = newUpvalue(&vm.gc, NULL);
  pushTemp(&vm.gc, OBJ_VAL(upvalue));
  temps++;
  addConstant(&vm.gc, &scriptFun->chunk, OBJ_VAL(upvalue));

  push(&vm, OBJ_VAL(scriptFun));

  while (temps > 0) {
    popTemp(&vm.gc);
    temps--;
  }

  InterpretResult ires = interpretCall(&vm, (Obj*)scriptFun, 0);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  fflush(out.fptr);
  fflush(err.fptr);
  EXPECT_STREQ("upvalue\n", out.buf);
  EXPECT_STREQ("", err.buf);

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

VMCase lists[] = {
  // ListsDataNonList1
  { INTERPRET_RUNTIME_ERROR, "List data can only be added to a list.",
      LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_LIST_DATA, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  // ListsDataNonList2
  { INTERPRET_RUNTIME_ERROR, "List data can only be added to a list.",
      LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_LIST_DATA, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("foo")) },
  // ListsGetIndexSimple
  { INTERPRET_OK, "nil\n", LIST(LitFun),
      // print [nil][0];
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_CONSTANT, 0,
          0, OP_GET_INDEX, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  // ListsSetIndexSimple
  { INTERPRET_OK, "0\n1\n1\n", LIST(LitFun),
      // var l=0;print l[0];print l[0]=1;print l[0];
      LIST(uint8_t, OP_LIST_INIT, OP_CONSTANT, 0, 1, OP_LIST_DATA,
          OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0,
          1, OP_GET_INDEX, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_CONSTANT,
          0, 1, OP_CONSTANT, 0, 2, OP_SET_INDEX, OP_PRINT,
          OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_GET_INDEX,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("l"), N(0.0), N(1.0)) },
  // ListsGetIndexNonNumber
  { INTERPRET_RUNTIME_ERROR, "List index must be a number.",
      LIST(LitFun),
      // [nil][nil];
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_NIL,
          OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit) },
  // ListsSetIndexNonNumber
  { INTERPRET_RUNTIME_ERROR, "List index must be a number.",
      LIST(LitFun),
      // [nil][nil]=nil;
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_NIL, OP_NIL,
          OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit) },
  // ListsGetIndexGetOutOfBounds1
  { INTERPRET_RUNTIME_ERROR, "List index (-1) out of bounds (1).",
      LIST(LitFun),
      // [nil][-1];
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_CONSTANT, 0,
          0, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(-1.0)) },
  // ListsSetIndexGetOutOfBounds1
  { INTERPRET_RUNTIME_ERROR, "List index (-1) out of bounds (1).",
      LIST(LitFun),
      // [nil][-1]=nil;
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_CONSTANT, 0,
          0, OP_NIL, OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(-1.0)) },
  // ListsGetIndexGetOutOfBounds2
  { INTERPRET_RUNTIME_ERROR, "List index (1) out of bounds (1).",
      LIST(LitFun),
      // [nil][1];
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_CONSTANT, 0,
          0, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0)) },
  // ListsSetIndexGetOutOfBounds2
  { INTERPRET_RUNTIME_ERROR, "List index (1) out of bounds (1).",
      LIST(LitFun),
      // [nil][1]=nil;
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_CONSTANT, 0,
          0, OP_NIL, OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0)) },
  // ListsGetIndexGetBadNumber
  { INTERPRET_RUNTIME_ERROR, "List index (0.5) must be a whole number.",
      LIST(LitFun),
      // [nil][0.5];
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_CONSTANT, 0,
          0, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.5)) },
  // ListsSetIndexGetBadNumber
  { INTERPRET_RUNTIME_ERROR, "List index (0.5) must be a whole number.",
      LIST(LitFun),
      // [nil][0.5]=nil;
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_CONSTANT, 0,
          0, OP_NIL, OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.5)) },
  // ListsInsertSimple
  { INTERPRET_OK, "[123, 456]\n", LIST(LitFun),
      // var l=[123];l.insert(0,456);print l;
      LIST(uint8_t, OP_LIST_INIT, OP_CONSTANT, 0, 1, OP_LIST_DATA,
          OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0,
          3, OP_CONSTANT, 0, 4, OP_INVOKE, 0, 2, 2, OP_POP,
          OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("l"), N(456.0), S("insert"), N(0.0), N(123.0)) },
  // ListsInsertBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 2 arguments but got 0.",
      LIST(LitFun),
      // [].insert();
      LIST(uint8_t, OP_LIST_INIT, OP_INVOKE, 0, 0, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("insert")) },
  // ListsInsertBadIndex
  { INTERPRET_RUNTIME_ERROR, "List index must be a number.",
      LIST(LitFun),
      // [].insert(nil,nil);
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_NIL, OP_INVOKE, 0, 0, 2,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("insert")) },
  // ListsPopSimple
  { INTERPRET_OK, "[123, 456]\n456\n[123]\n", LIST(LitFun),
      // var l=[123,456];print l;print l.pop();print l;
      LIST(uint8_t, OP_LIST_INIT, OP_CONSTANT, 0, 1, OP_LIST_DATA,
          OP_CONSTANT, 0, 2, OP_LIST_DATA, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_INVOKE,
          0, 3, 0, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("l"), N(123.0), N(456.0), S("pop")) },
  // ListsPopBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // [].pop(nil);
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_INVOKE, 0, 0, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("pop")) },
  // ListsPopEmpty
  { INTERPRET_RUNTIME_ERROR, "Can't pop from an empty list.",
      LIST(LitFun),
      // [].pop();
      LIST(uint8_t, OP_LIST_INIT, OP_INVOKE, 0, 0, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("pop")) },
  // ListsPushSimple
  { INTERPRET_OK, "[]\n[123]\n[123, 456]\n", LIST(LitFun),
      // var l=[];print l;l.push(123);print l;l.push(456);print l;
      LIST(uint8_t, OP_LIST_INIT, OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL,
          0, 0, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 2,
          OP_INVOKE, 0, 1, 1, OP_POP, OP_GET_GLOBAL, 0, 0, OP_PRINT,
          OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 3, OP_INVOKE, 0, 1, 1,
          OP_POP, OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("l"), S("push"), N(123.0), N(456.0)) },
  // ListsPushBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // [].push();
      LIST(uint8_t, OP_LIST_INIT, OP_INVOKE, 0, 0, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("push")) },
  // ListsRemoveSimple
  { INTERPRET_OK, "[123, 456, 789]\n456\n[123, 789]\n", LIST(LitFun),
      // var l=[123,456,789];print l;print l.remove(1);print l;
      LIST(uint8_t, OP_LIST_INIT, OP_CONSTANT, 0, 1, OP_LIST_DATA,
          OP_CONSTANT, 0, 2, OP_LIST_DATA, OP_CONSTANT, 0, 3,
          OP_LIST_DATA, OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0,
          OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 5, OP_INVOKE,
          0, 4, 1, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("l"), N(123.0), N(456.0), N(789.0), S("remove"),
          N(1.0)) },
  // ListsRemoveBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // [].remove();
      LIST(uint8_t, OP_LIST_INIT, OP_INVOKE, 0, 0, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("remove")) },
  // ListsRemoveBadIndex
  { INTERPRET_RUNTIME_ERROR, "List index (1) out of bounds (0).",
      LIST(LitFun),
      // [].remove(1);
      LIST(uint8_t, OP_LIST_INIT, OP_CONSTANT, 0, 1, OP_INVOKE, 0, 0, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("remove"), N(1.0)) },
  // ListsSizeSimple1
  { INTERPRET_OK, "3\n", LIST(LitFun),
      // print [nil,nil,nil].size();
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_NIL,
          OP_LIST_DATA, OP_NIL, OP_LIST_DATA, OP_INVOKE, 0, 0, 0,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("size")) },
  // ListsSizeSimple2
  { INTERPRET_OK, "3\n", LIST(LitFun),
      // var s=[nil,nil,nil].size;print s();
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_LIST_DATA, OP_NIL,
          OP_LIST_DATA, OP_NIL, OP_LIST_DATA, OP_GET_PROPERTY, 0, 1,
          OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("s"), S("size")) },
  // ListsSizeBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // [].size(nil);
      LIST(uint8_t, OP_LIST_INIT, OP_NIL, OP_INVOKE, 0, 0, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("size")) },
};

VM_TEST(Lists, lists, 26);

VMCase maps[] = {
  // MapsDataNonMap1
  { INTERPRET_RUNTIME_ERROR, "Map data can only be added to a map.",
      LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_NIL, OP_MAP_DATA, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  // MapsDataNonMap2
  { INTERPRET_RUNTIME_ERROR, "Map data can only be added to a map.",
      LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, 0, OP_NIL, OP_NIL, OP_MAP_DATA,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo")) },
  // MapsDataNonStringKey1
  { INTERPRET_RUNTIME_ERROR, "Map key must be a string.", LIST(LitFun),
      // ({[nil]:nil});
      LIST(uint8_t, OP_MAP_INIT, OP_NIL, OP_NIL, OP_MAP_DATA, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit) },
  // MapsDataNonStringKey2
  { INTERPRET_RUNTIME_ERROR, "Map key must be a string.", LIST(LitFun),
      // ({[{}]:nil});
      LIST(uint8_t, OP_MAP_INIT, OP_MAP_INIT, OP_NIL, OP_MAP_DATA,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit) },
  // MapsGetIndexSimple
  { INTERPRET_OK, "1\n", LIST(LitFun),
      // print{a:1}["a"];
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1,
          OP_MAP_DATA, OP_CONSTANT, 0, 0, OP_GET_INDEX, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), N(1.0)) },
  // MapsSetIndexSimple
  { INTERPRET_OK, "{}\n1\n{a: 1}\n", LIST(LitFun),
      // var m={};print m;print m["a"]=1;print m;
      LIST(uint8_t, OP_MAP_INIT, OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL,
          0, 0, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1,
          OP_CONSTANT, 0, 2, OP_SET_INDEX, OP_PRINT, OP_GET_GLOBAL, 0,
          0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("m"), S("a"), N(1.0)) },
  // MapsGetIndexNonString1
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}[nil]);
      LIST(uint8_t, OP_MAP_INIT, OP_NIL, OP_GET_INDEX, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  // MapsGetIndexNonString2
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}[{}]);
      LIST(uint8_t, OP_MAP_INIT, OP_MAP_INIT, OP_GET_INDEX, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit) },
  // MapsSetIndexNonString1
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}[nil]=nil);
      LIST(uint8_t, OP_MAP_INIT, OP_NIL, OP_NIL, OP_SET_INDEX, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit) },
  // MapsSetIndexNonString2
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}[{}]=nil);
      LIST(uint8_t, OP_MAP_INIT, OP_MAP_INIT, OP_NIL, OP_SET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit) },
  // MapsGetIndexMissing
  { INTERPRET_RUNTIME_ERROR, "Undefined key 'a'.", LIST(LitFun),
      // ({}["a"]);
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 0, OP_GET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("a")) },
  // MapsCountSimple
  { INTERPRET_OK, "3\n", LIST(LitFun),
      // print{a:4,b:5,c:6}.count();
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1,
          OP_MAP_DATA, OP_CONSTANT, 0, 2, OP_CONSTANT, 0, 3,
          OP_MAP_DATA, OP_CONSTANT, 0, 4, OP_CONSTANT, 0, 5,
          OP_MAP_DATA, OP_INVOKE, 0, 6, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), N(4.0), S("b"), N(5.0), S("c"), N(6.0),
          S("count")) },
  // MapsCountBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // ({}).count(nil);
      LIST(uint8_t, OP_MAP_INIT, OP_NIL, OP_INVOKE, 0, 0, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("count")) },
  // MapsHasSimple1
  { INTERPRET_OK, "true\n", LIST(LitFun),
      // print{a:1}.has("a");
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1,
          OP_MAP_DATA, OP_CONSTANT, 0, 0, OP_INVOKE, 0, 2, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), N(1.0), S("has")) },
  // MapsHasSimple2
  { INTERPRET_OK, "false\n", LIST(LitFun),
      // print{a:1}.has("b");
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1,
          OP_MAP_DATA, OP_CONSTANT, 0, 3, OP_INVOKE, 0, 2, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), N(1.0), S("has"), S("b")) },
  // MapsHasSimple3
  { INTERPRET_OK, "true\n", LIST(LitFun),
      // var mh={a:1}.has;print mh("a");
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2,
          OP_MAP_DATA, OP_GET_PROPERTY, 0, 3, OP_DEFINE_GLOBAL, 0, 0,
          OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_CALL, 1, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("mh"), S("a"), N(1.0), S("has")) },
  // MapsHasBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // ({}).has();
      LIST(uint8_t, OP_MAP_INIT, OP_INVOKE, 0, 0, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("has")) },
  // MapsHasNonStringKey1
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}).has(nil);
      LIST(uint8_t, OP_MAP_INIT, OP_NIL, OP_INVOKE, 0, 0, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("has")) },
  // MapsHasNonStringKey2
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}).has({});
      LIST(uint8_t, OP_MAP_INIT, OP_MAP_INIT, OP_INVOKE, 0, 0, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("has")) },
  // MapsKeysSimple
  { INTERPRET_OK, "3\n", LIST(LitFun),
      // print{a:4,b:5,c:6}.keys().size();
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 0, OP_CONSTANT, 0, 1,
          OP_MAP_DATA, OP_CONSTANT, 0, 2, OP_CONSTANT, 0, 3,
          OP_MAP_DATA, OP_CONSTANT, 0, 4, OP_CONSTANT, 0, 5,
          OP_MAP_DATA, OP_INVOKE, 0, 6, 0, OP_INVOKE, 0, 7, 0, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), N(4.0), S("b"), N(5.0), S("c"), N(6.0),
          S("keys"), S("size")) },
  // MapsKeysBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // ({}).keys(nil);
      LIST(uint8_t, OP_MAP_INIT, OP_NIL, OP_INVOKE, 0, 0, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("keys")) },
  // MapsRemoveSimple1
  { INTERPRET_OK, "false\n", LIST(LitFun),
      // print{}.remove("");
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 1, OP_INVOKE, 0, 0, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("remove"), S("")) },
  // MapsRemoveSimple2
  { INTERPRET_OK, "{a: 1, b: 2}\ntrue\n{b: 2}\n", LIST(LitFun),
      LIST(uint8_t, OP_MAP_INIT, OP_CONSTANT, 0, 1, OP_CONSTANT, 0, 2,
          OP_MAP_DATA, OP_CONSTANT, 0, 3, OP_CONSTANT, 0, 4,
          OP_MAP_DATA, OP_DEFINE_GLOBAL, 0, 0, OP_GET_GLOBAL, 0, 0,
          OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 0, 1, OP_INVOKE,
          0, 5, 1, OP_PRINT, OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("m"), S("a"), N(1.0), S("b"), N(2.0), S("remove")) },
  // MapsRemoveBadArity
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // ({}).remove();
      LIST(uint8_t, OP_MAP_INIT, OP_INVOKE, 0, 0, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("remove")) },
  // MapsRemoveNonStringKey1
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}).remove(nil);
      LIST(uint8_t, OP_MAP_INIT, OP_NIL, OP_INVOKE, 0, 0, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("remove")) },
  // MapsRemoveNonStringKey2
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      LIST(LitFun),
      // ({}).remove({});
      LIST(uint8_t, OP_MAP_INIT, OP_MAP_INIT, OP_INVOKE, 0, 0, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("remove")) },
};

VM_TEST(Maps, maps, 26);

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
