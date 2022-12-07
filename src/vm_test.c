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
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, B_FALSE) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, B_TRUE) },
  { INTERPRET_OK, "2.5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
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
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_POP, OP_PRINT,
          OP_NIL, OP_RETURN),
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
      LIST(uint8_t, OP_CONSTANT, 1, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL,
          0, 0, OP_PRINT, OP_CONSTANT, 2, OP_SET_GLOBAL, 0, 0, OP_POP,
          OP_GET_GLOBAL, 0, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), N(123.0), N(456.0)) },
};

VM_TEST(OpGlobals, opGlobals, 5);

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
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_EQUAL, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(1.0)) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_EQUAL, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(1.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_EQUAL, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
};

VM_TEST(OpEqual, opEqual, 8);

VMCase opGreater[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_GREATER, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_GREATER, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_GREATER, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(2.0), N(2.0)) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
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
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_LESS, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_LESS, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(2.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpLess, opLess, 6);

VMCase opLessC[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_LESS_C, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_LESS_C, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_LESS_C, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_OK, "true\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_LESS_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(1.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_LESS_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(2.0), N(2.0)) },
  { INTERPRET_OK, "false\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_LESS_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
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
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_ADD, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpAdd, opAdd, 4);

VMCase opAddConcat[] = {
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_ADD, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("")) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("")) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S(""), S("foo")) },
  { INTERPRET_OK, "foobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), S("bar")) },
  { INTERPRET_OK, "foobarfoobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_CONSTANT, 3, OP_ADD, OP_ADD, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("foo"), S("bar"), S("foo"), S("bar")) },
};

VM_TEST(OpAddConcat, opAddConcat, 7);

VMCase opAddC[] = {
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_ADD_C, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_ADD_C, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_PRINT),
      LIST(Lit, N(0.0), NIL) },
  { INTERPRET_OK, "5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpAddC, opAddC, 4);

VMCase opAddCConcat[] = {
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S(""), NIL) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_ADD_C, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("")) },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("foo"), S("")) },
  { INTERPRET_OK, "foo\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S(""), S("foo")) },
  { INTERPRET_OK, "foobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("foo"), S("bar")) },
  { INTERPRET_OK, "foobarfoobar\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_ADD_C, 1, OP_CONSTANT, 2,
          OP_ADD_C, 3, OP_ADD, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo"), S("bar"), S("foo"), S("bar")) },
};

VM_TEST(OpAddCConcat, opAddCConcat, 7);

VMCase opSubtract[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_SUBTRACT, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_SUBTRACT, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_SUBTRACT, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpSubtract, opSubtract, 4);

VMCase opSubtractC[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_SUBTRACT_C, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, NIL) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_SUBTRACT_C, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_SUBTRACT_C, 1, OP_PRINT),
      LIST(Lit, N(0.0), NIL) },
  { INTERPRET_OK, "1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_SUBTRACT_C, 1, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpSubtractC, opSubtractC, 4);

VMCase opMultiply[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_MULTIPLY, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_MULTIPLY, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_MULTIPLY, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "6\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
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
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_DIVIDE, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_DIVIDE, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "1.5\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(3.0), N(2.0)) },
};

VM_TEST(OpDivide, opDivide, 4);

VMCase opModulo[] = {
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_NIL, OP_MODULO, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_MODULO, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0)) },
  { INTERPRET_RUNTIME_ERROR, "Operands must be numbers.", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_MODULO, OP_PRINT),
      LIST(Lit, N(0.0)) },
  { INTERPRET_OK, "1.1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MODULO, OP_PRINT,
          OP_NIL, OP_RETURN),
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
      LIST(
          uint8_t, OP_CONSTANT, 0, OP_NOT, OP_PRINT, OP_NIL, OP_RETURN),
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
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_PRINT, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(1.0)) },
  { INTERPRET_OK, "1\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_PRINT,
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
  { INTERPRET_OK, "0\n2\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_JUMP_IF_FALSE,
          0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT, 2, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), N(1.0), N(2.0)) },
  { INTERPRET_OK, "0\n2\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_FALSE,
          OP_JUMP_IF_FALSE, 0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT,
          2, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), N(1.0), N(2.0)) },
  { INTERPRET_OK, "0\n1\n2\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_TRUE, OP_JUMP_IF_FALSE,
          0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT, 2, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), N(1.0), N(2.0)) },
};

VM_TEST(OpJumpIfFalse, opJumpIfFalse, 3);

VMCase opLoop[] = {
  { INTERPRET_OK, "0\n1\n2\n3\n4\n", LIST(LitFun),
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_CONSTANT, 1,
          OP_LESS, OP_JUMP_IF_FALSE, 0, 15, OP_POP, OP_GET_LOCAL, 1,
          OP_PRINT, OP_GET_LOCAL, 1, OP_CONSTANT, 2, OP_ADD,
          OP_SET_LOCAL, 1, OP_POP, OP_LOOP, 0, 23, OP_POP, OP_NIL,
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
                  OP_CONSTANT, 0, OP_ADD, OP_RETURN),
              LIST(Lit, N(1.0)) },
          // fun a() { print "a"; print b(1); print "A"; }
          { "a", 0, 0,
              LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_CLOSURE, 1,
                  OP_CONSTANT, 2, OP_CALL, 1, OP_PRINT, OP_CONSTANT, 3,
                  OP_PRINT, OP_NIL, OP_RETURN),
              LIST(Lit, S("a"), F(0), N(1.0), S("A")) }),
      // print "("; a(); print ")";
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_CLOSURE, 1, OP_CALL, 0,
          OP_POP, OP_CONSTANT, 2, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("("), F(1), S(")")) },
  // OpCallClock
  { INTERPRET_OK, "true\n", LIST(LitFun),
      // print clock() >= 0;
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_CONSTANT, 1,
          OP_LESS, OP_NOT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("clock"), N(0.0)) },
  // OpCallStr
  { INTERPRET_OK, "hi1\n", LIST(LitFun),
      // print "hi" + str(1);
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_GLOBAL, 1, 0, OP_CONSTANT, 2,
          OP_CALL, 1, OP_ADD, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("hi"), S("str"), N(1.0)) },
  // OpCallCeil
  { INTERPRET_OK, "2\n", LIST(LitFun),
      // print ceil(1.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("ceil"), N(1.5)) },
  // OpCallFloor
  { INTERPRET_OK, "1\n", LIST(LitFun),
      // print floor(1.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("floor"), N(1.5)) },
  // OpCallRound
  { INTERPRET_OK, "2\n", LIST(LitFun),
      // print round(1.5);
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CONSTANT, 1, OP_CALL, 1,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("round"), N(1.5)) },
  // OpCallUncallableNil
  { INTERPRET_RUNTIME_ERROR, "Can only call functions and classes.",
      LIST(LitFun),
      // nil();
      LIST(uint8_t, OP_NIL, OP_CALL, 0, OP_NIL, OP_RETURN), LIST(Lit) },
  // OpCallUncallableString
  { INTERPRET_RUNTIME_ERROR, "Can only call functions and classes.",
      LIST(LitFun),
      // "foo"();
      LIST(uint8_t, OP_CONSTANT, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("foo")) },
  // OpCallWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun,
          // fun a() {}
          { "a", 0, 0, LIST(uint8_t, OP_NIL, OP_RETURN), LIST(Lit) }),
      // a(nil);
      LIST(uint8_t, OP_CLOSURE, 0, OP_NIL, OP_CALL, 1, OP_NIL,
          OP_RETURN),
      LIST(Lit, F(0)) },
  // OpCallClockWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // clock(nil);
      LIST(uint8_t, OP_GET_GLOBAL, 0, OP_NIL, OP_CALL, 1, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("clock")) },
  // OpCallStrWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // str();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("str")) },
  // OpCallCeilWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // ceil();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("ceil")) },
  // OpCallFloorWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // floor();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("floor")) },
  // OpCallRoundWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      LIST(LitFun),
      // ceil();
      LIST(uint8_t, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, S("round")) },
  // FunNameInErrorMsg
  { INTERPRET_RUNTIME_ERROR, "] in myFunction",
      LIST(LitFun,
          { "myFunction", 0, 0,
              LIST(uint8_t, OP_NIL, OP_CALL, 0, OP_POP, OP_NIL,
                  OP_RETURN),
              LIST(Lit) }),
      // myFunction();
      LIST(uint8_t, OP_CLOSURE, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Lit, F(0)) },
};

VM_TEST(OpCall, opCall, 15);

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
              LIST(uint8_t, OP_CONSTANT, 0, OP_SET_UPVALUE, 0, OP_POP,
                  OP_CLOSURE, 1, 1, 1, 1, 2, 0, 0, 0, 1, OP_GET_LOCAL,
                  3, OP_RETURN, OP_NIL, OP_RETURN),
              LIST(Lit, N(1.0), F(0)) }),
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_CLOSURE, 1, 1, 1, 1, 2,
          OP_GET_LOCAL, 3, OP_CONSTANT, 2, OP_CONSTANT, 3, OP_CALL, 2,
          OP_CALL, 0, OP_POP, OP_POP, OP_CLOSE_UPVALUE,
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
      LIST(uint8_t, OP_NIL, OP_DEFINE_GLOBAL, 0, OP_NIL,
          OP_DEFINE_GLOBAL, 1, OP_NIL, OP_DEFINE_GLOBAL, 2, OP_CONSTANT,
          3, OP_CONSTANT, 4, OP_CONSTANT, 5, OP_CLOSURE, 6, 1, 3,
          OP_GET_LOCAL, 4, OP_SET_GLOBAL, 7, 0, OP_POP, OP_CLOSURE, 8,
          1, 1, OP_GET_LOCAL, 5, OP_SET_GLOBAL, 9, 0, OP_POP,
          OP_CLOSURE, 10, 1, 2, OP_GET_LOCAL, 6, OP_SET_GLOBAL, 11, 0,
          OP_POP, OP_POP, OP_POP, OP_POP, OP_CLOSE_UPVALUE,
          OP_CLOSE_UPVALUE, OP_CLOSE_UPVALUE, OP_GET_GLOBAL, 12, 0,
          OP_CALL, 0, OP_POP, OP_GET_GLOBAL, 13, 0, OP_CALL, 0, OP_POP,
          OP_GET_GLOBAL, 14, 0, OP_CALL, 0, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("f"), S("g"), S("h"), S("x"), S("y"), S("z"), F(0),
          S("f"), F(1), S("g"), F(2), S("h"), S("f"), S("g"), S("h")) },
};

VM_TEST(Closures, closures, 2);

VMCase index_[] = {
  // IndexClassSimple
  { INTERPRET_OK, "1\n", LIST(LitFun),
      // class F{} var f = F(); f["x"] = 1; print f["x"];
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 0,
          0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_DEFINE_GLOBAL,
          1, OP_GET_GLOBAL, 1, 0, OP_CONSTANT, 2, OP_CONSTANT, 3,
          OP_SET_INDEX, OP_POP, OP_GET_GLOBAL, 1, 0, OP_CONSTANT, 2,
          OP_GET_INDEX, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("f"), S("x"), N(1.0)) },
  // IndexClassUndefined
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x'.", LIST(LitFun),
      // class F{} F()["x"];
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 0,
          0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_CONSTANT, 1,
          OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("x")) },
  // IndexInvalidGet1
  { INTERPRET_RUNTIME_ERROR, "Only instances have properties.",
      LIST(LitFun),
      // 0["x"];
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("x")) },
  // IndexInvalidGet2
  { INTERPRET_RUNTIME_ERROR, "Only instances have properties.",
      LIST(LitFun),
      // "a"["x"];
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), S("x")) },
  // IndexInvalidSet1
  { INTERPRET_RUNTIME_ERROR, "Only instances have fields.",
      LIST(LitFun),
      // 0["x"] = 1;
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
          OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, N(0.0), S("x"), N(1.0)) },
  // IndexInvalidSet2
  { INTERPRET_RUNTIME_ERROR, "Only instances have fields.",
      LIST(LitFun),
      // "a"["x"] = 1;
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
          OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("a"), S("x"), N(1.0)) },
  // IndexInstanceGetBadIndex1
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} F()[0];
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 0,
          0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_CONSTANT, 1,
          OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), N(0.0)) },
  // IndexInstanceGetBadIndex2
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} F()[F()];
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 0,
          0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_GET_GLOBAL, 0,
          0, OP_CALL, 0, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F")) },
  // IndexInstanceSetBadIndex1
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} var f = F(); f[0] = 1;
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 0,
          0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_DEFINE_GLOBAL,
          1, OP_GET_GLOBAL, 1, 0, OP_CONSTANT, 2, OP_CONSTANT, 3,
          OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("f"), N(0.0), N(1.0)) },
  // IndexInstanceSetBadIndex2
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      LIST(LitFun),
      // class F{} var f = F(); f[F()] = 1;
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 0,
          0, OP_POP, OP_GET_GLOBAL, 0, 0, OP_CALL, 0, OP_DEFINE_GLOBAL,
          1, OP_GET_GLOBAL, 1, 0, OP_GET_GLOBAL, 0, 0, OP_CALL, 0,
          OP_CONSTANT, 2, OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("f"), N(1.0)) },
};

VM_TEST(Index, index_, 10);

VMCase classes[] = {
  // ClassesPrint
  { INTERPRET_OK, "A1234567\nA1234567 instance\n", LIST(LitFun),
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 1,
          0, OP_POP, OP_GET_GLOBAL, 2, 0, OP_PRINT, OP_GET_GLOBAL, 3, 0,
          OP_CALL, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("A1234567"), S("A1234567"), S("A1234567"),
          S("A1234567")) },
  // ClassesSimple
  { INTERPRET_OK, "1\n", LIST(LitFun),
      // class F{} var f = F(); f.x = 1; print f.x;
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          0, OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3, 0,
          OP_CONSTANT, 5, OP_SET_PROPERTY, 4, OP_POP, OP_GET_GLOBAL, 6,
          0, OP_GET_PROPERTY, 7, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("f"), S("F"), S("f"), S("x"), N(1.0), S("f"),
          S("x")) },
  // ClassesInitWrongNumArgs
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      LIST(LitFun),
      // class F{} F(nil);
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 1,
          0, OP_POP, OP_GET_GLOBAL, 2, 0, OP_NIL, OP_CALL, 1, OP_POP,
          OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("F"), S("F")) },
  // ClassesGetMissing
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x'.", LIST(LitFun),
      // class F{} var f = F(); f.x;
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          0, OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3, 0,
          OP_GET_PROPERTY, 4, OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("F"), S("f"), S("F"), S("f"), S("x")) },
  // ClassesGetNonInstance
  { INTERPRET_RUNTIME_ERROR, "Only instances have properties.",
      LIST(LitFun),
      // 0.x;
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_PROPERTY, 1, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0), S("x")) },
  // ClassesSetNonInstance
  { INTERPRET_RUNTIME_ERROR, "Only instances have fields.",
      LIST(LitFun),
      // 0.x = 1;
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 2, OP_SET_PROPERTY, 1,
          OP_POP, OP_NIL, OP_RETURN),
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
                  OP_SET_PROPERTY, 0, OP_POP, OP_GET_LOCAL, 0,
                  OP_RETURN),
              LIST(Lit, S("n")) },
          { "get", 0, 0,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_PROPERTY, 0,
                  OP_RETURN),
              LIST(Lit, S("n")) },
          { "set", 1, 0,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_LOCAL, 1,
                  OP_SET_PROPERTY, 0, OP_POP, OP_NIL, OP_RETURN),
              LIST(Lit, S("n")) }),
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 1,
          0, OP_CLOSURE, 3, OP_METHOD, 2, OP_CLOSURE, 5, OP_METHOD, 4,
          OP_CLOSURE, 7, OP_METHOD, 6, OP_POP, OP_GET_GLOBAL, 9, 0,
          OP_CONSTANT, 10, OP_CALL, 1, OP_DEFINE_GLOBAL, 8,
          OP_GET_GLOBAL, 11, 0, OP_INVOKE, 12, 0, OP_PRINT,
          OP_GET_GLOBAL, 13, 0, OP_CONSTANT, 15, OP_INVOKE, 14, 1,
          OP_POP, OP_GET_GLOBAL, 16, 0, OP_INVOKE, 17, 0, OP_PRINT,
          OP_GET_GLOBAL, 19, 0, OP_GET_PROPERTY, 20, OP_DEFINE_GLOBAL,
          18, OP_GET_GLOBAL, 22, 0, OP_GET_PROPERTY, 23,
          OP_DEFINE_GLOBAL, 21, OP_GET_GLOBAL, 24, 0, OP_CALL, 0,
          OP_PRINT, OP_GET_GLOBAL, 25, 0, OP_CONSTANT, 26, OP_CALL, 1,
          OP_POP, OP_GET_GLOBAL, 27, 0, OP_CALL, 0, OP_PRINT, OP_NIL,
          OP_RETURN),
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
              LIST(
                  uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
              LIST(Lit, N(1.0)) }),
      // class F{} var f = F(); f.blah = blah; f.blah();
      LIST(uint8_t, OP_CLOSURE, 1, OP_DEFINE_GLOBAL, 0, OP_CLASS, 2,
          OP_DEFINE_GLOBAL, 2, OP_GET_GLOBAL, 3, 0, OP_POP,
          OP_GET_GLOBAL, 5, 0, OP_CALL, 0, OP_DEFINE_GLOBAL, 4,
          OP_GET_GLOBAL, 6, 0, OP_GET_GLOBAL, 8, 0, OP_SET_PROPERTY, 7,
          OP_POP, OP_GET_GLOBAL, 9, 0, OP_INVOKE, 10, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("blah"), F(0), S("F"), S("F"), S("f"), S("F"), S("f"),
          S("blah"), S("blah"), S("f"), S("blah")) },
  // ClassesInvokeNonInstance
  { INTERPRET_RUNTIME_ERROR, "Only instances have methods.",
      LIST(LitFun),
      // 0.x();
      LIST(uint8_t, OP_CONSTANT, 0, OP_INVOKE, 1, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, N(0.0), S("x")) },
  // ClassesInvokeMissing
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'oops'.", LIST(LitFun),
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 1,
          0, OP_POP, OP_GET_GLOBAL, 3, 0, OP_CALL, 0, OP_DEFINE_GLOBAL,
          2, OP_GET_GLOBAL, 4, 0, OP_INVOKE, 5, 0, OP_POP, OP_NIL,
          OP_RETURN),
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
              LIST(
                  uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
              LIST(Lit, N(1.0)) },
          { "g", 0, 0,
              LIST(
                  uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
              LIST(Lit, N(2.0)) },
          { "h", 0, 0,
              LIST(
                  uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_RETURN),
              LIST(Lit, N(3.0)) },
          { "g", 0, 1,
              LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_GET_LOCAL, 0,
                  OP_GET_UPVALUE, 0, OP_SUPER_INVOKE, 1, 0, OP_POP,
                  OP_NIL, OP_RETURN),
              LIST(Lit, N(4.0), S("g")) },
          { "h", 0, 1,
              LIST(uint8_t, OP_GET_LOCAL, 0, OP_GET_UPVALUE, 0,
                  OP_GET_SUPER, 0, OP_CONSTANT, 1, OP_PRINT,
                  OP_GET_LOCAL, 1, OP_CALL, 0, OP_POP, OP_NIL,
                  OP_RETURN),
              LIST(Lit, S("h"), N(5.0)) }),
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 1,
          0, OP_CLOSURE, 3, OP_METHOD, 2, OP_CLOSURE, 5, OP_METHOD, 4,
          OP_CLOSURE, 7, OP_METHOD, 6, OP_POP, OP_CLASS, 8,
          OP_DEFINE_GLOBAL, 8, OP_GET_GLOBAL, 9, 0, OP_GET_GLOBAL, 10,
          0, OP_INHERIT, OP_GET_GLOBAL, 11, 0, OP_CLOSURE, 13, 1, 1,
          OP_METHOD, 12, OP_CLOSURE, 15, 1, 1, OP_METHOD, 14, OP_POP,
          OP_CLOSE_UPVALUE, OP_GET_GLOBAL, 17, 0, OP_CALL, 0,
          OP_DEFINE_GLOBAL, 16, OP_GET_GLOBAL, 18, 0, OP_INVOKE, 19, 0,
          OP_POP, OP_GET_GLOBAL, 20, 0, OP_INVOKE, 21, 0, OP_POP,
          OP_GET_GLOBAL, 22, 0, OP_INVOKE, 23, 0, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Lit, S("A"), S("A"), S("f"), F(0), S("g"), F(1), S("h"),
          F(2), S("B"), S("A"), S("B"), S("B"), S("g"), F(3), S("h"),
          F(4), S("b"), S("B"), S("b"), S("f"), S("b"), S("g"), S("b"),
          S("h")) },
  // ClassesSuperNonClass
  { INTERPRET_RUNTIME_ERROR, "Superclass must be a class.",
      LIST(LitFun),
      // { var A; class B < A {} }
      LIST(uint8_t, OP_NIL, OP_CLASS, 0, OP_GET_LOCAL, 1, OP_GET_LOCAL,
          2, OP_INHERIT, OP_GET_LOCAL, 2, OP_POP, OP_POP, OP_POP,
          OP_POP, OP_NIL, OP_RETURN),
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
                  OP_SUPER_INVOKE, 0, 0, OP_POP, OP_NIL, OP_RETURN),
              LIST(Lit, S("f")) }),
      LIST(uint8_t, OP_CLASS, 0, OP_GET_LOCAL, 1, OP_POP, OP_CLASS, 1,
          OP_GET_LOCAL, 1, OP_GET_LOCAL, 2, OP_INHERIT, OP_GET_LOCAL, 2,
          OP_CLOSURE, 3, 1, 3, OP_METHOD, 2, OP_POP, OP_CLOSE_UPVALUE,
          OP_GET_LOCAL, 2, OP_CALL, 0, OP_INVOKE, 4, 0, OP_POP, OP_POP,
          OP_POP, OP_NIL, OP_RETURN),
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
                  OP_GET_SUPER, 0, OP_POP, OP_NIL, OP_RETURN),
              LIST(Lit, S("f")) }),
      LIST(uint8_t, OP_CLASS, 0, OP_GET_LOCAL, 1, OP_POP, OP_CLASS, 1,
          OP_GET_LOCAL, 1, OP_GET_LOCAL, 2, OP_INHERIT, OP_GET_LOCAL, 2,
          OP_CLOSURE, 3, 1, 3, OP_METHOD, 2, OP_POP, OP_CLOSE_UPVALUE,
          OP_GET_LOCAL, 2, OP_CALL, 0, OP_INVOKE, 4, 0, OP_POP, OP_POP,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Lit, S("A"), S("B"), S("f"), F(0), S("f")) },
};

VM_TEST(ClassesSuper, classesSuper, 4);

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
