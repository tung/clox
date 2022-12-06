#include "vm.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "obj_native.h"
#include "object.h"

#ifndef THREADED_CODE
#define THREADED_CODE 1
#endif

bool debugTraceExecution = false;

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

static bool checkArity(VM* vm, int expected, int actual) {
  if (expected != actual) {
    runtimeError(
        vm, "Expected %d arguments but got %d.", expected, actual);
  }
  return expected == actual;
}

static bool clockNative(VM* vm, int argCount, Value* args) {
  (void)args;

  if (!checkArity(vm, 0, argCount)) {
    return false;
  }

  push(vm, NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
  return true;
}

static void defineNative(VM* vm, const char* name, NativeFn function) {
  push(vm,
      OBJ_VAL(
          copyString(&vm->gc, &vm->strings, name, (int)strlen(name))));
  push(vm, OBJ_VAL(newNative(&vm->gc, function)));
  int slot = vm->globalSlots.count;
  assert(slot < UINT16_MAX); // GCOV_EXCL_LINE
  writeValueArray(&vm->gc, &vm->globalSlots, vm->stack[1]);
  tableSet(
      &vm->gc, &vm->globals, AS_STRING(vm->stack[0]), AS_NUMBER(slot));
  pop(vm);
  pop(vm);
}

static void vmMarkRoots(GC* gc, void* arg) {
  VM* vm = (VM*)arg;

  for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
    markValue(gc, *slot);
  }

  for (int i = 0; i < vm->frameCount; i++) {
    markObject(gc, (Obj*)vm->frames[i].closure);
    markObject(gc, (Obj*)vm->frames[i].function);
  }

  for (ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject(gc, (Obj*)upvalue);
  }

  markTable(gc, &vm->globals);
  for (int i = 0; i < vm->globalSlots.count; i++) {
    markValue(gc, vm->globalSlots.values[i]);
  }

  markObject(gc, (Obj*)vm->initString);
}

void initVM(VM* vm, FILE* fout, FILE* ferr) {
  vm->fout = fout;
  vm->ferr = ferr;
  resetStack(vm);
  initGC(&vm->gc);
  vm->gc.markRoots = vmMarkRoots;
  vm->gc.markRootsArg = vm;
  vm->gc.fixWeak = (void (*)(void*))tableRemoveWhite;
  vm->gc.fixWeakArg = &vm->strings;

  initTable(&vm->globals, 0.75);
  initValueArray(&vm->globalSlots);
  initTable(&vm->strings, 0.75);

  vm->initString = NULL;
  vm->initString = copyString(&vm->gc, &vm->strings, "init", 4);

  defineNative(vm, "clock", clockNative);
}

void freeVM(VM* vm) {
  freeTable(&vm->gc, &vm->globals);
  freeValueArray(&vm->gc, &vm->globalSlots);
  freeTable(&vm->gc, &vm->strings);
  vm->initString = NULL;
  freeGC(&vm->gc);
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

static bool call(VM* vm, Obj* callable, int argCount) {
  ObjClosure* closure;
  ObjFunction* function;

  if (callable->type == OBJ_CLOSURE) {
    closure = (ObjClosure*)callable;
    function = closure->function;
  } else {
    assert(callable->type == OBJ_FUNCTION); // GCOV_EXCL_LINE
    closure = NULL;
    function = (ObjFunction*)callable;
  }

  if (!checkArity(vm, function->arity, argCount)) {
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
      case OBJ_CLOSURE: return call(vm, AS_OBJ(callee), argCount);
      case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        vm->stackTop[-argCount - 1] = bound->receiver;
        return call(vm, bound->method, argCount);
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        vm->stackTop[-argCount - 1] =
            OBJ_VAL(newInstance(&vm->gc, klass));
        Value initializer;
        if (tableGet(&klass->methods, vm->initString, &initializer)) {
          return call(vm, AS_OBJ(initializer), argCount);
        } else if (!checkArity(vm, 0, argCount)) {
          return false;
        }
        return true;
      }
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        if (!native(vm, argCount, vm->stackTop - argCount)) {
          return false;
        }
        Value result = pop(vm);
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

static bool invokeFromClass(
    VM* vm, ObjClass* klass, ObjString* name, int argCount) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError(vm, "Undefined property '%s'.", name->chars);
    return false;
  }
  return call(vm, AS_OBJ(method), argCount);
}

static bool invoke(VM* vm, ObjString* name, int argCount) {
  Value receiver = peek(vm, argCount);

  if (!IS_INSTANCE(receiver)) {
    runtimeError(vm, "Only instances have methods.");
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(receiver);

  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    vm->stackTop[-argCount - 1] = value;
    return callValue(vm, value, argCount);
  }

  return invokeFromClass(vm, instance->klass, name, argCount);
}

static bool bindMethod(VM* vm, ObjClass* klass, ObjString* name) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError(vm, "Undefined property '%s'.", name->chars);
    return false;
  }

  ObjBoundMethod* bound =
      newBoundMethod(&vm->gc, peek(vm, 0), AS_OBJ(method));
  pop(vm);
  push(vm, OBJ_VAL(bound));
  return true;
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

  ObjUpvalue* createdUpvalue = newUpvalue(&vm->gc, local);
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

static void defineMethod(VM* vm, ObjString* name) {
  Value method = peek(vm, 0);
  ObjClass* klass = AS_CLASS(peek(vm, 1));
  tableSet(&vm->gc, &klass->methods, name, method);
  pop(vm);
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(
    VM* vm, Value aValue, Value bValue, bool popTwice) {
  ObjString* b = AS_STRING(bValue);
  ObjString* a = AS_STRING(aValue);
  ObjString* result = concatStrings(&vm->gc, &vm->strings, a->chars,
      a->length, a->hash, b->chars, b->length);
  pop(vm);
  if (popTwice) {
    pop(vm);
  }
  push(vm, OBJ_VAL(result));
}

// GCOV_EXCL_START
static void trace(VM* vm, CallFrame* frame) {
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
#define BINARY_OP_C(valueType, op) \
  do { \
    Value bValue = READ_CONSTANT(); \
    if (!IS_NUMBER(bValue) || !IS_NUMBER(peek(vm, 0))) { \
      runtimeError(vm, "Operands must be numbers."); \
      return INTERPRET_RUNTIME_ERROR; \
    } \
    double b = AS_NUMBER(bValue); \
    double a = AS_NUMBER(pop(vm)); \
    push(vm, valueType(a op b)); \
  } while (false)

#if THREADED_CODE == 1

#define JUMP_ENTRY(op) [op] = &&CASE_##op
  static void* jumps[MAX_OPCODES] = {
    JUMP_ENTRY(OP_CONSTANT),
    JUMP_ENTRY(OP_NIL),
    JUMP_ENTRY(OP_TRUE),
    JUMP_ENTRY(OP_FALSE),
    JUMP_ENTRY(OP_POP),
    JUMP_ENTRY(OP_GET_LOCAL),
    JUMP_ENTRY(OP_SET_LOCAL),
    JUMP_ENTRY(OP_GET_GLOBAL),
    JUMP_ENTRY(OP_GET_GLOBAL_I),
    JUMP_ENTRY(OP_DEFINE_GLOBAL),
    JUMP_ENTRY(OP_SET_GLOBAL),
    JUMP_ENTRY(OP_SET_GLOBAL_I),
    JUMP_ENTRY(OP_GET_UPVALUE),
    JUMP_ENTRY(OP_SET_UPVALUE),
    JUMP_ENTRY(OP_GET_PROPERTY),
    JUMP_ENTRY(OP_SET_PROPERTY),
    JUMP_ENTRY(OP_GET_SUPER),
    JUMP_ENTRY(OP_EQUAL),
    JUMP_ENTRY(OP_GREATER),
    JUMP_ENTRY(OP_LESS),
    JUMP_ENTRY(OP_LESS_C),
    JUMP_ENTRY(OP_ADD),
    JUMP_ENTRY(OP_ADD_C),
    JUMP_ENTRY(OP_SUBTRACT),
    JUMP_ENTRY(OP_SUBTRACT_C),
    JUMP_ENTRY(OP_MULTIPLY),
    JUMP_ENTRY(OP_DIVIDE),
    JUMP_ENTRY(OP_MODULO),
    JUMP_ENTRY(OP_NOT),
    JUMP_ENTRY(OP_NEGATE),
    JUMP_ENTRY(OP_PRINT),
    JUMP_ENTRY(OP_JUMP),
    JUMP_ENTRY(OP_JUMP_IF_FALSE),
    JUMP_ENTRY(OP_PJMP_IF_FALSE),
    JUMP_ENTRY(OP_LOOP),
    JUMP_ENTRY(OP_CALL),
    JUMP_ENTRY(OP_INVOKE),
    JUMP_ENTRY(OP_SUPER_INVOKE),
    JUMP_ENTRY(OP_CLOSURE),
    JUMP_ENTRY(OP_CLOSE_UPVALUE),
    JUMP_ENTRY(OP_RETURN),
    JUMP_ENTRY(OP_CLASS),
    JUMP_ENTRY(OP_INHERIT),
    JUMP_ENTRY(OP_METHOD),
  };
#undef JUMP_ENTRY
  for (size_t i = 0; i < MAX_OPCODES; ++i) {
    assert(jumps[i] != NULL); // GCOV_EXCL_LINE
  }
#define FOR(c)
#define SWITCH(c) NEXT;
#define CASE(c) CASE_##c:
#define DEFAULT CASE_##DEFAULT:
#define NEXT \
  do { \
    if (debugTraceExecution) \
      trace(vm, frame); \
    uint8_t op = READ_BYTE(); \
    if (op >= MAX_OPCODES) \
      goto CASE_DEFAULT; \
    goto* jumps[op]; \
  } while (0)

#else

#define FOR(c) for (c)
#define SWITCH(c) switch (c)
#define CASE(c) case c:
#define DEFAULT default:
#define NEXT break

#endif

  FOR(;;) {
#if THREADED_CODE != 1
    // GCOV_EXCL_START
    if (debugTraceExecution) {
      trace(vm, frame);
    }
    // GCOV_EXCL_STOP
#endif
    SWITCH(READ_BYTE()) {
      CASE(OP_CONSTANT) {
        Value constant = READ_CONSTANT();
        push(vm, constant);
        NEXT;
      }
      CASE(OP_NIL) {
        push(vm, NIL_VAL);
        NEXT;
      }
      CASE(OP_TRUE) {
        push(vm, BOOL_VAL(true));
        NEXT;
      }
      CASE(OP_FALSE) {
        push(vm, BOOL_VAL(false));
        NEXT;
      }
      CASE(OP_POP) {
        pop(vm);
        NEXT;
      }
      CASE(OP_GET_LOCAL) {
        uint8_t slot = READ_BYTE();
        push(vm, frame->slots[slot]);
        NEXT;
      }
      CASE(OP_SET_LOCAL) {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(vm, 0);
        NEXT;
      }
      CASE(OP_GET_GLOBAL) {
        ObjString* name = READ_STRING();
        Value slot;
        if (!tableGet(&vm->globals, name, &slot)) {
          runtimeError(vm, "Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        uint16_t slotInt = (uint16_t)AS_NUMBER(slot);
        frame->ip[-2] = OP_GET_GLOBAL_I;
        frame->ip[-1] = (uint8_t)(slotInt >> 8);
        frame->ip[0] = (uint8_t)(slotInt & 0xff);
        frame->ip -= 2;
        NEXT;
      }
      CASE(OP_GET_GLOBAL_I) {
        push(vm, vm->globalSlots.values[READ_SHORT()]);
        NEXT;
      }
      CASE(OP_DEFINE_GLOBAL) {
        ObjString* name = READ_STRING();
        Value slot;
        if (!tableGet(&vm->globals, name, &slot)) {
          int newSlot = vm->globalSlots.count;
          // GCOV_EXCL_START
          if (newSlot > UINT16_MAX) {
            runtimeError(
                vm, "Can't have more than %u globals.", UINT16_MAX + 1);
            return INTERPRET_RUNTIME_ERROR;
          }
          // GCOV_EXCL_STOP
          writeValueArray(&vm->gc, &vm->globalSlots, peek(vm, 0));
          pop(vm);
          slot = NUMBER_VAL((double)newSlot);
          tableSet(&vm->gc, &vm->globals, name, slot);
        } else {
          vm->globalSlots.values[(int)AS_NUMBER(slot)] = peek(vm, 0);
        }
        NEXT;
      }
      CASE(OP_SET_GLOBAL) {
        ObjString* name = READ_STRING();
        Value slot;
        if (!tableGet(&vm->globals, name, &slot)) {
          runtimeError(vm, "Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        uint16_t slotInt = (uint16_t)AS_NUMBER(slot);
        frame->ip[-2] = OP_SET_GLOBAL_I;
        frame->ip[-1] = (uint8_t)(slotInt >> 8);
        frame->ip[0] = (uint8_t)(slotInt & 0xff);
        frame->ip -= 2;
        NEXT;
      }
      CASE(OP_SET_GLOBAL_I) {
        vm->globalSlots.values[READ_SHORT()] = peek(vm, 0);
        NEXT;
      }
      CASE(OP_GET_UPVALUE) {
        uint8_t slot = READ_BYTE();
        push(vm, *frame->closure->upvalues[slot]->location);
        NEXT;
      }
      CASE(OP_SET_UPVALUE) {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(vm, 0);
        NEXT;
      }
      CASE(OP_GET_PROPERTY) {
        if (!IS_INSTANCE(peek(vm, 0))) {
          runtimeError(vm, "Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(peek(vm, 0));
        ObjString* name = READ_STRING();

        Value value;
        if (tableGet(&instance->fields, name, &value)) {
          pop(vm); // Instance.
          push(vm, value);
          NEXT;
        }

        if (!bindMethod(vm, instance->klass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        NEXT;
      }
      CASE(OP_SET_PROPERTY) {
        if (!IS_INSTANCE(peek(vm, 1))) {
          runtimeError(vm, "Only instances have fields.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(peek(vm, 1));
        tableSet(
            &vm->gc, &instance->fields, READ_STRING(), peek(vm, 0));
        Value value = pop(vm);
        pop(vm);
        push(vm, value);
        NEXT;
      }
      CASE(OP_GET_SUPER) {
        ObjString* name = READ_STRING();
        ObjClass* superclass = AS_CLASS(pop(vm));

        if (!bindMethod(vm, superclass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        NEXT;
      }
      CASE(OP_EQUAL) {
        Value b = pop(vm);
        Value a = pop(vm);
        push(vm, BOOL_VAL(valuesEqual(a, b)));
        NEXT;
      }
      CASE(OP_GREATER) {
        BINARY_OP(BOOL_VAL, >);
        NEXT;
      }
      CASE(OP_LESS) {
        BINARY_OP(BOOL_VAL, <);
        NEXT;
      }
      CASE(OP_LESS_C) {
        BINARY_OP_C(BOOL_VAL, <);
        NEXT;
      }
      CASE(OP_ADD) {
        Value bValue = peek(vm, 0);
        Value aValue = peek(vm, 1);
        if (IS_STRING(bValue) && IS_STRING(aValue)) {
          concatenate(vm, aValue, bValue, true);
        } else if (IS_NUMBER(bValue) && IS_NUMBER(aValue)) {
          double b = AS_NUMBER(bValue);
          double a = AS_NUMBER(aValue);
          pop(vm);
          pop(vm);
          push(vm, NUMBER_VAL(a + b));
        } else {
          runtimeError(
              vm, "Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        NEXT;
      }
      CASE(OP_ADD_C) {
        Value bValue = READ_CONSTANT();
        Value aValue = peek(vm, 0);
        if (IS_STRING(bValue) && IS_STRING(aValue)) {
          concatenate(vm, aValue, bValue, false);
        } else if (IS_NUMBER(bValue) && IS_NUMBER(aValue)) {
          double b = AS_NUMBER(bValue);
          double a = AS_NUMBER(aValue);
          pop(vm);
          push(vm, NUMBER_VAL(a + b));
        } else {
          runtimeError(
              vm, "Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        NEXT;
      }
      CASE(OP_SUBTRACT) {
        BINARY_OP(NUMBER_VAL, -);
        NEXT;
      }
      CASE(OP_SUBTRACT_C) {
        BINARY_OP_C(NUMBER_VAL, -);
        NEXT;
      }
      CASE(OP_MULTIPLY) {
        BINARY_OP(NUMBER_VAL, *);
        NEXT;
      }
      CASE(OP_DIVIDE) {
        BINARY_OP(NUMBER_VAL, /);
        NEXT;
      }
      CASE(OP_MODULO) {
        if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {
          runtimeError(vm, "Operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }
        double b = AS_NUMBER(pop(vm));
        double a = AS_NUMBER(pop(vm));
        push(vm, NUMBER_VAL(fmod(a, b)));
        NEXT;
      }
      CASE(OP_NOT) {
        push(vm, BOOL_VAL(isFalsey(pop(vm))));
        NEXT;
      }
      CASE(OP_NEGATE) {
        if (!IS_NUMBER(peek(vm, 0))) {
          runtimeError(vm, "Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
        NEXT;
      }
      CASE(OP_PRINT) {
        printValue(vm->fout, pop(vm));
        fprintf(vm->fout, "\n");
        NEXT;
      }
      CASE(OP_JUMP) {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        NEXT;
      }
      CASE(OP_JUMP_IF_FALSE) {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(vm, 0))) {
          frame->ip += offset;
        }
        NEXT;
      }
      CASE(OP_PJMP_IF_FALSE) {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(vm, 0))) {
          frame->ip += offset;
        }
        pop(vm);
        NEXT;
      }
      CASE(OP_LOOP) {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        NEXT;
      }
      CASE(OP_CALL) {
        int argCount = READ_BYTE();
        if (!callValue(vm, peek(vm, argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frameCount - 1];
        NEXT;
      }
      CASE(OP_INVOKE) {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        if (!invoke(vm, method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frameCount - 1];
        NEXT;
      }
      CASE(OP_SUPER_INVOKE) {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        ObjClass* superclass = AS_CLASS(pop(vm));
        if (!invokeFromClass(vm, superclass, method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frameCount - 1];
        NEXT;
      }
      CASE(OP_CLOSURE) {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(&vm->gc, function);
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
        NEXT;
      }
      CASE(OP_CLOSE_UPVALUE) {
        closeUpvalues(vm, vm->stackTop - 1);
        pop(vm);
        NEXT;
      }
      CASE(OP_RETURN) {
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
        NEXT;
      }
      CASE(OP_CLASS) {
        push(vm, OBJ_VAL(newClass(&vm->gc, READ_STRING())));
        NEXT;
      }
      CASE(OP_INHERIT) {
        Value superclass = peek(vm, 1);
        if (!IS_CLASS(superclass)) {
          runtimeError(vm, "Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjClass* subclass = AS_CLASS(peek(vm, 0));
        tableAddAll(&vm->gc, &AS_CLASS(superclass)->methods,
            &subclass->methods);
        pop(vm); // Subclass.
        NEXT;
      }
      CASE(OP_METHOD) {
        defineMethod(vm, READ_STRING());
        NEXT;
      }
      DEFAULT {
        fprintf(vm->ferr, "Unknown opcode %d\n", frame->ip[-1]);
        return INTERPRET_RUNTIME_ERROR;
      }
    }
  }

#undef FOR
#undef SWITCH
#undef CASE
#undef DEFAULT
#undef NEXT

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpretCall(VM* vm, Obj* callable, int argCount) {
  call(vm, callable, argCount);
  return run(vm);
}

InterpretResult interpret(VM* vm, const char* source) {
  ObjFunction* function =
      compile(vm->fout, vm->ferr, source, &vm->gc, &vm->strings);
  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  push(vm, OBJ_VAL(function));
  call(vm, (Obj*)function, 0);

  return run(vm);
}
