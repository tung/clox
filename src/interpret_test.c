#include <assert.h>
#include <string.h>

#include "utest.h"

#include "membuf.h"
#include "vm.h"

#define ufx utest_fixture

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
      const char* errMsg = strstr(err.buf, "[line ");
      if (errMsg) {
        const char* errMsgEnd = strchr(errMsg, '\n');
        if (!errMsgEnd) {
          errMsgEnd = strchr(errMsg, '\0');
        }
        EXPECT_STRNEQ("", errMsg, errMsgEnd - errMsg);
      }
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
    const char* errMsg = strstr(err.buf, "[line ");
    if (errMsg) {
      const char* errMsgEnd = strchr(errMsg, '\n');
      if (!errMsgEnd) {
        errMsgEnd = strchr(errMsg, '\0');
      }
      EXPECT_STRNEQ("", errMsg, errMsgEnd - errMsg);
    }
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
};

INTERPRET(Error, error, 1);

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
  { INTERPRET_OK, "-2.5\n", "print 1+2*3/4-5;" },
};

INTERPRET(BinaryNum, binaryNum, 13);

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
  { INTERPRET_OK, "\n", "print \"\";" },
  { INTERPRET_OK, "foo\n", "print \"foo\";" },
  { INTERPRET_OK, "foo\n", "print \"\"+\"foo\";" },
  { INTERPRET_OK, "foo\n", "print \"foo\"+\"\";" },
  { INTERPRET_OK, "foobar\n", "print \"foo\"+\"bar\";" },
  { INTERPRET_OK, "true\n", "print \"foobar\"==\"foo\"+\"bar\";" },
};

INTERPRET(Strings, strings, 6);

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
};

INTERPRET(GlobalVars, globalVars, 9);

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

InterpretCase continueStmt[] = {
  { INTERPRET_COMPILE_ERROR, "Cannot 'continue' outside of a loop.",
      "continue" },
  { INTERPRET_COMPILE_ERROR, "Cannot 'continue' outside of a loop.",
      "continue;" },
  { INTERPRET_COMPILE_ERROR, "Cannot 'continue' outside of a loop.",
      "while(nil)0;continue;" },
  { INTERPRET_OK, "1\n3\n5\n",
      "var x;for(var i=0;i<6;i=i+1){x=!x;if(x)continue;print i;}" },
  { INTERPRET_OK, "1\n",
      "var x=true;while(x){x=false;continue;print 0;}print 1;" },
  { INTERPRET_OK, "1\n",
      "var x=true;for(;x;){x=false;continue;print 0;}print 1;" },
  { INTERPRET_OK, "1\n",
      "for(var x=true;x;){x=false;continue;print 0;}print 1;" },
  { INTERPRET_OK, "2\n4\n6\n",
      "var i=0;var x;while(i<6){i=i+1;x=!x;if(x)continue;print i;}" },
  { INTERPRET_OK, "2\n4\n6\n",
      "var i=0;var x;for(;i<6;){i=i+1;x=!x;if(x)continue;print i;}" },
  { INTERPRET_OK, "2\n4\n6\n",
      "var x;for(var i=0;i<6;){i=i+1;x=!x;if(x)continue;print i;}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "var i=0;while(i<6){var x=i;i=i+1;if(x<3)continue;print x;}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "var i=0;for(;i<6;){var x=i;i=i+1;if(x<3)continue;print x;}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "for(var i=0;i<6;){var x=i;i=i+1;if(x<3)continue;print x;}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "var i=0;"
      "while(i<6){var y;{var x=i;i=i+1;if(x<3)continue;print x;}}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "var i=0;"
      "for(;i<6;){var y;{var x=i;i=i+1;if(x<3)continue;print x;}}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "for(var i=0;i<6;)"
      "{var y;{var x=i;i=i+1;if(x<3)continue;print x;}}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "var i=0;"
      "{var y;while(i<6){var x=i;i=i+1;if(x<3)continue;print x;}}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "var i=0;"
      "{var y;for(;i<6;){var x=i;i=i+1;if(x<3)continue;print x;}}" },
  { INTERPRET_OK, "3\n4\n5\n",
      "{var y;"
      "for(var i=0;i<6;){var x=i;i=i+1;if(x<3)continue;print x;}}" },
};

INTERPRET(ContinueStmt, continueStmt, 19);

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

InterpretCase switchStmt[] = {
  { INTERPRET_COMPILE_ERROR, "Expect '(' after 'switch'.", "switch" },
  { INTERPRET_COMPILE_ERROR, "Expect ')' after expression.",
      "switch(0" },
  { INTERPRET_COMPILE_ERROR, "Expect '{' for switch body.",
      "switch(0)" },
  { INTERPRET_COMPILE_ERROR, "Expect '}' after switch body.",
      "switch(0){" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "switch(0){case" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.", "switch(0){case}" },
  { INTERPRET_COMPILE_ERROR, "Expect ':' after case expression.",
      "switch(0){case 0}" },
  { INTERPRET_COMPILE_ERROR, "Expect ':' after 'default'.",
      "switch(0){default" },
  { INTERPRET_COMPILE_ERROR, "Expect ':' after 'default'.",
      "switch(0){default}" },
  { INTERPRET_COMPILE_ERROR, "Expect expression.",
      "switch(0){default:case 0:}" },
  { INTERPRET_OK, "", "switch(0){}" },
  { INTERPRET_OK, "", "switch(0){case 0:}" },
  { INTERPRET_OK, "", "switch(0){case nil:}" },
  { INTERPRET_OK, "", "switch(0){case 0:case nil:}" },
  { INTERPRET_OK, "", "switch(0){default:}" },
  { INTERPRET_OK, "", "switch(0){case 0:default:}" },
  { INTERPRET_OK, "a\n", "var x=0;switch(x){case 0:print \"a\";}" },
  { INTERPRET_OK, "", "var x=1;switch(x){case 0:print \"a\";}" },
  { INTERPRET_OK, "a\n",
      "var x=0;switch(x){case 0:print \"a\";default:print \"z\";}" },
  { INTERPRET_OK, "z\n",
      "var x=1;switch(x){case 0:print \"a\";default:print \"z\";}" },
  { INTERPRET_OK, "9\n9\n",
      "switch(0){case 0:print 9;print 9;case 1:print 8;print 8;}" },
  { INTERPRET_OK, "8\n8\n",
      "switch(1){case 0:print 9;print 9;case 1:print 8;print 8;}" },
  { INTERPRET_OK, "7\n7\n",
      "switch(2){case 0:print 9;default:print 7;print 7;}" },
};

INTERPRET(SwitchStmt, switchStmt, 23);

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

UTEST_MAIN();
