#include <assert.h>
#include <string.h>

#include "utest.h"

#include "membuf.h"
#include "memory.h"
#include "vm.h"

#define ufx utest_fixture

UTEST(InterpretMulti, ForceGC) {
  MemBuf out, err;
  VM vm;

  initMemBuf(&out);
  initMemBuf(&err);
  // debugLogGC = true;
  debugStressGC = true; // Force initial collect with small heap.
  initVM(&vm, out.fptr, err.fptr);

  debugStressGC = false; // Test non-stressed garbage collection.
  for (size_t i = 0; i < 20; ++i) {
    InterpretResult ires = interpret(&vm, "fun a(){}");
    EXPECT_EQ((InterpretResult)INTERPRET_OK, ires);
    if (ires != INTERPRET_OK) {
      EXPECT_EQ(9999u, i);
    }
  }
  debugStressGC = true;

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

typedef struct {
  InterpretResult ires;
  const char* msg;
  const char* src;
} InterpretCase;

struct InterpretMulti {
  size_t caseCount;
  InterpretCase* cases;
};

UTEST_F_SETUP(InterpretMulti) {
  (void)utest_fixture;
  ASSERT_TRUE(1);
}

UTEST_F_TEARDOWN(InterpretMulti) {
  MemBuf out;
  MemBuf err;
  VM vm;

  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  for (size_t i = 0; i < ufx->caseCount; ++i) {
    InterpretCase* expected = &ufx->cases[i];

    freeMemBuf(&out);
    freeMemBuf(&err);
    initMemBuf(&out);
    initMemBuf(&err);
    vm.fout = out.fptr;
    vm.ferr = err.fptr;

    InterpretResult ires = interpret(&vm, expected->src);
    EXPECT_EQ(expected->ires, ires);
    if (*utest_result == UTEST_TEST_FAILURE) {
      EXPECT_EQ(-1, (int)i);
    }

    fflush(out.fptr);
    fflush(err.fptr);
    if (expected->ires == INTERPRET_OK) {
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
  }

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

#define INTERPRET_MULTI(name, data) \
  UTEST_F(InterpretMulti, name) { \
    ufx->caseCount = sizeof(data) / sizeof(data[0]); \
    ufx->cases = data; \
    ASSERT_TRUE(1); \
  }

InterpretCase tempStringCleanUp[] = {
  { INTERPRET_OK, "3\n", "print 1+2;" },
  { INTERPRET_OK, "xyz\n", "print \"x\"+\"y\"+\"z\";" },
  { INTERPRET_OK, "3\n", "print 1+2;" },
};

INTERPRET_MULTI(TempStringCleanUp, tempStringCleanUp);

InterpretCase stringReUseAfterError[] = {
  { INTERPRET_COMPILE_ERROR, "Expect ';' after variable declaration.",
      "var x=1" },
  { INTERPRET_OK, "", "var x=1;" },
};

INTERPRET_MULTI(StringReUseAfterError, stringReUseAfterError);

struct Interpret {
  InterpretCase* cases;
};

UTEST_I_SETUP(Interpret) {
  (void)utest_fixture;
  (void)utest_index;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(Interpret) {
  InterpretCase* expected = &ufx->cases[utest_index];

  MemBuf out;
  MemBuf err;
  VM vm;

  initMemBuf(&out);
  initMemBuf(&err);
  initVM(&vm, out.fptr, err.fptr);

  InterpretResult ires = interpret(&vm, expected->src);
  EXPECT_EQ(expected->ires, ires);

  fflush(out.fptr);
  fflush(err.fptr);
  if (expected->ires == INTERPRET_OK) {
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

  freeVM(&vm);
  freeMemBuf(&out);
  freeMemBuf(&err);
}

#define INTERPRET(name, data, count) \
  UTEST_I(Interpret, name, count) { \
    static_assert(sizeof(data) / sizeof(data[0]) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

InterpretCase empty[] = {
  { INTERPRET_OK, "", "" },
};

INTERPRET(Empty, empty, 1);

InterpretCase error[] = {
  { INTERPRET_COMPILE_ERROR, "Unexpected character", "#" },
  { INTERPRET_COMPILE_ERROR, "Expect expression", "print print print" },
};

INTERPRET(Error, error, 2);

InterpretCase grouping[] = {
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "(" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "();" },
  { INTERPRET_OK, "1\n", "print(1);" },
  { INTERPRET_OK, "1\n", "print(((1)));" },
  { INTERPRET_OK, "9\n", "print((1+2)*3);" },
  { INTERPRET_OK, "-3\n", "print((1+2)*(3-4));" },
};

INTERPRET(Grouping, grouping, 6);

InterpretCase negate[] = {
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "-;" },
  { INTERPRET_RUNTIME_ERROR, "Operand must be a number.", "-nil;" },
  { INTERPRET_RUNTIME_ERROR, "Operand must be a number.", "-false;" },
  { INTERPRET_RUNTIME_ERROR, "Operand must be a number.", "-true;" },
  { INTERPRET_RUNTIME_ERROR, "Operand must be a number.", "-\"x\";" },
  { INTERPRET_OK, "-1\n", "print -1;" },
  { INTERPRET_OK, "1\n", "print --1;" },
  { INTERPRET_OK, "-1\n", "print ---1;" },
};

INTERPRET(Negate, negate, 8);

InterpretCase binaryNum[] = {
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "+1;" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "1+;" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "1-;" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "1*;" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "1/;" },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.",
      "print nil+nil;" },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", "print nil+1;" },
  { INTERPRET_RUNTIME_ERROR,
      "Operands must be two numbers or two strings.", "print 1+nil;" },
  { INTERPRET_OK, "5\n", "print 3+2;" },
  { INTERPRET_OK, "1\n", "print 3-2;" },
  { INTERPRET_OK, "6\n", "print 3*2;" },
  { INTERPRET_OK, "1.5\n", "print 3/2;" },
  { INTERPRET_OK, "1.1\n", "print 5.1%2;" },
  { INTERPRET_OK, "-2.5\n", "print 1+2*3/4-5;" },
  { INTERPRET_OK, "1.1\n", "print 1.1+4%2;" },
};

INTERPRET(BinaryNum, binaryNum, 15);

InterpretCase comments[] = {
  { INTERPRET_OK, "", "//print 1;" },
  { INTERPRET_OK, "1\n", "print 1;//" },
  { INTERPRET_OK, "", "////print 1;//" },
  { INTERPRET_OK, "1\n", "//\nprint //\n1;//" },
  { INTERPRET_OK, "1\n", "//\nprint 1;" },
  { INTERPRET_OK, "2\n", "//print 1;\nprint 2;" },
  { INTERPRET_OK, "1\n2\n", "//\nprint 1;\nprint 2;" },
};

INTERPRET(Comments, comments, 7);

InterpretCase print[] = {
  { INTERPRET_OK, "1\n", "print 1;" },
  { INTERPRET_OK, "nil\n", "print nil;" },
  { INTERPRET_OK, "false\n", "print false;" },
  { INTERPRET_OK, "true\n", "print true;" },
};

INTERPRET(Print, print, 4);

InterpretCase logicalNot[] = {
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "!;" },
  { INTERPRET_OK, "true\n", "print !nil;" },
  { INTERPRET_OK, "true\n", "print !false;" },
  { INTERPRET_OK, "false\n", "print !true;" },
  { INTERPRET_OK, "false\n", "print !0;" },
  { INTERPRET_OK, "false\n", "print !\"\";" },
  { INTERPRET_OK, "false\n", "print !!nil;" },
  { INTERPRET_OK, "false\n", "print !(!nil);" },
  { INTERPRET_OK, "true\n", "print !!!nil;" },
};

INTERPRET(LogicalNot, logicalNot, 9);

InterpretCase equal[] = {
  { INTERPRET_OK, "false\n", "print nil==false;" },
  { INTERPRET_OK, "false\n", "print false==true;" },
  { INTERPRET_OK, "false\n", "print true==0;" },
  { INTERPRET_OK, "false\n", "print 0==\"\";" },
  { INTERPRET_OK, "false\n", "print \"\"==nil;" },
  { INTERPRET_OK, "true\n", "print nil==nil;" },
  { INTERPRET_OK, "true\n", "print false==false;" },
  { INTERPRET_OK, "true\n", "print true==true;" },
  { INTERPRET_OK, "true\n", "print 0==0;" },
  { INTERPRET_OK, "true\n", "print 1==1;" },
  { INTERPRET_OK, "false\n", "print 0==1;" },
  { INTERPRET_OK, "false\n", "print 1==0;" },
  { INTERPRET_OK, "true\n", "print \"x\"==\"x\";" },
  { INTERPRET_OK, "true\n", "print \"y\"==\"y\";" },
  { INTERPRET_OK, "false\n", "print \"x\"==\"y\";" },
  { INTERPRET_OK, "false\n", "print \"y\"==\"x\";" },
};

INTERPRET(Equal, equal, 16);

InterpretCase notEqual[] = {
  { INTERPRET_OK, "true\n", "print nil!=false;" },
  { INTERPRET_OK, "true\n", "print false!=true;" },
  { INTERPRET_OK, "true\n", "print true!=0;" },
  { INTERPRET_OK, "true\n", "print 0!=\"\";" },
  { INTERPRET_OK, "true\n", "print \"\"!=nil;" },
  { INTERPRET_OK, "false\n", "print nil!=nil;" },
  { INTERPRET_OK, "false\n", "print false!=false;" },
  { INTERPRET_OK, "false\n", "print true!=true;" },
  { INTERPRET_OK, "false\n", "print 0!=0;" },
  { INTERPRET_OK, "false\n", "print 1!=1;" },
  { INTERPRET_OK, "true\n", "print 0!=1;" },
  { INTERPRET_OK, "true\n", "print 1!=0;" },
  { INTERPRET_OK, "false\n", "print \"x\"!=\"x\";" },
  { INTERPRET_OK, "false\n", "print \"y\"!=\"y\";" },
  { INTERPRET_OK, "true\n", "print \"x\"!=\"y\";" },
  { INTERPRET_OK, "true\n", "print \"y\"!=\"x\";" },
};

INTERPRET(NotEqual, notEqual, 16);

InterpretCase greater[] = {
  { INTERPRET_OK, "true\n", "print 2>1;" },
  { INTERPRET_OK, "false\n", "print 2>2;" },
  { INTERPRET_OK, "false\n", "print 2>3;" },
};

INTERPRET(Greater, greater, 3);

InterpretCase greaterEqual[] = {
  { INTERPRET_OK, "true\n", "print 2>=1;" },
  { INTERPRET_OK, "true\n", "print 2>=2;" },
  { INTERPRET_OK, "false\n", "print 2>=3;" },
};

INTERPRET(GreaterEqual, greaterEqual, 3);

InterpretCase less[] = {
  { INTERPRET_OK, "false\n", "print 2<1;" },
  { INTERPRET_OK, "false\n", "print 2<2;" },
  { INTERPRET_OK, "true\n", "print 2<3;" },
};

INTERPRET(Less, less, 3);

InterpretCase lessEqual[] = {
  { INTERPRET_OK, "false\n", "print 2<=1;" },
  { INTERPRET_OK, "true\n", "print 2<=2;" },
  { INTERPRET_OK, "true\n", "print 2<=3;" },
};

INTERPRET(LessEqual, lessEqual, 3);

InterpretCase strings[] = {
  { INTERPRET_RUNTIME_ERROR, "String index (0) out of bounds (0).",
      "\"\"[0];" },
  { INTERPRET_RUNTIME_ERROR,
      "String index (0.5) must be a whole number.", "\"a\"[0.5];" },
  { INTERPRET_RUNTIME_ERROR,
      "Can only set index of lists, maps and instances.",
      "\"a\"[0]=98;" },
  { INTERPRET_OK, "\n", "print \"\";" },
  { INTERPRET_OK, "foo\n", "print \"foo\";" },
  { INTERPRET_OK, "foo\n", "print \"\"+\"foo\";" },
  { INTERPRET_OK, "foo\n", "print \"foo\"+\"\";" },
  { INTERPRET_OK, "foobar\n", "print \"foo\"+\"bar\";" },
  { INTERPRET_OK, "1234567887654321\n1234567887654321\n",
      "print \"1234567887654321\";print \"12345678\"+\"87654321\";" },
  { INTERPRET_OK, "true\n", "print \"foobar\"==\"foo\"+\"bar\";" },
  { INTERPRET_OK, "97\n98\n99\n65\n66\n67\n",
      "var s=\"abcABC\";print s[0];print s[1];print s[2];"
      "print s[3];print s[4];print s[5];" },
};

INTERPRET(Strings, strings, 11);

InterpretCase stringsParseNum[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "\"\".parsenum(nil);" },
  { INTERPRET_OK, "-123.456\n", "print \" -123.456 \".parsenum();" },
  { INTERPRET_OK, "579\n",
      "print \"123\".parsenum()+\"456\".parsenum();" },
  { INTERPRET_OK, "nil\n", "print \"123 z\".parsenum();" },
};

INTERPRET(StringsParseNum, stringsParseNum, 4);

InterpretCase stringsSize[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "\"\".size(nil);" },
  { INTERPRET_OK, "0\n", "print \"\".size();" },
  { INTERPRET_OK, "11\n", "print \"hello world\".size();" },
  { INTERPRET_OK, "11\n", "var s=\"hello world\".size;print s();" },
};

INTERPRET(StringsSize, stringsSize, 4);

InterpretCase stringsSubstr[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 2 arguments but got 0.",
      "\"\".substr();" },
  { INTERPRET_RUNTIME_ERROR, "Start must be a number.",
      "\"\".substr(nil,0);" },
  { INTERPRET_RUNTIME_ERROR, "End must be a number.",
      "\"\".substr(0,nil);" },
  { INTERPRET_RUNTIME_ERROR, "Start (0.5) must be a whole number.",
      "\"a\".substr(0.5,1);" },
  { INTERPRET_RUNTIME_ERROR, "End (0.5) must be a whole number.",
      "\"a\".substr(0,0.5);" },
  { INTERPRET_OK, "true\n", "print \"\".substr(0,0)==\"\";" },
  { INTERPRET_OK, "hello\n", "print \"hello\".substr(0,-1);" },
  { INTERPRET_OK, "ello\n", "print \"hello\".substr(1,2147483647);" },
  { INTERPRET_OK, "hell\n", "print \"hello\".substr(-2147483648,-2);" },
  { INTERPRET_OK, "hello\nworld\n",
      "var msg=\"hello world\";"
      "print msg.substr(0,5);print msg.substr(-6,-1);" },
};

INTERPRET(StringsSubstr, stringsSubstr, 10);

InterpretCase nativeArgc[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "argc(nil);" },
  { INTERPRET_OK, "0\n", "print argc();" },
};

INTERPRET(NativeArgc, nativeArgc, 2);

InterpretCase nativeArgv[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "argv();" },
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.",
      "argv(nil);" },
  { INTERPRET_RUNTIME_ERROR, "Argument (1) out of bounds (0).",
      "argv(1);" },
};

INTERPRET(NativeArgv, nativeArgv, 3);

InterpretCase nativeCeil[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "ceil();" },
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.",
      "ceil(nil);" },
  { INTERPRET_OK, "2\n", "print ceil(1.5);" },
};

INTERPRET(NativeCeil, nativeCeil, 3);

InterpretCase nativeChr[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "chr();" },
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.",
      "chr(nil);" },
  { INTERPRET_RUNTIME_ERROR, "Argument (0.5) must be a whole number.",
      "chr(0.5);" },
  { INTERPRET_RUNTIME_ERROR, "Argument (-129) must be between ",
      "chr(-129);" },
  { INTERPRET_RUNTIME_ERROR, "Argument (256) must be between ",
      "chr(256);" },
  { INTERPRET_OK, "a\n", "print chr(97);" },
  { INTERPRET_OK, "1\n", "print chr(0).size();" },
};

INTERPRET(NativeChr, nativeChr, 7);

InterpretCase nativeClock[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "clock(nil);" },
  { INTERPRET_OK, "<native fn>\n", "print clock;" },
  { INTERPRET_OK, "true\n", "print clock()>=0;" },
};

INTERPRET(NativeClock, nativeClock, 3);

InterpretCase nativeEprint[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "eprint();" },
  { INTERPRET_RUNTIME_ERROR, "123.456\n", "eprint(123.456);eprint();" },
};

INTERPRET(NativeEprint, nativeEprint, 2);

InterpretCase nativeExit[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "exit();" },
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.",
      "exit(nil);" },
  { INTERPRET_RUNTIME_ERROR, "Argument (-1) must be between 0 and ",
      "exit(-1);" },
  { INTERPRET_RUNTIME_ERROR, "Argument (1e+10) must be between 0 and ",
      "exit(10000000000);" },
  { INTERPRET_RUNTIME_ERROR, "Argument (0.5) must be a whole number.",
      "exit(0.5);" },
};

INTERPRET(NativeExit, nativeExit, 5);

InterpretCase nativeFloor[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "floor();" },
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.",
      "floor(nil);" },
  { INTERPRET_OK, "1\n", "print floor(1.5);" },
};

INTERPRET(NativeFloor, nativeFloor, 3);

InterpretCase nativeRound[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "round();" },
  { INTERPRET_RUNTIME_ERROR, "Argument must be a number.",
      "round(nil);" },
  { INTERPRET_OK, "2\n", "print round(1.5);" },
  { INTERPRET_OK, "1\n", "print round(1.49);" },
};

INTERPRET(NativeRound, nativeRound, 4);

InterpretCase nativeStr[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "str();" },
  { INTERPRET_OK, "hi1\n", "print \"hi\"+str(1);" },
};

INTERPRET(NativeStr, nativeStr, 2);

InterpretCase nativeType[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "type();" },
  { INTERPRET_OK, "boolean\nnil\nnumber\n",
      "print type(true);print type(nil);print type(0);" },
  { INTERPRET_OK, "string\nlist\nmap\n",
      "print type(\"\");print type([]);print type({});" },
  { INTERPRET_OK, "function\n", "fun f(){}print type(f);" },
  { INTERPRET_OK, "function\n", "{var x;fun g(){x;}print type(g);}" },
  { INTERPRET_OK, "native function\n", "print type(type);" },
  { INTERPRET_OK, "class\ninstance\nfunction\n",
      "class F{m(){}}"
      "print type(F);print type(F());print type(F().m);" },
};

INTERPRET(NativeType, nativeType, 7);

InterpretCase functions[] = {
  { INTERPRET_COMPILE_ERROR, "Expect function name.", "fun" },
  { INTERPRET_COMPILE_ERROR, "Expect '(' after function name.",
      "fun a" },
  { INTERPRET_COMPILE_ERROR, "Expect '{' before function body.",
      "fun a()" },
  { INTERPRET_COMPILE_ERROR, "Expect ')' after parameters.",
      "fun a(x" },
  { INTERPRET_COMPILE_ERROR, "Expect parameter name.", "fun a(x," },
  { INTERPRET_COMPILE_ERROR, "Expect '}' after block.", "fun a(x,y){" },
  { INTERPRET_COMPILE_ERROR, "Expect '(' after 'fun'.",
      "(fun a(){})();" },
  { INTERPRET_COMPILE_ERROR, "Can't return from top-level code.",
      "return" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "fun a(){return" },
  { INTERPRET_COMPILE_ERROR, "Expect ';' after return value.",
      "fun a(){return 0" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "a(" },
  { INTERPRET_COMPILE_ERROR, "Expect ')' after arguments.", "a(0" },
  { INTERPRET_RUNTIME_ERROR, "Can only call functions and classes.",
      "nil();" },
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "fun a(){}a(0);" },
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "fun b(){}fun a(){b(0);}a();" },
  { INTERPRET_OK, "<fn a>\n", "fun a(){}print a;" },
  { INTERPRET_OK, "<fn ()>\n", "print fun(){};" },
  { INTERPRET_OK, "0\n", "fun a(){print 0;return;print 1;}a();" },
  { INTERPRET_OK, "1\n", "fun a(){return 1;}print a();" },
  { INTERPRET_OK, "1\n", "print fun(){return 1;}();" },
  { INTERPRET_OK, "1\n", "fun a(x){print x;}a(1);" },
  { INTERPRET_OK, "1\n", "(fun(x){print x;})(1);" },
  { INTERPRET_OK, "2\n", "fun a(x){return x+x;}print a(1);" },
  { INTERPRET_OK, "4\n", "fun a(x){return x+x;}print a(a(1));" },
  { INTERPRET_OK, "5\n", "fun a(x,y){print x+y;}a(3,2);" },
  { INTERPRET_OK, "1\n2\n3\n",
      "fun a(){print 2;}fun b(){print 1;a();print 3;}b();" },
  { INTERPRET_OK, "1\n0\n2\n3\n0\n4\n",
      "fun a(){print 1;b();print 2;}"
      "fun b(){print 0;}fun c(){print 3;b();print 4;}a();c();" },
};

INTERPRET(Functions, functions, 27);

InterpretCase closures[] = {
  { INTERPRET_OK, "outer\n",
      "var x = \"global\";"
      "fun outer() {"
      "  var x = \"outer\";"
      "  fun inner() {"
      "    print x;"
      "  }"
      "  inner();"
      "}"
      "outer();" },
  { INTERPRET_OK, "local\n",
      "fun makeClosure() {"
      "  var local = \"local\";"
      "  fun closure() {"
      "    print local;"
      "  }"
      "  return closure;"
      "}"
      "var closure = makeClosure();"
      "closure();" },
  { INTERPRET_OK, "1\n2\n3\n",
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
      "c(); c(); c();" },
  { INTERPRET_OK, "6\n",
      "fun outer(x) {"
      "  fun middle(y) {"
      "    fun inner(z) {"
      "      return x + y + z;"
      "    }"
      "    return inner;"
      "  }"
      "  return middle;"
      "}"
      "print outer(1)(2)(3);" },
  { INTERPRET_OK, "z\nx\ny\n",
      "var f; var g; var h;"
      "{"
      "  var x = \"x\"; var y = \"y\"; var z = \"z\";"
      "  fun ff() { print z; } f = ff;"
      "  fun gg() { print x; } g = gg;"
      "  fun hh() { print y; } h = hh;"
      "}"
      "f(); g(); h();" },
};

INTERPRET(Closures, closures, 5);

InterpretCase globalVars[] = {
  { INTERPRET_COMPILE_ERROR, "Expect variable name.", "var 0;" },
  { INTERPRET_COMPILE_ERROR, "Invalid assignment target.",
      "var x;var y;x+y=1;" },
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'x'.", "x;" },
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'x'.", "x;var x;" },
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'x'.", "x=1;" },
  { INTERPRET_OK, "nil\n", "var x;print x;" },
  { INTERPRET_OK, "1\n", "var x=1;print x;" },
  { INTERPRET_OK, "2\n", "var x=1;print x+x;" },
  { INTERPRET_OK, "3\n", "var x=1+2;print x;" },
  { INTERPRET_OK, "1\nhi\n", "var x=1;print x;var x=\"hi\";print x;" },
  { INTERPRET_OK, "1\nhi\n",
      "fun f(){print x;}var x=1;f();x=\"hi\";f();" },
};

INTERPRET(GlobalVars, globalVars, 11);

InterpretCase localVars[] = {
  { INTERPRET_COMPILE_ERROR,
      "Already a variable with this name in this scope.",
      "{var x;var x;}" },
  { INTERPRET_COMPILE_ERROR,
      "Can't read local variable in its own initializer.",
      "{var a;{var a=a;}}" },
  { INTERPRET_RUNTIME_ERROR, "Undefined variable 'x'.",
      "{var x;}print x;" },
  { INTERPRET_OK, "nil\n", "{var x;print x;}" },
  { INTERPRET_OK, "nil\n", "{{{var x;print x;}}}" },
  { INTERPRET_OK, "1\n", "{var x=1;print x;}" },
  { INTERPRET_OK, "2\n1\n", "var x=1;{var x=2;print x;}print x;" },
  { INTERPRET_OK, "good\n", "{var x=\"go\";var y=\"od\";print x+y;}" },
  { INTERPRET_OK, "hi\n", "{var x=\"h\";var xx=\"i\";print x+xx;}" },
};

INTERPRET(LocalVars, localVars, 9);

InterpretCase andOr[] = {
  { INTERPRET_OK, "false\n", "print false and 1;" },
  { INTERPRET_OK, "1\n", "print true and 1;" },
  { INTERPRET_OK, "1\n", "print false or 1;" },
  { INTERPRET_OK, "true\n", "print true or 1;" },
  { INTERPRET_OK, "1\n", "print true and false or 1;" },
  { INTERPRET_OK, "1\n", "print false or true and 1;" },
};

INTERPRET(AndOr, andOr, 6);

InterpretCase expressionStmt[] = {
  { INTERPRET_OK, "", "0;" },
  { INTERPRET_OK, "", "nil;" },
  { INTERPRET_OK, "", "true;" },
  { INTERPRET_OK, "", "false;" },
  { INTERPRET_OK, "", "\"foo\";" },
};

INTERPRET(ExpressionStmt, expressionStmt, 5);

InterpretCase forStmt[] = {
  { INTERPRET_COMPILE_ERROR, "Expect '(' after 'for'.", "for" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "for(" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "for()" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "for(;" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "for(;)" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "for(;;)" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "for(;;);" },
  { INTERPRET_OK, "0\n1\n2\n", "for(var i=0;i<3;i=i+1)print i;" },
  { INTERPRET_OK, "0\n1\n2\n3\n",
      "var i;for(i=0;i<3;i=i+1)print i;print i;" },
};

INTERPRET(ForStmt, forStmt, 9);

InterpretCase ifStmt[] = {
  { INTERPRET_COMPILE_ERROR, "Expect '(' after 'if'.", "if" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "if(" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "if()" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "if(0)" },
  { INTERPRET_COMPILE_ERROR, "Expect ';' after expression.", "if(0)1" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "if(0)1;else" },
  { INTERPRET_OK, "1\n", "if(false)print 0;print 1;" },
  { INTERPRET_OK, "0\n1\n", "if(true)print 0;print 1;" },
  { INTERPRET_OK, "1\n2\n", "if(false)print 0;else print 1;print 2;" },
  { INTERPRET_OK, "0\n2\n", "if(true)print 0;else print 1;print 2;" },
  { INTERPRET_OK, "0\n2\n",
      "if(true)print 0;if(false)print 1;else print 2;" },
};

INTERPRET(IfStmt, ifStmt, 11);

InterpretCase whileStmt[] = {
  { INTERPRET_COMPILE_ERROR, "Expect '(' after 'while'.", "while" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "while(" },
  { INTERPRET_COMPILE_ERROR, "Expect ')' after condition.", "while(0" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "while(0)" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "while(0);" },
  { INTERPRET_OK, "", "while(false)print 1;" },
  { INTERPRET_OK, "0\n1\n2\n", "var i=0;while(i<3){print i;i=i+1;}" },
};

INTERPRET(WhileStmt, whileStmt, 7);

InterpretCase classes[] = {
  { INTERPRET_COMPILE_ERROR, "Expect class name.", "class" },
  { INTERPRET_COMPILE_ERROR, "Expect class name.", "class 0" },
  { INTERPRET_COMPILE_ERROR, "Expect '{' before class body.",
      "class F" },
  { INTERPRET_COMPILE_ERROR, "Expect '}' after class body.",
      "class F{" },
  { INTERPRET_COMPILE_ERROR, "Invalid assignment target.",
      "class F{}var f=F();1+f.x=2;" },
  { INTERPRET_RUNTIME_ERROR,
      "Only lists and instances have properties.", "0.x;" },
  { INTERPRET_RUNTIME_ERROR,
      "Only lists and instances have properties.", "class F{}F.x;" },
  { INTERPRET_RUNTIME_ERROR,
      "Can only index lists, maps, strings and instances.",
      "0[\"x\"];" },
  { INTERPRET_RUNTIME_ERROR, "Only instances have fields.", "0.x=1;" },
  { INTERPRET_RUNTIME_ERROR, "Only instances have fields.",
      "\"\".x=1;" },
  { INTERPRET_RUNTIME_ERROR,
      "Can only set index of lists, maps and instances.",
      "0[\"x\"]=1;" },
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x'.",
      "class F{}var f=F();f.x;" },
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x'.",
      "class F{}var f=F();f[\"x\"];" },
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      "class F{}var f=F();f[0];" },
  { INTERPRET_RUNTIME_ERROR, "Instances can only be indexed by string.",
      "class F{}var f=F();f[0]=1;" },
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x1234567'.",
      "class F{}var f=F();f.x1234567;" },
  { INTERPRET_OK, "F\n", "class F{}print F;" },
  { INTERPRET_OK, "F instance\n", "class F{}print F();" },
  { INTERPRET_OK, "1\n1\n",
      "class F{}var f=F();print f.x=1;print f.x;" },
  { INTERPRET_OK, "1\n1\n",
      "class F{}var f=F();print f[\"x\"]=1;print f[\"x\"];" },
};

INTERPRET(Classes, classes, 20);

InterpretCase methods[] = {
  { INTERPRET_COMPILE_ERROR, "Expect method name.", "class F{0}" },
  { INTERPRET_COMPILE_ERROR, "Can't use 'this' outside of a class.",
      "this" },
  { INTERPRET_COMPILE_ERROR, "Can't use 'this' outside of a class.",
      "fun f(){this;}" },
  { INTERPRET_COMPILE_ERROR,
      "Can't return a value from an initializer.",
      "class F{init(){return 0;}}" },
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "class F{}F(0);" },
  { INTERPRET_RUNTIME_ERROR,
      "Only lists, maps, strings and instances have methods.",
      "0.x();" },
  { INTERPRET_RUNTIME_ERROR,
      "Only lists, maps, strings and instances have methods.",
      "class F{}F.x();" },
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x'.",
      "class F{}F().x();" },
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'x1234567'.",
      "class F{}F().x1234567();" },
  { INTERPRET_OK, "<fn x>\n", "class F{x(){}}print F().x;" },
  { INTERPRET_OK, "<fn f>\n", "{var x;class F{f(){x;}}print F().f;}" },
  { INTERPRET_OK, "0\n1\n",
      "class F{x(n){print n;return n+1;}}print F().x(0);" },
  { INTERPRET_OK, "2\n3\n",
      "class F {"
      "  init(x) { this.x = x; }"
      "  get() { return this.x; }"
      "  set(nx) { this.x = nx; }"
      "}"
      "var f = F(2); print f.get(); f.set(3); print f.get();" },
  { INTERPRET_OK, "2\n3\n",
      "class F {"
      "  init(x) { this.x = x; }"
      "  get() { return this.x; }"
      "  set(nx) { this.x = nx; }"
      "}"
      "var f = F(2); var g = f.get; var s = f.set;"
      "print g(); s(3); print g();" },
  { INTERPRET_OK, "not a method\n",
      "class Oops {"
      "  init() {"
      "    fun f() { print \"not a method\"; }"
      "    this.field = f;"
      "  }"
      "}"
      "var oops = Oops(); oops.field();" },
  { INTERPRET_OK, "1\n",
      "class F {"
      "  init() { this.x=1; }"
      "  blah() { fun f() { print this.x; } f(); }"
      "}"
      "F().blah();" },
};

INTERPRET(Methods, methods, 16);

InterpretCase superclasses[] = {
  { INTERPRET_COMPILE_ERROR, "Expect superclass name.", "class A<" },
  { INTERPRET_COMPILE_ERROR, "A class can't inherit from itself.",
      "class A<A" },
  { INTERPRET_COMPILE_ERROR, "Can't use 'super' outside of a class.",
      "super" },
  { INTERPRET_COMPILE_ERROR,
      "Can't use 'super' in a class with no superclass.",
      "class A{f(){super;}}" },
  { INTERPRET_COMPILE_ERROR, "Expect '.' after 'super'.",
      "class A{}class B<A{f(){super;}}" },
  { INTERPRET_COMPILE_ERROR, "Expect superclass method name.",
      "class A{}class B<A{f(){super.;}}" },
  { INTERPRET_RUNTIME_ERROR, "Superclass must be a class.",
      "var A;class B<A{}" },
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'f'.",
      "class A {}"
      "class B < A { f() { super.f(); } }"
      "B().f();" },
  { INTERPRET_RUNTIME_ERROR, "Undefined property 'f'.",
      "class A {}"
      "class B < A { f() { var f = super.f; } }"
      "B().f();" },
  { INTERPRET_OK, "1\n",
      "class A { f() { print 1; } }"
      "class B < A {}"
      "B().f();" },
  { INTERPRET_OK, "2\n",
      "class A { f() { print 1; } }"
      "class B < A { f() { print 2; } }"
      "B().f();" },
  { INTERPRET_OK, "1\n2\n",
      "class A { f() { print 1; } }"
      "class B < A { f() { super.f(); print 2; } }"
      "B().f();" },
  { INTERPRET_OK, "2\n1\n",
      "class A { f() { print 1; } }"
      "class B < A { f() { var f = super.f; print 2; f(); } }"
      "B().f();" },
  { INTERPRET_OK, "A method\n",
      "class A { f() { print \"A method\"; } }"
      "class B < A { f() { print \"B method\"; } g() { super.f(); } }"
      "class C < B {}"
      "C().g();" },
  { INTERPRET_OK, "1\n-2\n",
      "class A { f() { print 1; this.g(2); } g(n) { print n; } }"
      "class B < A { g(n) { super.g(-n); } }"
      "B().f();" },
};

INTERPRET(Superclasses, superclasses, 15);

InterpretCase list[] = {
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "[" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "[," },
  { INTERPRET_COMPILE_ERROR, "Expect ']' after list.", "[0" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "[0," },
  { INTERPRET_RUNTIME_ERROR, "List index must be a number.",
      "[nil][nil];" },
  { INTERPRET_RUNTIME_ERROR, "List index must be a number.",
      "[nil][nil]=1;" },
  { INTERPRET_RUNTIME_ERROR, "List index (-1) out of bounds (1).",
      "[nil][-1];" },
  { INTERPRET_RUNTIME_ERROR, "List index (-1) out of bounds (1).",
      "[nil][-1]=1;" },
  { INTERPRET_RUNTIME_ERROR, "List index (1) out of bounds (1).",
      "[nil][1];" },
  { INTERPRET_RUNTIME_ERROR, "List index (1) out of bounds (1).",
      "[nil][1]=1;" },
  { INTERPRET_RUNTIME_ERROR, "List index (0.5) must be a whole number.",
      "[nil][0.5];" },
  { INTERPRET_RUNTIME_ERROR, "List index (0.5) must be a whole number.",
      "[nil][0.5]=1;" },
  { INTERPRET_OK, "1\n", "print[1][0];" },
  { INTERPRET_OK, "1\n", "print[1,][0];" },
  { INTERPRET_OK, "1\n2\n3\n",
      "var l=[1,1+1,1+1+1];print l[0];print l[1];print l[1+1];" },
  { INTERPRET_OK, "nil\nfalse\ntrue\nhi\n[1, 2, 3]\n",
      "var l=[nil,false,true,\"hi\",[1,2,3]];"
      "print l[0];print l[1];print l[2];print l[3];print l[4];" },
  { INTERPRET_OK, "0\nhi\n",
      "var l=[0];print l[0];l[0]=\"hi\";print l[0];" },
  { INTERPRET_OK, "[1, 2, 3]\n0\n[1, 0, 3]\n",
      "var l=[1,2,3];print l;print l[1]=0;print l;" },
  { INTERPRET_OK, "[1, 2, 3]\n[4, 5, <list 3>]\n[1, 2, 3]\n",
      "var a=[1,2,3];var b=[4,5,a];print a;print b;print b[2];" },
  { INTERPRET_OK, "[1, 2, 3]\n[1, 2, <list 3>]\n",
      "var a=[1,2,3];print a;a[2]=a;print a;" },
};

INTERPRET(List, list, 20);

InterpretCase listInsert[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 2 arguments but got 0.",
      "[].insert();" },
  { INTERPRET_RUNTIME_ERROR, "List index (1) out of bounds (1).",
      "[0].insert(1,0);" },
  { INTERPRET_OK, "[0]\n[true, 0]\n",
      "var l=[0];print l;l.insert(0,true);print l;" },
  { INTERPRET_OK, "[1, 2, 3]\n[1, 2, false, 3]\n",
      "var l=[1,2,3];print l;l.insert(2, false);print l;" },
  { INTERPRET_OK, "[0, 1, 2, 3, 4, 5, 6, 7, 8]\n",
      "var l=[8];for(var i=0;i<8;i=i+1)l.insert(i,i);print l;" },
};

INTERPRET(ListInsert, listInsert, 5);

InterpretCase listPop[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "[].pop(0);" },
  { INTERPRET_RUNTIME_ERROR, "Can't pop from an empty list.",
      "[].pop();" },
  { INTERPRET_OK, "[123]\n123\n[]\n",
      "var l=[123];print l;print l.pop();print l;" },
  { INTERPRET_OK, "7\n6\n5\n4\n3\n2\n1\n0\n[]\n",
      "var l=[0,1,2,3,4,5,6,7];"
      "for(var i=0;i<8;i=i+1)print l.pop();"
      "print l;" },
};

INTERPRET(ListPop, listPop, 4);

InterpretCase listPush[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "[].push();" },
  { INTERPRET_OK, "[]\n[0]\n", "var l=[];print l;l.push(0);print l;" },
  { INTERPRET_OK, "[0, 1, 2, 3, 4, 5, 6, 7]\n",
      "var l=[];for(var i=0;i<8;i=i+1)l.push(i);print l;" },
};

INTERPRET(ListPush, listPush, 3);

InterpretCase listRemove[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "[].remove();" },
  { INTERPRET_RUNTIME_ERROR, "List index (1) out of bounds (1).",
      "[0].remove(1);" },
  { INTERPRET_OK, "[2, 4, 6]\n4\n[2, 6]\n",
      "var l=[2,4,6];print l;print l.remove(1);print l;" },
  { INTERPRET_OK, "0\n1\n2\n3\n4\n5\n6\n7\n[]\n",
      "var l=[0,1,2,3,4,5,6,7];"
      "for(var i=0;i<8;i=i+1)print l.remove(0);"
      "print l;" },
};

INTERPRET(ListRemove, listRemove, 4);

InterpretCase listSize[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "[].size(0);" },
  { INTERPRET_OK, "<native fn>\n", "print[].size;" },
  { INTERPRET_OK, "0\n1\n3\n",
      "var a=[];var b=[0];var c=[0,0,0];"
      "var as=a.size;var bs=b.size;var cs=c.size;"
      "print as();print bs();print cs();" },
  { INTERPRET_OK, "0\n1\n3\n",
      "print[].size();print[0].size();print[0,0,0].size();" },
};

INTERPRET(ListSize, listSize, 4);

InterpretCase map[] = {
  { INTERPRET_COMPILE_ERROR, "Expect identifier or '['.", "({)" },
  { INTERPRET_COMPILE_ERROR, "Expect identifier or '['.", "({,)" },
  { INTERPRET_COMPILE_ERROR, "Expect ':' after map key.", "({a)" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "({a:)" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "({a:,)" },
  { INTERPRET_COMPILE_ERROR, "Expect '}' after map.", "({a:1)" },
  { INTERPRET_COMPILE_ERROR, "Expect identifier or '['.", "({a:1,)" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "({[)" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "({[])" },
  { INTERPRET_COMPILE_ERROR, "Expect ']' after expression.",
      "({[\"a\")" },
  { INTERPRET_COMPILE_ERROR, "Expect ':' after map key.",
      "({[\"a\"])" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}[nil]);" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}[{}]);" },
  { INTERPRET_RUNTIME_ERROR, "Undefined key 'a'.", "({}[\"a\"]);" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}[nil]=nil);" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}[{}]=nil);" },
  { INTERPRET_OK, "{}\n", "print{};" },
  { INTERPRET_OK, "{a: 1}\n", "print{a:1};" },
  { INTERPRET_OK, "{a: 1}\n", "print{a:1,};" },
  { INTERPRET_OK, "{ab: 3}\n", "print{[\"a\"+\"b\"]:1+2,};" },
  { INTERPRET_OK, "{a: 1, b: 2}\n", "print{a:1,b:2};" },
  { INTERPRET_OK, "{a: 1, b: <map>}\n", "print{a:1,b:{c:2}};" },
  { INTERPRET_OK, "1\n2\n",
      "var m={a:1,b:2};print m[\"a\"];print m[\"b\"];" },
  { INTERPRET_OK, "1\n2\n1\n2\n",
      "var m={};print m[\"a\"]=1;print m[\"b\"]=2;"
      "print m[\"a\"];print m[\"b\"];" },
};

INTERPRET(Map, map, 24);

InterpretCase mapCount[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "({}).count(nil);" },
  { INTERPRET_OK, "0\n", "print{}.count();" },
  { INTERPRET_OK, "3\n", "print{a:4,b:5,c:6}.count();" },
};

INTERPRET(MapCount, mapCount, 3);

InterpretCase mapHas[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "({}).has();" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}).has(nil);" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}).has({});" },
  { INTERPRET_OK, "false\n", "print{}.has(\"\");" },
  { INTERPRET_OK, "true\n", "print{a:1}.has(\"a\");" },
  { INTERPRET_OK, "false\n", "print{a:1}.has(\"b\");" },
  { INTERPRET_OK, "true\n", "var ha={a:1}.has;print ha(\"a\");" },
};

INTERPRET(MapHas, mapHas, 7);

InterpretCase mapKeys[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 0 arguments but got 1.",
      "({}).keys(nil);" },
  { INTERPRET_OK, "0\n", "print{}.keys().size();" },
  { INTERPRET_OK, "1\n", "print{a:1}.keys().size();" },
  { INTERPRET_OK, "3\n", "print{a:1,b:2,c:3}.keys().size();" },
  { INTERPRET_OK, "6\n",
      "var m={a:1,b:2,c:3};var ks=m.keys();var sum=0;"
      "for(var i=0;i<ks.size();i=i+1)sum=sum+m[ks[i]];"
      "print sum;" },
};

INTERPRET(MapKeys, mapKeys, 5);

InterpretCase mapRemove[] = {
  { INTERPRET_RUNTIME_ERROR, "Expected 1 arguments but got 0.",
      "({}).remove();" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}).remove(nil);" },
  { INTERPRET_RUNTIME_ERROR, "Maps can only be indexed by string.",
      "({}).remove({});" },
  { INTERPRET_OK, "false\n", "print{}.remove(\"\");" },
  { INTERPRET_OK, "{a: 1}\ntrue\n{}\n",
      "var m={a:1};print m;print m.remove(\"a\");print m;" },
  { INTERPRET_OK, "{a: 1, b: 2}\n{a: 1}\n",
      "var m={a:1,b:2};print m;m.remove(\"b\");print m;" },
};

INTERPRET(MapRemove, mapRemove, 6);

UTEST_STATE();

int main(int argc, const char* argv[]) {
  debugStressGC = true;
  return utest_main(argc, argv);
}
