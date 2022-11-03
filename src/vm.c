#include "vm.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"

static void resetStack(VM* vm) {
  vm->stackTop = vm->stack;
}

static void runtimeError(VM* vm, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(vm->ferr, format, args);
  va_end(args);
  fputs("\n", vm->ferr);

  size_t instruction = vm->ip - vm->chunk->code - 1;
  int line = vm->chunk->lines[instruction];
  fprintf(vm->ferr, "[line %d] in script\n", line);
  resetStack(vm);
}

void initVM(VM* vm, FILE* fout, FILE* ferr) {
  vm->fout = fout;
  vm->ferr = ferr;
  resetStack(vm);
  vm->objects = NULL;
  initTable(&vm->globals, 0.75);
  initTable(&vm->strings, 0.75);
}

void freeVM(VM* vm) {
  freeTable(&vm->globals);
  freeTable(&vm->strings);
  freeObjects(vm->objects);
  vm->objects = NULL;
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

static void concatenate(VM* vm) {
  ObjString* b = AS_STRING(pop(vm));
  ObjString* a = AS_STRING(pop(vm));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result =
      takeString(&vm->objects, &vm->strings, chars, length);
  push(vm, OBJ_VAL(result));
}

static InterpretResult run(VM* vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT() (vm->chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
  do { \
    if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
      runtimeError(vm, "Operands must be numbers."); \
      return INTERPRET_RUNTIME_ERROR; \
    } \
    double b = AS_NUMBER(pop(vm)); \
    double a = AS_NUMBER(pop(vm)); \
    push(vm, valueType(a op b)); \
  } while (false)

  const uint8_t* codeEnd = &vm->chunk->code[vm->chunk->count];
  while (vm->ip < codeEnd) {
#ifdef DEBUG_TRACE_EXECUTION
    fprintf(vm->ferr, "          ");
    for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
      fprintf(vm->ferr, "[ ");
      printValue(vm->ferr, *slot);
      fprintf(vm->ferr, " ]");
    }
    fprintf(vm->ferr, "\n");
    disassembleInstruction(
        vm->ferr, vm->chunk, (int)(vm->ip - vm->chunk->code));
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
      case OP_POP: pop(vm); break;
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm->globals, name, &value)) {
          runtimeError(vm, "Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm, value);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm->globals, name, peek(vm, 0));
        pop(vm);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if (tableSet(&vm->globals, name, peek(vm, 0))) {
          tableDelete(&vm->globals, name);
          runtimeError(vm, "Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_EQUAL: {
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm, BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: {
        if (IS_STRING(peek(vm, 0)) && IS_STRING(peek(vm, 1))) {
          concatenate(vm);
        } else if (IS_NUMBER(peek(vm, 0)) && IS_NUMBER(peek(vm, 1))) {
          double b = AS_NUMBER(pop(vm));
          double a = AS_NUMBER(pop(vm));
          push(vm, NUMBER_VAL(a + b));
        } else {
          runtimeError(
              vm, "Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT: push(vm, BOOL_VAL(isFalsey(pop(vm)))); break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek(vm, 0))) {
          runtimeError(vm, "Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
        break;
      case OP_PRINT: {
        printValue(vm->fout, pop(vm));
        fprintf(vm->fout, "\n");
        break;
      }
      case OP_RETURN: {
        // Exit interpreter.
        return INTERPRET_OK;
      }
      default: {
        fprintf(vm->ferr, "Unknown opcode %d\n", instruction);
        return INTERPRET_RUNTIME_ERROR;
      }
    }
  }

  fprintf(vm->ferr, "missing OP_RETURN\n");
  return INTERPRET_RUNTIME_ERROR;

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpretChunk(VM* vm, Chunk* chunk) {
  vm->chunk = chunk;
  vm->ip = vm->chunk->code;
  return run(vm);
}

InterpretResult interpret(VM* vm, const char* source) {
  Chunk chunk;
  initChunk(&chunk);

  Obj* objects = NULL;
  if (!compile(
          vm->fout, vm->ferr, source, &chunk, &objects, &vm->strings)) {
    freeChunk(&chunk);
    freeObjects(objects);
    return INTERPRET_COMPILE_ERROR;
  }

  if (objects != NULL) {
    // Prepend objects to vm->objects.
    Obj* lastObject = objects;
    while (lastObject->next != NULL) {
      lastObject = lastObject->next;
    }
    lastObject->next = vm->objects;
    vm->objects = objects;
  }

  InterpretResult result = interpretChunk(vm, &chunk);

  vm->chunk = NULL;
  vm->ip = NULL;
  freeChunk(&chunk);
  return result;
}
