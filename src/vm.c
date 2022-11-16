#include "vm.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"

bool debugTraceExecution = false;

static Value clockNative(int argCount, Value* args) {
  (void)argCount;
  (void)args;

  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack(VM* vm) {
  vm->stackTop = vm->stack;
  vm->frameCount = 0;
  vm->openUpvalues = NULL;
}

static void runtimeError(VM* vm, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(vm->ferr, format, args);
  va_end(args);
  fputs("\n", vm->ferr);

  for (int i = vm->frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm->frames[i];
    ObjFunction* function = frame->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(
        vm->ferr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(vm->ferr, "script\n");
    } else {
      fprintf(vm->ferr, "%s()\n", function->name->chars);
    }
  }

  resetStack(vm);
}

static void defineNative(VM* vm, const char* name, NativeFn function) {
  push(vm,
      OBJ_VAL(copyString(
          &vm->objects, &vm->strings, name, (int)strlen(name))));
  push(vm, OBJ_VAL(newNative(&vm->objects, function)));
  tableSet(&vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
  pop(vm);
  pop(vm);
}

void initVM(VM* vm, FILE* fout, FILE* ferr) {
  vm->fout = fout;
  vm->ferr = ferr;
  resetStack(vm);
  vm->objects = NULL;
  initTable(&vm->globals, 0.75);
  initTable(&vm->strings, 0.75);

  defineNative(vm, "clock", clockNative);
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

static bool call(
    VM* vm, ObjFunction* function, ObjClosure* closure, int argCount) {
  if (argCount != function->arity) {
    runtimeError(vm, "Expected %d arguments but got %d.",
        function->arity, argCount);
    return false;
  }

  // GCOV_EXCL_START
  if (vm->frameCount == FRAMES_MAX) {
    runtimeError(vm, "Stack overflow.");
    return false;
  }
  // GCOV_EXCL_STOP

  CallFrame* frame = &vm->frames[vm->frameCount++];
  frame->closure = closure;
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slots = vm->stackTop - argCount - 1;
  return true;
}

static bool callValue(VM* vm, Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_FUNCTION:
        return call(vm, AS_FUNCTION(callee), NULL, argCount);
      case OBJ_CLOSURE: {
        ObjClosure* c = AS_CLOSURE(callee);
        return call(vm, c->function, c, argCount);
      }
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(argCount, vm->stackTop - argCount);
        vm->stackTop -= argCount + 1;
        push(vm, result);
        return true;
      }
      default: break; // Non-callable object type.
    }
  }
  runtimeError(vm, "Can only call functions and classes.");
  return false;
}

static ObjUpvalue* captureUpvalue(VM* vm, Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm->openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(&vm->objects, local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm->openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(VM* vm, Value* last) {
  while (
      vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm->openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm->openUpvalues = upvalue->next;
  }
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
  CallFrame* frame = &vm->frames[vm->frameCount - 1];

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT() \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
  (frame->function->chunk.constants.values[READ_BYTE()])

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

  for (;;) {
    // GCOV_EXCL_START
    if (debugTraceExecution) {
      fprintf(vm->ferr, "          ");
      for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        fprintf(vm->ferr, "[ ");
        printValue(vm->ferr, *slot);
        fprintf(vm->ferr, " ]");
      }
      fprintf(vm->ferr, "\n");
      disassembleInstruction(vm->ferr, &frame->function->chunk,
          (int)(frame->ip - frame->function->chunk.code));
    }
    // GCOV_EXCL_STOP

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
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(vm, frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(vm, 0);
        break;
      }
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
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(vm, *frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(vm, 0);
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
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(vm, 0))) {
          frame->ip += offset;
        }
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        if (!callValue(vm, peek(vm, argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frameCount - 1];
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(&vm->objects, function);
        push(vm, OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            closure->upvalues[i] =
                captureUpvalue(vm, frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm, vm->stackTop - 1);
        pop(vm);
        break;
      case OP_RETURN: {
        Value result = pop(vm);
        closeUpvalues(vm, frame->slots);
        vm->frameCount--;
        if (vm->frameCount == 0) {
          pop(vm);
          return INTERPRET_OK;
        }

        vm->stackTop = frame->slots;
        push(vm, result);
        frame = &vm->frames[vm->frameCount - 1];
        break;
      }
      default: {
        fprintf(vm->ferr, "Unknown opcode %d\n", instruction);
        return INTERPRET_RUNTIME_ERROR;
      }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpretChunk(VM* vm, Chunk* chunk) {
  ObjFunction* function = newFunction(&vm->objects);
  freeChunk(&function->chunk);
  function->chunk = *chunk;

  push(vm, OBJ_VAL(function));
  call(vm, function, NULL, 0);

  return run(vm);
}

InterpretResult interpret(VM* vm, const char* source) {
  ObjFunction* function =
      compile(vm->fout, vm->ferr, source, &vm->objects, &vm->strings);
  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  push(vm, OBJ_VAL(function));
  call(vm, function, NULL, 0);

  return run(vm);
}
