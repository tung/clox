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

static void printVersion(FILE* fout) {
  fputs("clox " XSTR(VERSION) "\n", fout);
}

static void repl(int argc, const char* argv[]) {
  VM vm;
  initVM(&vm, stdout, stderr);
  argsVM(&vm, argc, argv);

  const char prefix[] = "print";
  const size_t inputStart = sizeof(prefix) - 1;

  MemBuf src;
  initMemBuf(&src);
  fputs(prefix, src.fptr);
  fflush(src.fptr);

  linenoiseSetMultiLine(1);
  linenoiseHistorySetMaxLen(100);

  printVersion(stdout);

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

static void runFile(const char* path, int argc, const char* argv[]) {
  VM vm;
  MemBuf source;
  InterpretResult result;

  initVM(&vm, stdout, stderr);
  argsVM(&vm, argc, argv);
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

static void printHelp(FILE* fout) {
  printVersion(fout);
  fputs(
      "\n"
      "Usage: clox [options] [path]\n"
      "\n"
      "   -D, --dump\t\t(debug) Dump disassembled script\n"
      "   -T, --trace\t\t(debug) Trace script execution\n"
      "   -L, --log-gc\t\t(debug) Log garbage collector\n"
      "   -S, --stress-gc\t(debug) Always collect garbage\n"
      "   -h, -?, --help\tShow help (this message) and exit\n"
      "   -v, --version\tShow version information and exit\n",
      fout);
}

int main(int argc, const char* argv[]) {
  const char* argv0 = argv[0];
  const char* script = NULL;

  while (argc > 1) {
    if (!strcmp(argv[1], "--")) {
      argv++;
      argc--;
      break;
    } else if (!strcmp(argv[1], "--version")) {
      printVersion(stdout);
      return 0;
    } else if (!strcmp(argv[1], "--help")) {
      printHelp(stdout);
      return 0;
    } else if (!strcmp(argv[1], "--dump")) {
      debugPrintCode = true;
    } else if (!strcmp(argv[1], "--trace")) {
      debugTraceExecution = true;
    } else if (!strcmp(argv[1], "--log-gc")) {
      debugLogGC = true;
    } else if (!strcmp(argv[1], "--stress-gc")) {
      debugStressGC = true;
    } else if (!strncmp(argv[1], "--", 2)) {
      fprintf(stderr, "Unknown option: '%s'\n", argv[1]);
      printHelp(stderr);
      return 1;
    } else if (argv[1][0] == '-') {
      if (!argv[1][1]) {
        script = argv[1];
        break;
      }
      for (const char* a = argv[1] + 1; *a; ++a) {
        switch (*a) {
          case 'v': printVersion(stdout); return 0;
          case '?':
          case 'h': printHelp(stdout); return 0;
          case 'D': debugPrintCode = true; break;
          case 'T': debugTraceExecution = true; break;
          case 'L': debugLogGC = true; break;
          case 'S': debugStressGC = true; break;
          default:
            fprintf(stderr, "Unknown option: '%c'\n", *a);
            printHelp(stderr);
            return 1;
        }
      }
    } else {
      script = argv[1];
      break;
    }
    argv++;
    argc--;
  }

  argv[0] = argv0;

  if (script) {
    runFile(script, argc, argv);
  } else {
    repl(argc, argv);
  }

  return 0;
}
