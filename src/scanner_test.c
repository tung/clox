#include "scanner.h"

#include <assert.h>

#include "utest.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

typedef struct {
  int lines;
  const char* str;
} LinesInString;

struct ScanCountLines {
  LinesInString* cases;
};

UTEST_I_SETUP(ScanCountLines) {
  (void)utest_fixture;
  (void)utest_index;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(ScanCountLines) {
  Scanner s;
  Token t;
  initScanner(&s, utest_fixture->cases[utest_index].str);
  do {
    t = scanToken(&s);
  } while (t.type != TOKEN_EOF && t.type != TOKEN_ERROR);
  ASSERT_EQ((TokenType)TOKEN_EOF, t.type);
  EXPECT_EQ(utest_fixture->cases[utest_index].lines, t.line);
}

LinesInString linesInString[] = {
  { 1, "" },
  { 2, "\n" },
  { 3, "\n\n" },
  { 1, "//" },
  { 2, "//\n" },
  { 3, "//\n//\n" },
  { 1, "\"\"" },
  { 2, "\"\"\n" },
  { 3, "\"\n\"\n" },
};

UTEST_I(ScanCountLines, LinesInString, 9) {
  static_assert(ARRAY_SIZE(linesInString) == 9, "LinesInString");
  utest_fixture->cases = linesInString;
  ASSERT_TRUE(1);
}

typedef struct {
  const char* str;
  TokenType* tts;
} StringToTokenTypes;

struct ScanTokenTypes {
  StringToTokenTypes* cases;
};

UTEST_I_SETUP(ScanTokenTypes) {
  (void)utest_fixture;
  (void)utest_index;
  ASSERT_TRUE(1);
}

UTEST_I_TEARDOWN(ScanTokenTypes) {
  Scanner s;
  Token t;
  TokenType* tt = utest_fixture->cases[utest_index].tts;
  initScanner(&s, utest_fixture->cases[utest_index].str);
  do {
    t = scanToken(&s);
    ASSERT_EQ(*tt, t.type);
    tt++;
  } while (tt[-1] != TOKEN_EOF && tt[-1] != TOKEN_ERROR &&
      t.type != TOKEN_EOF && t.type != TOKEN_ERROR);
}

#define SCAN_TOKEN_TYPES(name, data, count) \
  UTEST_I(ScanTokenTypes, name, count) { \
    static_assert(ARRAY_SIZE(data) == count, #name); \
    utest_fixture->cases = data; \
    ASSERT_TRUE(1); \
  }

StringToTokenTypes noContents[] = {
  { "", (TokenType[]){ TOKEN_EOF } },
  { "#", (TokenType[]){ TOKEN_ERROR } },
};

SCAN_TOKEN_TYPES(NoContents, noContents, 2);

StringToTokenTypes single[] = {
  { "(", (TokenType[]){ TOKEN_LEFT_PAREN, TOKEN_EOF } },
  { ")", (TokenType[]){ TOKEN_RIGHT_PAREN, TOKEN_EOF } },
  { "{", (TokenType[]){ TOKEN_LEFT_BRACE, TOKEN_EOF } },
  { "}", (TokenType[]){ TOKEN_RIGHT_BRACE, TOKEN_EOF } },
  { ";", (TokenType[]){ TOKEN_SEMICOLON, TOKEN_EOF } },
  { ":", (TokenType[]){ TOKEN_COLON, TOKEN_EOF } },
  { ",", (TokenType[]){ TOKEN_COMMA, TOKEN_EOF } },
  { ".", (TokenType[]){ TOKEN_DOT, TOKEN_EOF } },
  { "-", (TokenType[]){ TOKEN_MINUS, TOKEN_EOF } },
  { "+", (TokenType[]){ TOKEN_PLUS, TOKEN_EOF } },
  { "/", (TokenType[]){ TOKEN_SLASH, TOKEN_EOF } },
  { "*", (TokenType[]){ TOKEN_STAR, TOKEN_EOF } },
};

SCAN_TOKEN_TYPES(Single, single, 12);

StringToTokenTypes oneOrTwo[] = {
  { "!", (TokenType[]){ TOKEN_BANG, TOKEN_EOF } },
  { "!!", (TokenType[]){ TOKEN_BANG, TOKEN_BANG, TOKEN_EOF } },
  { "!=", (TokenType[]){ TOKEN_BANG_EQUAL, TOKEN_EOF } },
  { "=", (TokenType[]){ TOKEN_EQUAL, TOKEN_EOF } },
  { "==", (TokenType[]){ TOKEN_EQUAL_EQUAL, TOKEN_EOF } },
  { "<", (TokenType[]){ TOKEN_LESS, TOKEN_EOF } },
  { "<<", (TokenType[]){ TOKEN_LESS, TOKEN_LESS, TOKEN_EOF } },
  { "<=", (TokenType[]){ TOKEN_LESS_EQUAL, TOKEN_EOF } },
  { ">", (TokenType[]){ TOKEN_GREATER, TOKEN_EOF } },
  { ">>", (TokenType[]){ TOKEN_GREATER, TOKEN_GREATER, TOKEN_EOF } },
  { ">=", (TokenType[]){ TOKEN_GREATER_EQUAL, TOKEN_EOF } },
};

SCAN_TOKEN_TYPES(OneOrTwo, oneOrTwo, 11);

StringToTokenTypes whitespace[] = {
  { "   ", (TokenType[]){ TOKEN_EOF } },
  { "\r\r\r", (TokenType[]){ TOKEN_EOF } },
  { "\t\t\t", (TokenType[]){ TOKEN_EOF } },
  { "\n\n\n", (TokenType[]){ TOKEN_EOF } },
};

SCAN_TOKEN_TYPES(Whitespace, whitespace, 4);

StringToTokenTypes comments[] = {
  { "//", (TokenType[]){ TOKEN_EOF } },
  { ".//", (TokenType[]){ TOKEN_DOT, TOKEN_EOF } },
  { "//.", (TokenType[]){ TOKEN_EOF } },
  { "//\n.", (TokenType[]){ TOKEN_DOT, TOKEN_EOF } },
  { ".//.\n.", (TokenType[]){ TOKEN_DOT, TOKEN_DOT, TOKEN_EOF } },
};

SCAN_TOKEN_TYPES(Comments, comments, 5);

StringToTokenTypes strings[] = {
  { "\"\"", (TokenType[]){ TOKEN_STRING, TOKEN_EOF } },
  { "\"foo\"", (TokenType[]){ TOKEN_STRING, TOKEN_EOF } },
  { "\"", (TokenType[]){ TOKEN_ERROR } },
  { "\"\n\"", (TokenType[]){ TOKEN_STRING, TOKEN_EOF } },
  { "\"foo\nbar\"", (TokenType[]){ TOKEN_STRING, TOKEN_EOF } },
  { "\"\n", (TokenType[]){ TOKEN_ERROR } },
};

SCAN_TOKEN_TYPES(Strings, strings, 6);

StringToTokenTypes numbers[] = {
  { "0", (TokenType[]){ TOKEN_NUMBER, TOKEN_EOF } },
  { "1234567890", (TokenType[]){ TOKEN_NUMBER, TOKEN_EOF } },
  { "0.0", (TokenType[]){ TOKEN_NUMBER, TOKEN_EOF } },
  { "0.", (TokenType[]){ TOKEN_NUMBER, TOKEN_DOT, TOKEN_EOF } },
};

SCAN_TOKEN_TYPES(Numbers, numbers, 4);

StringToTokenTypes identifiers[] = {
  { "c", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "cc", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "f", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "fee", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "foe", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "s", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "ss", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "t", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "to", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "___", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "abcdefghijklmnopqrstuvwxyz",
      (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
      (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
  { "_0123456789", (TokenType[]){ TOKEN_IDENTIFIER, TOKEN_EOF } },
};

SCAN_TOKEN_TYPES(Identifiers, identifiers, 13);

StringToTokenTypes keywords[] = {
  { "and", (TokenType[]){ TOKEN_AND, TOKEN_EOF } },
  { "case", (TokenType[]){ TOKEN_CASE, TOKEN_EOF } },
  { "class", (TokenType[]){ TOKEN_CLASS, TOKEN_EOF } },
  { "default", (TokenType[]){ TOKEN_DEFAULT, TOKEN_EOF } },
  { "else", (TokenType[]){ TOKEN_ELSE, TOKEN_EOF } },
  { "false", (TokenType[]){ TOKEN_FALSE, TOKEN_EOF } },
  { "for", (TokenType[]){ TOKEN_FOR, TOKEN_EOF } },
  { "fun", (TokenType[]){ TOKEN_FUN, TOKEN_EOF } },
  { "if", (TokenType[]){ TOKEN_IF, TOKEN_EOF } },
  { "nil", (TokenType[]){ TOKEN_NIL, TOKEN_EOF } },
  { "or", (TokenType[]){ TOKEN_OR, TOKEN_EOF } },
  { "print", (TokenType[]){ TOKEN_PRINT, TOKEN_EOF } },
  { "return", (TokenType[]){ TOKEN_RETURN, TOKEN_EOF } },
  { "super", (TokenType[]){ TOKEN_SUPER, TOKEN_EOF } },
  { "switch", (TokenType[]){ TOKEN_SWITCH, TOKEN_EOF } },
  { "this", (TokenType[]){ TOKEN_THIS, TOKEN_EOF } },
  { "true", (TokenType[]){ TOKEN_TRUE, TOKEN_EOF } },
  { "var", (TokenType[]){ TOKEN_VAR, TOKEN_EOF } },
  { "while", (TokenType[]){ TOKEN_WHILE, TOKEN_EOF } },
};

SCAN_TOKEN_TYPES(Keywords, keywords, 19);

UTEST_MAIN();
