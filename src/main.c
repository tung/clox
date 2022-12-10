#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linenoise.h"

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "membuf.h"
#include "memory.h"
#include "vm.h"

#define STR(x) #x
#define XSTR(x) STR(x)

static void printVersion(void) {
  puts("clox " XSTR(VERSION));
}

static void repl(void) {
  VM vm;
  initVM(&vm, stdout, stderr);

  const char prefix[] = "print";
  const size_t inputStart = sizeof(prefix) - 1;

  MemBuf src;
  initMemBuf(&src);
  fputs(prefix, src.fptr);
  fflush(src.fptr);

  linenoiseSetMultiLine(1);
  linenoiseHistorySetMaxLen(100);

  printVersion();

  for (;;) {
    const char* prompt = src.buf && src.buf[inputStart] ? "" : "> ";
    char* line;
    if ((line = linenoise(prompt)) == NULL) {
      freeVM(&vm);
      freeMemBuf(&src);
      break;
    }

    size_t len = strlen(line);
    if (len >= 1 && line[len - 1] == '\\') {
      fprintf(src.fptr, "%.*s\n", (int)len - 1, line);
      fflush(src.fptr);
    } else {
      fputs(line, src.fptr);
      fputc('\n', src.fptr);
      fflush(src.fptr);

      if (src.buf[inputStart] == '=') {
        src.buf[inputStart] = ' ';
        fputc(';', src.fptr);
        fflush(src.fptr);
        interpret(&vm, src.buf);
      } else {
        interpret(&vm, src.buf + inputStart);
      }

      freeMemBuf(&src);
      initMemBuf(&src);
      fputs(prefix, src.fptr);
      fflush(src.fptr);
    }

    linenoiseHistoryAdd(line);
    linenoiseFree(line);
  }
}

static void readFile(MemBuf* mbuf, const char* path) {
  FILE* file;
  if (!strncmp("-", path, sizeof("-"))) {
    file = stdin;
  } else {
    file = fopen(path, "rb");
    if (file == NULL) {
      fprintf(stderr, "Could not open file '%s'.\n", path);
      perror("fopen");
      exit(74);
    }
  }

  char tmp[1024];
  size_t num;
  while ((num = fread(&tmp, 1, sizeof(tmp), file)) > 0) {
    fwrite(&tmp, 1, num, mbuf->fptr);
  }
  fflush(mbuf->fptr);

  if (ferror(file)) {
    fprintf(stderr, "Error reading file '%s'.\n", path);
    perror("fread");
    fclose(file);
    exit(74);
  }

  fclose(file);
}

static void runFile(const char* path) {
  VM vm;
  MemBuf source;
  InterpretResult result;

  initVM(&vm, stdout, stderr);
  initMemBuf(&source);
  readFile(&source, path);
  result = interpret(&vm, source.buf);
  freeMemBuf(&source);
  freeVM(&vm);

  if (result == INTERPRET_COMPILE_ERROR) {
    exit(65);
  }
  if (result == INTERPRET_RUNTIME_ERROR) {
    exit(70);
  }
}

int main(int argc, char* argv[]) {
  while (argc > 1) {
    if (!strcmp(argv[1], "--version")) {
      printVersion();
      return 0;
    } else if (!strcmp(argv[1], "--dump")) {
      debugPrintCode = true;
    } else if (!strcmp(argv[1], "--trace")) {
      debugTraceExecution = true;
    } else if (!strcmp(argv[1], "--log-gc")) {
      debugLogGC = true;
    } else if (!strcmp(argv[1], "--stress-gc")) {
      debugStressGC = true;
    } else {
      break;
    }
    argv++;
    argc--;
  }

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: clox [path]\n");
  }

  return 0;
}
