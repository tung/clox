#include "vm.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"

static void resetStack(VM* vm) {
  vm->stackTop = vm->stack;
}

static void runtimeError(FILE* ferr, VM* vm, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(ferr, format, args);
  va_end(args);
  fputs("\n", ferr);

  size_t instruction = vm->ip - vm->chunk->code - 1;
  int line = vm->chunk->lines[instruction];
  fprintf(ferr, "[line %d] in script\n", line);
  resetStack(vm);
}

void initVM(VM* vm) {
  resetStack(vm);
}

void freeVM(VM* vm) {
  (void)vm;
}

void push(VM* vm, Value value) {
  assert(vm->stackTop < vm->stack + STACK_MAX); // GCOV_EXCL_LINE
  *vm->stackTop = value;
  vm->stackTop++;
}

Value pop(VM* vm) {
  assert(vm->stackTop > vm->stack); // GCOV_EXCL_LINE
  vm->stackTop--;
  return *vm->stackTop;
}

static Value peek(VM* vm, int distance) {
  assert(distance < vm->stackTop - vm->stack); // GCOV_EXCL_LINE
  return vm->stackTop[-1 - distance];
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run(FILE* fout, FILE* ferr, VM* vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
#define BINARY_OP(valueType, op) \
  do { \
    if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
      runtimeError(ferr, vm, "Operands must be numbers."); \
      return INTERPRET_RUNTIME_ERROR; \
    } \
    double b = AS_NUMBER(pop(vm)); \
    double a = AS_NUMBER(pop(vm)); \
    push(vm, valueType(a op b)); \
  } while (false)

  const uint8_t* codeEnd = &vm->chunk->code[vm->chunk->count];
  while (vm->ip < codeEnd) {
#ifdef DEBUG_TRACE_EXECUTION
    fprintf(fout, "          ");
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
      fprintf(fout, "[ ");
      printValue(fout, *slot);
      fprintf(fout, " ]");
    }
    fprintf(fout, "\n");
    disassembleInstruction(
        fout, ferr, vm->chunk, (int)(vm->ip - vm->chunk->code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(vm, constant);
        break;
      }
      case OP_NIL: push(vm, NIL_VAL); break;
      case OP_TRUE: push(vm, BOOL_VAL(true)); break;
      case OP_FALSE: push(vm, BOOL_VAL(false)); break;
      case OP_EQUAL: {
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm, BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: BINARY_OP(NUMBER_VAL, +); break;
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT: push(vm, BOOL_VAL(isFalsey(pop(vm)))); break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek(vm, 0))) {
          runtimeError(ferr, vm, "Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
        break;
      case OP_RETURN: {
        printValue(fout, pop(vm));
        fprintf(fout, "\n");
        return INTERPRET_OK;
      }
      default: {
        fprintf(ferr, "Unknown opcode %d\n", instruction);
        return INTERPRET_RUNTIME_ERROR;
      }
    }
  }

  fprintf(ferr, "missing OP_RETURN\n");
  return INTERPRET_RUNTIME_ERROR;

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpretChunk(
    FILE* fout, FILE* ferr, VM* vm, Chunk* chunk) {
  vm->chunk = chunk;
  vm->ip = vm->chunk->code;
  return run(fout, ferr, vm);
}

InterpretResult interpret(
    FILE* fout, FILE* ferr, VM* vm, const char* source) {
  Chunk chunk;
  initChunk(&chunk);

  if (!compile(fout, ferr, source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILE_ERROR;
  }

  InterpretResult result = interpretChunk(fout, ferr, vm, &chunk);

  vm->chunk = NULL;
  vm->ip = NULL;
  freeChunk(&chunk);
  return result;
}
