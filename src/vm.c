#include "vm.h"

#include <stdio.h>

#include "common.h"
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
  *vm->stackTop = value;
  vm->stackTop++;
}

Value pop(VM* vm) {
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

  for (;;) {
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
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(
    FILE* fout, FILE* ferr, VM* vm, Chunk* chunk) {
  vm->chunk = chunk;
  vm->ip = vm->chunk->code;
  return run(fout, ferr, vm);
}
