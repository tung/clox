#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "memory.h"

static void resetStack(VM* vm) {
  vm->stack = NULL;
  vm->stackCount = 0;
  vm->stackCapacity = 0;
}

void initVM(VM* vm) {
  resetStack(vm);
}

void freeVM(VM* vm) {
  FREE_ARRAY(Value, vm->stack, vm->stackCapacity);
  resetStack(vm);
}

void push(VM* vm, Value value) {
  if (vm->stackCapacity < vm->stackCount + 1) {
    int oldCapacity = vm->stackCapacity;
    vm->stackCapacity = GROW_CAPACITY(oldCapacity);
    vm->stack =
        GROW_ARRAY(Value, vm->stack, oldCapacity, vm->stackCapacity);
  }

  vm->stack[vm->stackCount] = value;
  vm->stackCount++;
}

Value pop(VM* vm) {
  assert(vm->stackCount > 0);
  vm->stackCount--;
  return vm->stack[vm->stackCount];
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
    for (size_t i = 0; i < vm->stackCount; ++i) {
      fprintf(fout, "[ ");
      printValue(fout, vm->stack[i]);
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

InterpretResult interpret(
    FILE* fout, FILE* ferr, VM* vm, Chunk* chunk) {
  vm->chunk = chunk;
  vm->ip = vm->chunk->code;
  return run(fout, ferr, vm);
}
