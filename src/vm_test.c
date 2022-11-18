#include "vm.h"

#include <assert.h>
#include <string.h>

#include "utest.h"

#include "chunk.h"
#include "gc.h"
#include "list.h"
#include "membuf.h"
#include "memory.h"
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

static size_t fillChunk(Chunk* chunk, GC* gc, Table* strings,
    size_t numOps, uint8_t* ops, size_t numVals, Value* vals) {
  size_t temps = 0;

  for (size_t i = 0; i < numOps; ++i) {
    writeChunk(gc, chunk, ops[i], 1);
  }
  for (size_t i = 0; i < numVals; ++i) {
    if (IS_STRING(vals[i])) {
      // Ensure strings are interned correctly.
      ObjString* valStr = AS_STRING(vals[i]);
      ObjString* valStrCopy =
          copyString(gc, strings, valStr->chars, valStr->length);
      Value v = OBJ_VAL(valStrCopy);
      pushTemp(gc, v);
      temps++;
      addConstant(gc, chunk, v);
    } else {
      addConstant(gc, chunk, vals[i]);
    }
  }

  return temps;
}

struct VMSimple {
  MemBuf out;
  MemBuf err;
  VM vm;
};

UTEST_F_SETUP(VMSimple) {
  initMemBuf(&ufx->out);
  initMemBuf(&ufx->err);
  initVM(&ufx->vm, ufx->out.fptr, ufx->err.fptr);
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(VMSimple) {
  freeMemBuf(&ufx->out);
  freeMemBuf(&ufx->err);
  freeVM(&ufx->vm);
  ASSERT_TRUE(1);
}

UTEST_F(VMSimple, Empty) {
  ASSERT_EQ(0, ufx->vm.stackTop - ufx->vm.stack);
}

UTEST_F(VMSimple, PushPop) {
  push(&ufx->vm, NUMBER_VAL(1.2));
  push(&ufx->vm, NUMBER_VAL(3.4));
  push(&ufx->vm, NUMBER_VAL(5.6));
  ASSERT_EQ(3, ufx->vm.stackTop - ufx->vm.stack);
  EXPECT_VALEQ(NUMBER_VAL(5.6), pop(&ufx->vm));
  EXPECT_VALEQ(NUMBER_VAL(3.4), pop(&ufx->vm));
  EXPECT_VALEQ(NUMBER_VAL(1.2), pop(&ufx->vm));
  ASSERT_EQ(0, ufx->vm.stackTop - ufx->vm.stack);
}

UTEST_F(VMSimple, UnknownOp) {
  Chunk chunk;
  initChunk(&chunk);
  writeChunk(&ufx->vm.gc, &chunk, 255, 1);

  InterpretResult ires = interpretChunk(&ufx->vm, &chunk);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  fflush(ufx->err.fptr);
  const char errMsg[] = "Unknown opcode 255\n";
  EXPECT_STREQ(errMsg, memBufSuffix(ufx->err, errMsg));
}

UTEST_F(VMSimple, PrintScript) {
  size_t temps = 0;

  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_GET_LOCAL, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->out.fptr);
  EXPECT_STREQ("<script>\n", ufx->out.buf);

  if (ires != INTERPRET_OK) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }
}

UTEST_F(VMSimple, OpCall) {
  size_t temps = 0;

  // fun b(n) { print n; return n + 1; }
  ObjFunction* bFun = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(bFun));
  temps++;
  bFun->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "b", 1);
  bFun->arity = 1;
  temps += fillChunk(&bFun->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_GET_LOCAL, 1, OP_PRINT, OP_GET_LOCAL, 1,
          OP_CONSTANT, 0, OP_ADD, OP_RETURN),
      LIST(Value, N(1.0)));

  // fun a() { print "a"; print b(1); print "A"; }
  ObjFunction* aFun = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(aFun));
  temps++;
  aFun->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "a", 1);
  temps += fillChunk(&aFun->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_CLOSURE, 1,
          OP_CONSTANT, 2, OP_CALL, 1, OP_PRINT, OP_CONSTANT, 3,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value, S("a"), OBJ_VAL(bFun), N(1.0), S("A")));

  // print "z"; a(); print "Z";
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_CLOSURE, 1, OP_CALL, 0,
          OP_POP, OP_CONSTANT, 2, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value, S("("), OBJ_VAL(aFun), S(")")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->out.fptr);
  EXPECT_STREQ("(\na\n1\n2\nA\n)\n", ufx->out.buf);

  if (ires != INTERPRET_OK) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }
}

UTEST_F(VMSimple, OpCallClock) {
  size_t temps = 0;

  // print clock() >= 0;
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_GET_GLOBAL, 0, OP_CALL, 0, OP_CONSTANT, 1,
          OP_LESS, OP_NOT, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value, S("clock"), N(0.0)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->out.fptr);
  EXPECT_STREQ("true\n", ufx->out.buf);

  if (ires != INTERPRET_OK) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }
}

UTEST_F(VMSimple, OpCallUncallableNil) {
  size_t temps = 0;

  // nil();
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_NIL, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Value));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Can only call functions and classes.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, OpCallUncallableString) {
  size_t temps = 0;

  // "foo"();
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Value, S("foo")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Can only call functions and classes.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, OpCallWrongNumArgs) {
  size_t temps = 0;

  // fun a() {}
  ObjFunction* aFun = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(aFun));
  temps++;
  aFun->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "a", 1);
  temps += fillChunk(&aFun->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_NIL, OP_RETURN), LIST(Value));

  // a(nil);
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLOSURE, 0, OP_NIL, OP_CALL, 1, OP_NIL,
          OP_RETURN),
      LIST(Value, OBJ_VAL(aFun)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Expected 0 arguments but got 1.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, FunNameInErrorMsg) {
  size_t temps = 0;

  // fun myFunction() { nil(); }
  ObjFunction* myFunction = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(myFunction));
  temps++;
  myFunction->name =
      copyString(&ufx->vm.gc, &ufx->vm.strings, "myFunction", 10);
  temps += fillChunk(&myFunction->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_NIL, OP_CALL, 0, OP_POP, OP_NIL, OP_RETURN),
      LIST(Value));

  // myFunction();
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLOSURE, 0, OP_CALL, 0, OP_NIL, OP_RETURN),
      LIST(Value, OBJ_VAL(myFunction)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "] in myFunction";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, Closures1) {
  // {
  //   var x;
  //   var y = 2;
  //   fun f(a, b) {
  //     x = 1;
  //     fun g() {
  //       print a + b + x + y;
  //     }
  //     return g;
  //   }
  //   f(3, 4)();
  // }
  size_t temps = 0;

  ObjFunction* gFun = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(gFun));
  temps++;
  gFun->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "g", 1);
  gFun->upvalueCount = 4;
  temps += fillChunk(&gFun->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_GET_UPVALUE, 0, OP_GET_UPVALUE, 1, OP_ADD,
          OP_GET_UPVALUE, 2, OP_ADD, OP_GET_UPVALUE, 3, OP_ADD,
          OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value));

  ObjFunction* fFun = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(fFun));
  temps++;
  fFun->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "f", 1);
  fFun->arity = 2;
  fFun->upvalueCount = 2;
  temps += fillChunk(&fFun->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_SET_UPVALUE, 0, OP_POP,
          OP_CLOSURE, 1, 1, 1, 1, 2, 0, 0, 0, 1, OP_GET_LOCAL, 3,
          OP_RETURN, OP_NIL, OP_RETURN),
      LIST(Value, N(1.0), OBJ_VAL(gFun)));

  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_CLOSURE, 1, 1, 1, 1, 2,
          OP_GET_LOCAL, 3, OP_CONSTANT, 2, OP_CONSTANT, 3, OP_CALL, 2,
          OP_CALL, 0, OP_POP, OP_POP, OP_CLOSE_UPVALUE,
          OP_CLOSE_UPVALUE, OP_NIL, OP_RETURN),
      LIST(Value, N(2.0), OBJ_VAL(fFun), N(3.0), N(4.0)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->out.fptr);
  EXPECT_STREQ("10\n", ufx->out.buf);

  if (ires != INTERPRET_OK) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }
}

UTEST_F(VMSimple, Closures2) {
  // var f; var g; var h;
  // {
  //   var x = "x"; var y = "y"; var z = "z";
  //   fun ff() { print z; } f = ff;
  //   fun gg() { print x; } g = gg;
  //   fun hh() { print y; } h = hh;
  // }
  // f(); g(); h();
  size_t temps = 0;

  ObjFunction* ff = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(ff));
  temps++;
  ff->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "ff", 2);
  ff->upvalueCount = 1;
  temps += fillChunk(&ff->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_GET_UPVALUE, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value));

  ObjFunction* gg = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(gg));
  temps++;
  gg->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "gg", 2);
  gg->upvalueCount = 1;
  temps += fillChunk(&gg->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_GET_UPVALUE, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value));

  ObjFunction* hh = newFunction(&ufx->vm.gc);
  pushTemp(&ufx->vm.gc, OBJ_VAL(hh));
  temps++;
  hh->name = copyString(&ufx->vm.gc, &ufx->vm.strings, "hh", 2);
  hh->upvalueCount = 1;
  temps += fillChunk(&hh->chunk, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_GET_UPVALUE, 0, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value));

  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_NIL, OP_DEFINE_GLOBAL, 0, OP_NIL,
          OP_DEFINE_GLOBAL, 1, OP_NIL, OP_DEFINE_GLOBAL, 2, OP_CONSTANT,
          3, OP_CONSTANT, 4, OP_CONSTANT, 5, OP_CLOSURE, 6, 1, 3,
          OP_GET_LOCAL, 4, OP_SET_GLOBAL, 7, OP_POP, OP_CLOSURE, 8, 1,
          1, OP_GET_LOCAL, 5, OP_SET_GLOBAL, 9, OP_POP, OP_CLOSURE, 10,
          1, 2, OP_GET_LOCAL, 6, OP_SET_GLOBAL, 11, OP_POP, OP_POP,
          OP_POP, OP_POP, OP_CLOSE_UPVALUE, OP_CLOSE_UPVALUE,
          OP_CLOSE_UPVALUE, OP_GET_GLOBAL, 12, OP_CALL, 0, OP_POP,
          OP_GET_GLOBAL, 13, OP_CALL, 0, OP_POP, OP_GET_GLOBAL, 14,
          OP_CALL, 0, OP_POP, OP_NIL, OP_RETURN),
      LIST(Value, S("f"), S("g"), S("h"), S("x"), S("y"), S("z"),
          OBJ_VAL(ff), S("f"), OBJ_VAL(gg), S("g"), OBJ_VAL(hh), S("h"),
          S("f"), S("g"), S("h")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->out.fptr);
  EXPECT_STREQ("z\nx\ny\n", ufx->out.buf);

  if (ires != INTERPRET_OK) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesSimple) {
  size_t temps = 0;

  // class F{} var f = F(); f.x = 1; print f.x;
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3,
          OP_CONSTANT, 5, OP_SET_PROPERTY, 4, OP_POP, OP_GET_GLOBAL, 6,
          OP_GET_PROPERTY, 7, OP_PRINT, OP_NIL, OP_RETURN),
      LIST(Value, S("F"), S("f"), S("F"), S("f"), S("x"), N(1.0),
          S("f"), S("x")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->out.fptr);
  EXPECT_STREQ("1\n", ufx->out.buf);

  if (ires != INTERPRET_OK) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesGetUndefinedProperty) {
  size_t temps = 0;

  // class F{} var f = F(); f.x;
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3,
          OP_GET_PROPERTY, 4, OP_POP, OP_NIL, OP_RETURN),
      LIST(Value, S("F"), S("f"), S("F"), S("f"), S("x")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Undefined property 'x'.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesGetNonInstance) {
  size_t temps = 0;

  // 0.x;
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_PROPERTY, 1, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Value, N(0.0), S("x")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Only instances have properties.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesSetNonInstance) {
  size_t temps = 0;

  // 0.x = 1;
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 2, OP_SET_PROPERTY, 1,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Value, N(0.0), S("x"), N(1.0)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Only instances have fields.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesIndexSimple) {
  size_t temps = 0;

  // class F{} var f = F(); f["x"] = 1; print f["x"];
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3,
          OP_CONSTANT, 4, OP_CONSTANT, 5, OP_SET_INDEX, OP_POP,
          OP_GET_GLOBAL, 6, OP_CONSTANT, 7, OP_GET_INDEX, OP_PRINT,
          OP_NIL, OP_RETURN),
      LIST(Value, S("F"), S("f"), S("F"), S("f"), S("x"), N(1.0),
          S("f"), S("x")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->out.fptr);
  EXPECT_STREQ("1\n", ufx->out.buf);

  if (ires != INTERPRET_OK) {
    fflush(ufx->err.fptr);
    EXPECT_STREQ("", ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesIndexGetUndefinedProperty) {
  size_t temps = 0;

  // class F{} var f = F(); f["x"];
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3,
          OP_CONSTANT, 4, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Value, S("F"), S("f"), S("F"), S("f"), S("x")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Undefined property 'x'.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesIndexGetNonInstance) {
  size_t temps = 0;

  // 0["x"];
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_INDEX,
          OP_POP, OP_NIL, OP_RETURN),
      LIST(Value, N(0.0), S("x")));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Only instances have properties.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesIndexSetNonInstance) {
  size_t temps = 0;

  // 0["x"] = 1;
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
          OP_SET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Value, N(0.0), S("x"), N(1.0)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Only instances have fields.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesIndexGetBadIndex) {
  size_t temps = 0;

  // class F{} var f = F(); f[0];
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3,
          OP_CONSTANT, 4, OP_GET_INDEX, OP_POP, OP_NIL, OP_RETURN),
      LIST(Value, S("F"), S("f"), S("F"), S("f"), N(0.0)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Instances can only be indexed by string.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

UTEST_F(VMSimple, ClassesIndexSetBadIndex) {
  size_t temps = 0;

  // class F{} var f = F(); f[0] = 1;
  Chunk script;
  initChunk(&script);
  temps += fillChunk(&script, &ufx->vm.gc, &ufx->vm.strings,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3,
          OP_CONSTANT, 4, OP_CONSTANT, 5, OP_SET_INDEX, OP_POP, OP_NIL,
          OP_RETURN),
      LIST(Value, S("F"), S("f"), S("F"), S("f"), N(0.0), N(1.0)));

  InterpretResult ires = interpretChunk(&ufx->vm, &script);
  EXPECT_EQ((InterpretResult)INTERPRET_RUNTIME_ERROR, ires);

  while (temps > 0) {
    popTemp(&ufx->vm.gc);
    temps--;
  }

  fflush(ufx->err.fptr);
  const char* msg = "Instances can only be indexed by string.";
  const char* findMsg = strstr(ufx->err.buf, msg);
  if (findMsg) {
    EXPECT_STRNEQ(msg, findMsg, strlen(msg));
  } else {
    EXPECT_STREQ(msg, ufx->err.buf);
  }
}

typedef struct {
  const char* msgSuffix;
  InterpretResult ires;
  int codeSize;
  uint8_t* code;
  int valueSize;
  Value* values;
} ResultFromChunk;

struct VM {
  ResultFromChunk* cases;
};

UTEST_I_SETUP(VM) {
  (void)utest_index;
  (void)utest_fixture;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(VM) {
  ResultFromChunk* expected = &ufx->cases[utest_index];

  size_t temps = 0;
  MemBuf out, err;
  VM vm;
  Chunk chunk;
  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);
  initChunk(&chunk);

  // Prepare the chunk.
  temps += fillChunk(&chunk, &vm.gc, &vm.strings, expected->codeSize,
      expected->code, expected->valueSize, expected->values);
  writeChunk(&vm.gc, &chunk, OP_NIL, (expected->codeSize - 1) >> 1);
  writeChunk(&vm.gc, &chunk, OP_RETURN, (expected->codeSize - 1) >> 1);

  // Interpret the chunk.
  InterpretResult ires = interpretChunk(&vm, &chunk);
  EXPECT_EQ(expected->ires, ires);

  while (temps > 0) {
    popTemp(&vm.gc);
    temps--;
  }

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
}

#define VM_CASES(name, data, count) \
  UTEST_I(VM, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

ResultFromChunk opConstant[] = {
  { "nil\n", INTERPRET_OK, LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT),
      LIST(Value, NIL) },
  { "false\n", INTERPRET_OK, LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT),
      LIST(Value, B(false)) },
  { "true\n", INTERPRET_OK, LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT),
      LIST(Value, B(true)) },
  { "2.5\n", INTERPRET_OK, LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT),
      LIST(Value, N(2.5)) },
};

VM_CASES(OpConstant, opConstant, 4);

ResultFromChunk opLiterals[] = {
  { "false\n", INTERPRET_OK, LIST(uint8_t, OP_FALSE, OP_PRINT),
      LIST(Value) },
  { "nil\n", INTERPRET_OK, LIST(uint8_t, OP_NIL, OP_PRINT),
      LIST(Value) },
  { "true\n", INTERPRET_OK, LIST(uint8_t, OP_TRUE, OP_PRINT),
      LIST(Value) },
};

VM_CASES(OpLiterals, opLiterals, 3);

ResultFromChunk opPop[] = {
  { "0\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_POP, OP_PRINT),
      LIST(Value, N(0.0), N(1.0)) },
};

VM_CASES(OpPop, opPop, 1);

ResultFromChunk opLocals[] = {
  { "false\ntrue\nfalse\ntrue\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_FALSE, OP_GET_LOCAL, 1, OP_GET_LOCAL, 2,
          OP_PRINT, OP_PRINT, OP_PRINT, OP_PRINT),
      LIST(Value) },
  { "true\ntrue\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_TRUE, OP_SET_LOCAL, 1, OP_PRINT,
          OP_PRINT),
      LIST(Value) },
};

VM_CASES(OpLocals, opLocals, 2);

ResultFromChunk opGlobals[] = {
  { "", INTERPRET_RUNTIME_ERROR, LIST(uint8_t, OP_GET_GLOBAL, 0),
      LIST(Value, S("foo")) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_SET_GLOBAL, 0), LIST(Value, S("foo")) },
  { "123\n456\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 1, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL,
          0, OP_PRINT, OP_CONSTANT, 2, OP_SET_GLOBAL, 0, OP_POP,
          OP_GET_GLOBAL, 0, OP_PRINT),
      LIST(Value, S("foo"), N(123.0), N(456.0)) },
};

VM_CASES(OpGlobals, opGlobals, 3);

ResultFromChunk opEqual[] = {
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_EQUAL, OP_PRINT), LIST(Value) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_FALSE, OP_EQUAL, OP_PRINT),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_FALSE, OP_EQUAL, OP_PRINT),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_FALSE, OP_TRUE, OP_EQUAL, OP_PRINT),
      LIST(Value) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_TRUE, OP_EQUAL, OP_PRINT),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_EQUAL, OP_PRINT),
      LIST(Value, N(1.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_EQUAL, OP_PRINT),
      LIST(Value, N(1.0), N(1.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_EQUAL, OP_PRINT),
      LIST(Value, N(1.0), N(2.0)) },
};

VM_CASES(OpEqual, opEqual, 8);

ResultFromChunk opGreater[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_GREATER, OP_PRINT),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_GREATER, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT),
      LIST(Value, N(1.0), N(2.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT),
      LIST(Value, N(2.0), N(2.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GREATER,
          OP_PRINT),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_CASES(OpGreater, opGreater, 5);

ResultFromChunk opLess[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_LESS, OP_PRINT), LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_LESS, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT),
      LIST(Value, N(1.0), N(2.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT),
      LIST(Value, N(2.0), N(2.0)) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_PRINT),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_CASES(OpLess, opLess, 5);

ResultFromChunk opIn[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_IN, OP_PRINT), LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_IN, OP_PRINT),
      LIST(Value, S("x")) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_NIL,
          OP_GET_GLOBAL, 1, OP_CALL, 0, OP_IN, OP_PRINT),
      LIST(Value, S("F"), S("F")) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_CONSTANT, 1,
          OP_GET_GLOBAL, 2, OP_CALL, 0, OP_IN, OP_PRINT),
      LIST(Value, S("F"), S("x"), S("F")) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_CLASS, 0, OP_DEFINE_GLOBAL, 0, OP_GET_GLOBAL, 2,
          OP_CALL, 0, OP_DEFINE_GLOBAL, 1, OP_GET_GLOBAL, 3,
          OP_CONSTANT, 5, OP_SET_PROPERTY, 4, OP_POP, OP_CONSTANT, 6,
          OP_GET_GLOBAL, 7, OP_IN, OP_PRINT),
      LIST(Value, S("F"), S("f"), S("F"), S("f"), S("x"), N(1.0),
          S("x"), S("f")) },
};

VM_CASES(OpIn, opIn, 5);

ResultFromChunk opAdd[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_ADD, OP_PRINT), LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_ADD, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "5\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_CASES(OpAdd, opAdd, 5);

ResultFromChunk opAddConcat[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NIL, OP_ADD, OP_PRINT),
      LIST(Value, S("")) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_ADD, OP_PRINT),
      LIST(Value, S("")) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT),
      LIST(Value, N(0.0), S("")) },
  { "foo\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT),
      LIST(Value, S("foo"), S("")) },
  { "foo\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT),
      LIST(Value, S(""), S("foo")) },
  { "foobar\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT),
      LIST(Value, S("foo"), S("bar")) },
  { "foobarfoobar\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
          2, OP_CONSTANT, 3, OP_ADD, OP_ADD, OP_PRINT),
      LIST(Value, S("foo"), S("bar"), S("foo"), S("bar")) },
};

VM_CASES(OpAddConcat, opAddConcat, 7);

ResultFromChunk opSubtract[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_SUBTRACT, OP_PRINT),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_SUBTRACT, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "1\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT,
          OP_PRINT),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_CASES(OpSubtract, opSubtract, 3);

ResultFromChunk opMultiply[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_MULTIPLY, OP_PRINT),
      LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_MULTIPLY, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "6\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_MULTIPLY,
          OP_PRINT),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_CASES(OpMultiply, opMultiply, 3);

ResultFromChunk opDivide[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NIL, OP_DIVIDE, OP_PRINT), LIST(Value) },
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_CONSTANT, 0, OP_DIVIDE, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "1.5\n", INTERPRET_OK,
      LIST(
          uint8_t, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_DIVIDE, OP_PRINT),
      LIST(Value, N(3.0), N(2.0)) },
};

VM_CASES(OpDivide, opDivide, 3);

ResultFromChunk opNot[] = {
  { "true\n", INTERPRET_OK, LIST(uint8_t, OP_NIL, OP_NOT, OP_PRINT),
      LIST(Value) },
  { "true\n", INTERPRET_OK, LIST(uint8_t, OP_FALSE, OP_NOT, OP_PRINT),
      LIST(Value) },
  { "false\n", INTERPRET_OK, LIST(uint8_t, OP_TRUE, OP_NOT, OP_PRINT),
      LIST(Value) },
  { "false\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NOT, OP_PRINT),
      LIST(Value, N(0.0)) },
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_TRUE, OP_NOT, OP_NOT, OP_PRINT), LIST(Value) },
};

VM_CASES(OpNot, opNot, 5);

ResultFromChunk opNegate[] = {
  { "", INTERPRET_RUNTIME_ERROR,
      LIST(uint8_t, OP_NIL, OP_NEGATE, OP_PRINT), LIST(Value) },
  { "-1\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_PRINT),
      LIST(Value, N(1.0)) },
  { "1\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_PRINT),
      LIST(Value, N(1.0)) },
};

VM_CASES(OpNegate, opNegate, 3);

ResultFromChunk opJump[] = {
  { "true\n", INTERPRET_OK,
      LIST(uint8_t, OP_JUMP, 0, 2, OP_NIL, OP_PRINT, OP_TRUE, OP_PRINT),
      LIST(Value) },
};

VM_CASES(OpJump, opJump, 1);

ResultFromChunk opJumpIfFalse[] = {
  { "0\n2\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_NIL, OP_JUMP_IF_FALSE,
          0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT, 2, OP_PRINT),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
  { "0\n2\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_FALSE,
          OP_JUMP_IF_FALSE, 0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT,
          2, OP_PRINT),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
  { "0\n1\n2\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_PRINT, OP_TRUE, OP_JUMP_IF_FALSE,
          0, 3, OP_CONSTANT, 1, OP_PRINT, OP_CONSTANT, 2, OP_PRINT),
      LIST(Value, N(0.0), N(1.0), N(2.0)) },
};

VM_CASES(OpJumpIfFalse, opJumpIfFalse, 3);

ResultFromChunk opLoop[] = {
  { "0\n1\n2\n3\n4\n", INTERPRET_OK,
      LIST(uint8_t, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_CONSTANT, 1,
          OP_LESS, OP_JUMP_IF_FALSE, 0, 15, OP_POP, OP_GET_LOCAL, 1,
          OP_PRINT, OP_GET_LOCAL, 1, OP_CONSTANT, 2, OP_ADD,
          OP_SET_LOCAL, 1, OP_POP, OP_LOOP, 0, 23, OP_POP),
      LIST(Value, N(0.0), N(5.0), N(1.0)) },
};

VM_CASES(OpLoop, opLoop, 1);

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
