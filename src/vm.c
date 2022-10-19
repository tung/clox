#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"

static void resetStack(VM* vm) {
  vm->stackTop = vm->stack;
}

void initVM(VM* vm) {
  resetStack(vm);
}

void freeVM(VM* vm) {
  (void)vm;
}

void push(VM* vm, Value value) {
  assert(vm->stackTop < vm->stack + STACK_MAX);
  *vm->stackTop = value;
  vm->stackTop++;
}

Value pop(VM* vm) {
  assert(vm->stackTop > vm->stack);
  vm->stackTop--;
  return *vm->stackTop;
}

static InterpretResult run(FILE* fout, FILE* ferr, VM* vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op) \
  do { \
    double b = pop(vm); \
    double a = pop(vm); \
    push(vm, a op b); \
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
      case OP_ADD: BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE: BINARY_OP(/); break;
      case OP_NEGATE: push(vm, -pop(vm)); break;
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

InterpretResult interpret(FILE* fout, const char* source) {
  compile(fout, source);
  return INTERPRET_OK;
}
