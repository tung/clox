#include "vm.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "membuf.h"
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

static bool checkIndexBounds(
    VM* vm, const char* type, int bounds, Value indexValue) {
  if (!IS_NUMBER(indexValue)) {
    runtimeError(vm, "%s must be a number.", type);
    return false;
  }

  double indexNum = AS_NUMBER(indexValue);

  if (indexNum < 0 || indexNum >= (double)bounds) {
    runtimeError(
        vm, "%s (%g) out of bounds (%d).", type, indexNum, bounds);
    return false;
  }

  if ((double)(int)indexNum != indexNum) {
    runtimeError(vm, "%s (%g) must be a whole number.", type, indexNum);
    return false;
  }

  return true;
}

static bool checkStringIndex(
    VM* vm, ObjString* string, Value indexValue) {
  return checkIndexBounds(
      vm, "String index", string->length, indexValue);
}

static bool argcNative(VM* vm, int argCount, Value* args) {
  (void)args;
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  push(vm, NUMBER_VAL((double)vm->args.count));
  return true;
}

static bool argvNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!checkIndexBounds(vm, "Argument", vm->args.count, args[0])) {
    return false;
  }
  int pos = (int)AS_NUMBER(args[0]);
  push(vm, vm->args.values[pos]);
  return true;
}

static bool ceilNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!IS_NUMBER(args[0])) {
    runtimeError(vm, "Argument must be a number.");
    return false;
  }
  push(vm, NUMBER_VAL(ceil(AS_NUMBER(args[0]))));
  return true;
}

static bool chrNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!IS_NUMBER(args[0])) {
    runtimeError(vm, "Argument must be a number.");
    return false;
  }
  double num = AS_NUMBER(args[0]);
  if (num < (double)CHAR_MIN || num > (double)CHAR_MAX) {
    runtimeError(vm, "Argument (%g) must be between %d and %d.", num,
        CHAR_MIN, CHAR_MAX);
    return false;
  }
  if ((double)(char)num != num) {
    runtimeError(vm, "Argument (%g) must be a whole number.", num);
    return false;
  }
  char buf[2];
  buf[0] = (char)num;
  buf[1] = '\0';
  push(vm, OBJ_VAL(copyString(&vm->gc, &vm->strings, buf, 1)));
  return true;
}

static bool clockNative(VM* vm, int argCount, Value* args) {
  (void)args;
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  push(vm, NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
  return true;
}

static bool eprintNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  printValue(vm->ferr, args[0]);
  fputc('\n', vm->ferr);
  push(vm, NIL_VAL);
  return true;
}

static bool exitNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!IS_NUMBER(args[0])) {
    runtimeError(vm, "Argument must be a number.");
    return false;
  }
  double num = AS_NUMBER(args[0]);
  if (num < 0.0 || num > (double)UCHAR_MAX) {
    runtimeError(
        vm, "Argument (%g) must be between 0 and %d.", num, UCHAR_MAX);
    return false;
  }
  if ((double)(int)num != num) {
    runtimeError(vm, "Argument (%g) must be a whole number.", num);
    return false;
  }
  exit((int)num);
  return true;
}

static bool floorNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!IS_NUMBER(args[0])) {
    runtimeError(vm, "Argument must be a number.");
    return false;
  }
  push(vm, NUMBER_VAL(floor(AS_NUMBER(args[0]))));
  return true;
}

static bool roundNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!IS_NUMBER(args[0])) {
    runtimeError(vm, "Argument must be a number.");
    return false;
  }
  push(vm, NUMBER_VAL(round(AS_NUMBER(args[0]))));
  return true;
}

static bool strNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  MemBuf out;
  initMemBuf(&out);
  printValue(out.fptr, args[0]);
  fflush(out.fptr);
  push(vm,
      OBJ_VAL(copyString(&vm->gc, &vm->strings, out.buf, out.size)));
  freeMemBuf(&out);
  return true;
}

static bool typeNative(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  const char* t = "unknown";
  if (IS_OBJ(args[0])) {
    switch (OBJ_TYPE(args[0])) {
      case OBJ_BOUND_METHOD:
      case OBJ_CLOSURE:
      case OBJ_FUNCTION: t = "function"; break;
      case OBJ_CLASS: t = "class"; break;
      case OBJ_INSTANCE: t = "instance"; break;
      case OBJ_LIST: t = "list"; break;
      case OBJ_MAP: t = "map"; break;
      case OBJ_NATIVE: t = "native function"; break;
      case OBJ_STRING: t = "string"; break;
      case OBJ_UPVALUE: t = "upvalue"; break;
    }
  } else if (IS_BOOL(args[0])) {
    t = "boolean";
  } else if (IS_NIL(args[0])) {
    t = "nil";
  } else if (IS_NUMBER(args[0])) {
    t = "number";
  }
  push(vm, OBJ_VAL(copyString(&vm->gc, &vm->strings, t, strlen(t))));
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
      &vm->gc, &vm->globals, AS_STRING(vm->stack[0]), NUMBER_VAL(slot));
  pop(vm);
  pop(vm);
}

static void vmMarkRoots(GC* gc, void* arg) {
  VM* vm = (VM*)arg;

  for (int i = 0; i < vm->args.count; i++) {
    markValue(gc, vm->args.values[i]);
  }

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
  markObject(gc, (Obj*)vm->listClass);
  markObject(gc, (Obj*)vm->mapClass);
  markObject(gc, (Obj*)vm->stringClass);
}

static void defineNativeMethod(
    VM* vm, ObjClass* klass, const char* name, NativeFn fn) {
  ObjString* str =
      copyString(&vm->gc, &vm->strings, name, (int)strlen(name));
  pushTemp(&vm->gc, OBJ_VAL(str));

  ObjNative* native = newNative(&vm->gc, fn);
  pushTemp(&vm->gc, OBJ_VAL(native));

  tableSet(&vm->gc, &klass->methods, str, OBJ_VAL(native));

  popTemp(&vm->gc); // str
  popTemp(&vm->gc); // native
}

static bool checkListIndex(VM* vm, Value listValue, Value indexValue) {
  ObjList* list = AS_LIST(listValue);
  return checkIndexBounds(
      vm, "List index", list->elements.count, indexValue);
}

static bool listInsert(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 2, argCount)) {
    return false;
  }
  if (!checkListIndex(vm, args[-1], args[0])) {
    return false;
  }
  ObjList* list = AS_LIST(args[-1]);
  int pos = (int)AS_NUMBER(args[0]);
  insertValueArray(&vm->gc, &list->elements, pos, args[1]);
  push(vm, NIL_VAL);
  return true;
}

static bool listPop(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  ObjList* list = AS_LIST(args[-1]);
  if (list->elements.count == 0) {
    runtimeError(vm, "Can't pop from an empty list.");
    return false;
  }
  push(vm, removeValueArray(&list->elements, list->elements.count - 1));
  return true;
}

static bool listPush(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  ObjList* list = AS_LIST(args[-1]);
  writeValueArray(&vm->gc, &list->elements, args[0]);
  push(vm, NIL_VAL);
  return true;
}

static bool listRemove(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!checkListIndex(vm, args[-1], args[0])) {
    return false;
  }
  ObjList* list = AS_LIST(args[-1]);
  int pos = (int)AS_NUMBER(args[0]);
  push(vm, removeValueArray(&list->elements, pos));
  return true;
}

static bool listSize(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  ObjList* list = AS_LIST(args[-1]);
  push(vm, NUMBER_VAL((double)list->elements.count));
  return true;
}

static void initListClass(VM* vm) {
  const char listStr[] = "(List)";
  ObjString* listClassName =
      copyString(&vm->gc, &vm->strings, listStr, sizeof(listStr) - 1);
  pushTemp(&vm->gc, OBJ_VAL(listClassName));
  vm->listClass = newClass(&vm->gc, listClassName);
  popTemp(&vm->gc);

  defineNativeMethod(vm, vm->listClass, "insert", listInsert);
  defineNativeMethod(vm, vm->listClass, "push", listPush);
  defineNativeMethod(vm, vm->listClass, "pop", listPop);
  defineNativeMethod(vm, vm->listClass, "size", listSize);
  defineNativeMethod(vm, vm->listClass, "remove", listRemove);
}

static bool mapCount(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  ObjMap* map = AS_MAP(args[-1]);
  int count = 0;
  for (int i = 0; i < map->table.capacity; ++i) {
    count += !!(map->table.entries[i].key);
  }
  push(vm, NUMBER_VAL((double)count));
  return true;
}

static bool mapHas(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!IS_STRING(args[0])) {
    runtimeError(vm, "Maps can only be indexed by string.");
    return false;
  }
  ObjMap* map = AS_MAP(args[-1]);
  ObjString* key = AS_STRING(args[0]);
  Value value;
  push(vm, BOOL_VAL(tableGet(&map->table, key, &value)));
  return true;
}

static bool mapKeys(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  ObjMap* map = AS_MAP(args[-1]);
  ObjList* keys = newList(&vm->gc);
  push(vm, OBJ_VAL(keys));
  for (int i = 0; i < map->table.capacity; ++i) {
    Entry* entry = &map->table.entries[i];
    if (entry->key == NULL) {
      continue;
    }
    writeValueArray(&vm->gc, &keys->elements, OBJ_VAL(entry->key));
  }
  return true;
}

static bool mapRemove(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 1, argCount)) {
    return false;
  }
  if (!IS_STRING(args[0])) {
    runtimeError(vm, "Maps can only be indexed by string.");
    return false;
  }
  ObjMap* map = AS_MAP(args[-1]);
  ObjString* key = AS_STRING(args[0]);
  push(vm, BOOL_VAL(tableDelete(&map->table, key)));
  return true;
}

static void initMapClass(VM* vm) {
  const char mapStr[] = "(Map)";
  ObjString* mapClassName =
      copyString(&vm->gc, &vm->strings, mapStr, sizeof(mapStr) - 1);
  pushTemp(&vm->gc, OBJ_VAL(mapClassName));
  vm->mapClass = newClass(&vm->gc, mapClassName);
  popTemp(&vm->gc);

  defineNativeMethod(vm, vm->mapClass, "count", mapCount);
  defineNativeMethod(vm, vm->mapClass, "has", mapHas);
  defineNativeMethod(vm, vm->mapClass, "keys", mapKeys);
  defineNativeMethod(vm, vm->mapClass, "remove", mapRemove);
}

static bool stringParseNum(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  ObjString* string = AS_STRING(args[-1]);
  char* after;
  double result = strtod(string->chars, &after);
  while (after < string->chars + string->length) {
    if (!isspace(*after)) {
      break;
    }
    ++after;
  }
  if (after == string->chars + string->length) {
    push(vm, NUMBER_VAL(result));
  } else {
    push(vm, NIL_VAL);
  }
  return true;
}

static bool stringSize(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 0, argCount)) {
    return false;
  }
  ObjString* string = AS_STRING(args[-1]);
  push(vm, NUMBER_VAL((double)string->length));
  return true;
}

static bool substrIndex(
    VM* vm, Value input, const char* type, int length, int* index) {
  if (!IS_NUMBER(input)) {
    runtimeError(vm, "%s must be a number.", type);
    return false;
  }
  double num = AS_NUMBER(input);
  if (num <= (double)INT_MIN) {
    *index = INT_MIN;
  } else if (num >= (double)INT_MAX) {
    *index = INT_MAX;
  } else {
    if ((double)(int)num != num) {
      runtimeError(vm, "%s (%g) must be a whole number.", type, num);
      return false;
    }
    *index = num;
  }
  if (*index < 0) {
    *index += length + 1;
  }
  if (*index < 0) {
    *index = 0;
  } else if (*index > length) {
    *index = length;
  }
  return true;
}

static bool stringSubstr(VM* vm, int argCount, Value* args) {
  if (!checkArity(vm, 2, argCount)) {
    return false;
  }
  ObjString* string = AS_STRING(args[-1]);
  int start;
  int end;
  if (!substrIndex(vm, args[0], "Start", string->length, &start)) {
    return false;
  }
  if (!substrIndex(vm, args[1], "End", string->length, &end)) {
    return false;
  }
  const char* chars = "";
  int length = 0;
  if (start < end) {
    chars = string->chars + start;
    length = end - start;
  }
  push(vm, OBJ_VAL(copyString(&vm->gc, &vm->strings, chars, length)));
  return true;
}

static void initStringClass(VM* vm) {
  const char stringStr[] = "(String)";
  ObjString* stringClassName = copyString(
      &vm->gc, &vm->strings, stringStr, sizeof(stringStr) - 1);
  pushTemp(&vm->gc, OBJ_VAL(stringClassName));
  vm->stringClass = newClass(&vm->gc, stringClassName);
  popTemp(&vm->gc);

  defineNativeMethod(vm, vm->stringClass, "parsenum", stringParseNum);
  defineNativeMethod(vm, vm->stringClass, "size", stringSize);
  defineNativeMethod(vm, vm->stringClass, "substr", stringSubstr);
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

  initValueArray(&vm->args);
  initTable(&vm->globals, 0.75);
  initValueArray(&vm->globalSlots);
  initTable(&vm->strings, 0.75);

  vm->initString = NULL;
  vm->listClass = NULL;
  vm->mapClass = NULL;
  vm->stringClass = NULL;

  vm->initString = copyString(&vm->gc, &vm->strings, "init", 4);
  initListClass(vm);
  initMapClass(vm);
  initStringClass(vm);

  defineNative(vm, "argc", argcNative);
  defineNative(vm, "argv", argvNative);
  defineNative(vm, "ceil", ceilNative);
  defineNative(vm, "chr", chrNative);
  defineNative(vm, "clock", clockNative);
  defineNative(vm, "eprint", eprintNative);
  defineNative(vm, "exit", exitNative);
  defineNative(vm, "floor", floorNative);
  defineNative(vm, "round", roundNative);
  defineNative(vm, "str", strNative);
  defineNative(vm, "type", typeNative);
}

void freeVM(VM* vm) {
  freeValueArray(&vm->gc, &vm->args);
  freeTable(&vm->gc, &vm->globals);
  freeValueArray(&vm->gc, &vm->globalSlots);
  freeTable(&vm->gc, &vm->strings);
  vm->initString = NULL;
  freeGC(&vm->gc);
}

void argsVM(VM* vm, int argc, const char* argv[]) {
  for (int i = 0; i < argc; ++i) {
    const char* ptr = argv[i] ? argv[i] : "";
    ObjString* s = copyString(&vm->gc, &vm->strings, ptr, strlen(ptr));
    Value arg = OBJ_VAL(s);
    pushTemp(&vm->gc, arg);
    writeValueArray(&vm->gc, &vm->args, arg);
    popTemp(&vm->gc);
  }
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
        return callValue(vm, OBJ_VAL(bound->method), argCount);
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        vm->stackTop[-argCount - 1] =
            OBJ_VAL(newInstance(&vm->gc, klass));
        Value initializer;
        if (tableGet(&klass->methods, vm->initString, &initializer)) {
          return callValue(vm, initializer, argCount);
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
  return callValue(vm, method, argCount);
}

static bool invoke(VM* vm, ObjString* name, int argCount) {
  Value receiver = peek(vm, argCount);
  ObjClass* klass;

  if (IS_LIST(receiver)) {
    klass = vm->listClass;
  } else if (IS_MAP(receiver)) {
    klass = vm->mapClass;
  } else if (IS_STRING(receiver)) {
    klass = vm->stringClass;
  } else if (IS_INSTANCE(receiver)) {
    ObjInstance* instance = AS_INSTANCE(receiver);
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
      vm->stackTop[-argCount - 1] = value;
      return callValue(vm, value, argCount);
    }
    klass = instance->klass;
  } else {
    runtimeError(
        vm, "Only lists, maps, strings and instances have methods.");
    return false;
  }

  return invokeFromClass(vm, klass, name, argCount);
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
    JUMP_ENTRY(OP_GET_INDEX),
    JUMP_ENTRY(OP_SET_INDEX),
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
    JUMP_ENTRY(OP_LIST_INIT),
    JUMP_ENTRY(OP_LIST_DATA),
    JUMP_ENTRY(OP_MAP_INIT),
    JUMP_ENTRY(OP_MAP_DATA),
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
        ObjString* name = READ_STRING();
        Value receiver = peek(vm, 0);
        ObjClass* klass;

        if (IS_LIST(receiver)) {
          klass = vm->listClass;
        } else if (IS_MAP(receiver)) {
          klass = vm->mapClass;
        } else if (IS_STRING(receiver)) {
          klass = vm->stringClass;
        } else if (IS_INSTANCE(receiver)) {
          ObjInstance* instance = AS_INSTANCE(peek(vm, 0));
          Value value;
          if (tableGet(&instance->fields, name, &value)) {
            pop(vm); // Instance.
            push(vm, value);
            NEXT;
          }
          klass = instance->klass;
        } else {
          runtimeError(vm, "Only lists and instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        if (!bindMethod(vm, klass, name)) {
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
      CASE(OP_GET_INDEX) {
        if (IS_LIST(peek(vm, 1))) {
          if (!checkListIndex(vm, peek(vm, 1), peek(vm, 0))) {
            return INTERPRET_RUNTIME_ERROR;
          }
          int index = (int)AS_NUMBER(pop(vm));
          ObjList* list = AS_LIST(pop(vm));
          push(vm, list->elements.values[index]);
          NEXT;
        } else if (IS_MAP(peek(vm, 1))) {
          if (!IS_STRING(peek(vm, 0))) {
            runtimeError(vm, "Maps can only be indexed by string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          ObjString* key = AS_STRING(peek(vm, 0));
          ObjMap* map = AS_MAP(peek(vm, 1));
          Value value;
          if (tableGet(&map->table, key, &value)) {
            pop(vm); // Key.
            pop(vm); // Map.
            push(vm, value);
            NEXT;
          }
          runtimeError(vm, "Undefined key '%s'.", key->chars);
        } else if (IS_STRING(peek(vm, 1))) {
          ObjString* string = AS_STRING(peek(vm, 1));
          if (!checkStringIndex(vm, string, peek(vm, 0))) {
            return INTERPRET_RUNTIME_ERROR;
          }
          int index = (int)AS_NUMBER(pop(vm));
          char c = string->chars[index];
          pop(vm); // String.
          push(vm, NUMBER_VAL((double)c));
          NEXT;
        } else if (IS_INSTANCE(peek(vm, 1))) {
          if (!IS_STRING(peek(vm, 0))) {
            runtimeError(
                vm, "Instances can only be indexed by string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          ObjString* name = AS_STRING(peek(vm, 0));
          ObjInstance* instance = AS_INSTANCE(peek(vm, 1));
          Value value;
          if (tableGet(&instance->fields, name, &value)) {
            pop(vm); // Name.
            pop(vm); // Instance.
            push(vm, value);
            NEXT;
          }
          runtimeError(vm, "Undefined property '%s'.", name->chars);
        } else {
          runtimeError(
              vm, "Can only index lists, maps, strings and instances.");
        }
        return INTERPRET_RUNTIME_ERROR;
      }
      CASE(OP_SET_INDEX) {
        if (IS_LIST(peek(vm, 2))) {
          if (!checkListIndex(vm, peek(vm, 2), peek(vm, 1))) {
            return INTERPRET_RUNTIME_ERROR;
          }
          Value value = pop(vm);
          int index = (int)AS_NUMBER(pop(vm));
          ObjList* list = AS_LIST(pop(vm));
          list->elements.values[index] = value;
          push(vm, value);
          NEXT;
        } else if (IS_MAP(peek(vm, 2))) {
          if (!IS_STRING(peek(vm, 1))) {
            runtimeError(vm, "Maps can only be indexed by string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          ObjString* key = AS_STRING(peek(vm, 1));
          ObjMap* map = AS_MAP(peek(vm, 2));
          tableSet(&vm->gc, &map->table, key, peek(vm, 0));
          Value value = pop(vm);
          pop(vm); // Key.
          pop(vm); // Map.
          push(vm, value);
          NEXT;
        } else if (IS_INSTANCE(peek(vm, 2))) {
          if (!IS_STRING(peek(vm, 1))) {
            runtimeError(
                vm, "Instances can only be indexed by string.");
            return INTERPRET_RUNTIME_ERROR;
          }
          ObjString* name = AS_STRING(peek(vm, 1));
          ObjInstance* instance = AS_INSTANCE(peek(vm, 2));
          tableSet(&vm->gc, &instance->fields, name, peek(vm, 0));
          Value value = pop(vm);
          pop(vm); // Name.
          pop(vm); // Instance.
          push(vm, value);
          NEXT;
        } else {
          runtimeError(
              vm, "Can only set index of lists, maps and instances.");
        }
        return INTERPRET_RUNTIME_ERROR;
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
      CASE(OP_LIST_INIT) {
        push(vm, OBJ_VAL(newList(&vm->gc)));
        NEXT;
      }
      CASE(OP_LIST_DATA) {
        if (!IS_LIST(peek(vm, 1))) {
          runtimeError(vm, "List data can only be added to a list.");
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjList* list = AS_LIST(peek(vm, 1));
        writeValueArray(&vm->gc, &list->elements, peek(vm, 0));
        pop(vm);
        NEXT;
      }
      CASE(OP_MAP_INIT) {
        push(vm, OBJ_VAL(newMap(&vm->gc)));
        NEXT;
      }
      CASE(OP_MAP_DATA) {
        if (!IS_MAP(peek(vm, 2))) {
          runtimeError(vm, "Map data can only be added to a map.");
          return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_STRING(peek(vm, 1))) {
          runtimeError(vm, "Map key must be a string.");
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjMap* map = AS_MAP(peek(vm, 2));
        ObjString* key = AS_STRING(peek(vm, 1));
        tableSet(&vm->gc, &map->table, key, peek(vm, 0));
        pop(vm); // Value.
        pop(vm); // Key.
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
