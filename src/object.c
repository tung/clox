#include "object.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "memory.h"
#include "value.h"

#define ALLOCATE_OBJ(gc, type, objectType) \
  (type*)allocateObject(gc, sizeof(type), objectType)

static Obj* allocateObject(GC* gc, size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(gc, NULL, 0, size);
  object->type = type;
  object->isMarked = false;

  object->next = gc->objects;
  gc->objects = object;

  // GCOV_EXCL_START
  if (debugLogGC) {
    fprintf(
        stderr, "%p allocate %zu for %d\n", (void*)object, size, type);
  }
  // GCOV_EXCL_STOP

  return object;
}

ObjBoundMethod* newBoundMethod(
    GC* gc, Value receiver, ObjClosure* method) {
  ObjBoundMethod* bound =
      ALLOCATE_OBJ(gc, ObjBoundMethod, OBJ_BOUND_METHOD);
  bound->receiver = receiver;
  bound->method = method;
  return bound;
}

ObjClass* newClass(GC* gc, Value name) {
  ObjClass* klass = ALLOCATE_OBJ(gc, ObjClass, OBJ_CLASS);
  klass->name = name;
  initTable(&klass->methods, 0.75);
  return klass;
}

ObjClosure* newClosure(GC* gc, ObjFunction* function) {
  ObjUpvalue** upvalues =
      ALLOCATE(gc, ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure* closure = ALLOCATE_OBJ(gc, ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction* newFunction(GC* gc) {
  ObjFunction* function = ALLOCATE_OBJ(gc, ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NIL_VAL;
  initChunk(&function->chunk);
  return function;
}

ObjInstance* newInstance(GC* gc, ObjClass* klass) {
  ObjInstance* instance = ALLOCATE_OBJ(gc, ObjInstance, OBJ_INSTANCE);
  instance->klass = klass;
  initTable(&instance->fields, 0.75);
  return instance;
}

ObjNative* newNative(GC* gc, NativeFn function) {
  ObjNative* native = ALLOCATE_OBJ(gc, ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

static Value allocateString(
    GC* gc, Table* strings, char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(gc, ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  Value stringValue = OBJ_VAL(string);
  pushTemp(gc, stringValue);
  tableSet(gc, strings, stringValue, NIL_VAL);
  popTemp(gc);
  return stringValue;
}

static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; ++i) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

Value takeString(GC* gc, Table* strings, char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  Value interned = tableFindString(strings, chars, length, hash);
  if (!IS_NIL(interned)) {
    FREE_ARRAY(gc, char, chars, length + 1);
    return interned;
  }

  return allocateString(gc, strings, chars, length, hash);
}

Value copyString(
    GC* gc, Table* strings, const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  Value interned = tableFindString(strings, chars, length, hash);
  if (!IS_NIL(interned)) {
    return interned;
  }

  char* heapChars = ALLOCATE(gc, char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(gc, strings, heapChars, length, hash);
}

ObjUpvalue* newUpvalue(GC* gc, Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(gc, ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

static void printFunction(FILE* fout, ObjFunction* function) {
  if (IS_NIL(function->name)) {
    fprintf(fout, "<script>");
    return;
  }
  fprintf(fout, "<fn ");
  printValue(fout, function->name);
  fprintf(fout, ">");
}

void printObject(FILE* fout, Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_BOUND_METHOD:
      printFunction(fout, AS_BOUND_METHOD(value)->method->function);
      break;
    case OBJ_CLASS: printValue(fout, AS_CLASS(value)->name); break;
    case OBJ_CLOSURE:
      printFunction(fout, AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION: printFunction(fout, AS_FUNCTION(value)); break;
    case OBJ_INSTANCE:
      printValue(fout, AS_INSTANCE(value)->klass->name);
      fprintf(fout, " instance");
      break;
    case OBJ_NATIVE: fprintf(fout, "<native fn>"); break;
    case OBJ_STRING: fprintf(fout, "%s", AS_CSTRING(value)); break;
    case OBJ_UPVALUE: fprintf(fout, "upvalue"); break;
  }
}

const char* objectType(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_BOUND_METHOD: return "bound method";
    case OBJ_CLASS: return "class";
    case OBJ_CLOSURE: return "closure";
    case OBJ_FUNCTION: return "function";
    case OBJ_INSTANCE: return "instance";
    case OBJ_NATIVE: return "native";
    case OBJ_STRING: return "string";
    case OBJ_UPVALUE: return "upvalue";
  }
  return "???"; // GCOV_EXCL_LINE: Unreachable.
}
