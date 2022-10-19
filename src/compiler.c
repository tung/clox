#include "compiler.h"

#include <stdio.h>

#include "common.h"
#include "scanner.h"

void compile(FILE* fout, const char* source) {
  Scanner scanner;
  initScanner(&scanner, source);
  int line = -1;
  for (;;) {
    Token token = scanToken(&scanner);
    if (token.line != line) {
      fprintf(fout, "%4d ", token.line);
      line = token.line;
    } else {
      fprintf(fout, "   | ");
    }
    fprintf(
        fout, "%2d '%.*s'\n", token.type, token.length, token.start);

    if (token.type == TOKEN_EOF) {
      break;
    }
  }
}
