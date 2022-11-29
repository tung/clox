#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "membuf.h"
#include "memory.h"
#include "vm.h"

static void repl(void) {
  VM vm;
  initVM(&vm, stdout, stderr);

  char line[1024];
  for (;;) {
    printf("> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      freeVM(&vm);
      break;
    }

    if (line[0] == '=') {
      MemBuf temp;
      initMemBuf(&temp);

      char* newLine = strchr(line, '\n');
      if (newLine) {
        *newLine = '\0';
      }

      fputs("print ", temp.fptr);
      fputs(line + 1, temp.fptr);
      fputs(";", temp.fptr);

      fflush(temp.fptr);
      interpret(&vm, temp.buf);

      freeMemBuf(&temp);
      continue;
    }

    interpret(&vm, line);
  }
}

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

static void runFile(const char* path) {
  VM vm;
  initVM(&vm, stdout, stderr);
  char* source = readFile(path);
  InterpretResult result = interpret(&vm, source);
  free(source);
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
    if (!strncmp("--dump", argv[1], sizeof("--dump"))) {
      debugPrintCode = true;
    } else if (!strncmp("--trace", argv[1], sizeof("--trace"))) {
      debugTraceExecution = true;
    } else if (!strncmp("--log-gc", argv[1], sizeof("--log-gc"))) {
      debugLogGC = true;
    } else if (!strncmp(
                   "--stress-gc", argv[1], sizeof("--stress-gc"))) {
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
