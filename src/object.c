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

ObjClass* newClass(GC* gc, ObjString* name) {
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
  function->name = NULL;
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

static ObjString* allocateConcatStrings(GC* gc, Table* strings,
    const char* a, int aLen, const char* b, int bLen, uint32_t hash) {
  int length = aLen + bLen;
  char* heapChars = length >= SMALL_STR_MAX_CHARS
      ? ALLOCATE(gc, char, length + 1)
      : NULL;

  ObjString* string = ALLOCATE_OBJ(gc, ObjString, OBJ_STRING);
  string->hash = hash;
  string->length = length;
  if (heapChars) {
    string->chars.ptr = heapChars;
    pushTemp(gc, OBJ_VAL(string));
    tableSet(gc, strings, string, NIL_VAL);
    popTemp(gc);
  } else {
    memcpy(string->chars.small, a, aLen);
    memcpy(string->chars.small + aLen, b, bLen);
    string->chars.small[length] = '\0';
  }

  return string;
}

Value concatStrings(GC* gc, Table* strings, const char* a, int aLen,
    const char* b, int bLen) {
  assert(aLen + bLen >= 0); // GCOV_EXCL_LINE

  uint32_t hash = 2166136261u;
  for (int i = 0; i < aLen; ++i) {
    hash ^= (uint8_t)a[i];
    hash *= 16777619;
  }
  for (int i = 0; i < bLen; ++i) {
    hash ^= (uint8_t)b[i];
    hash *= 16777619;
  }

  ObjString* interned =
      tableFindConcatStrings(strings, a, aLen, b, bLen, hash);
  if (interned != NULL) {
    return OBJ_VAL(interned);
  }

  if (aLen + bLen >= TINY_STR_MAX_CHARS) {
    return OBJ_VAL(
        allocateConcatStrings(gc, strings, a, aLen, b, bLen, hash));
  } else {
    Value str = { .type = VAL_TINY_STR };
    int length = aLen + bLen;
    AS_TINY_STR(str).length = length;
    memcpy(&AS_TINY_STR(str).chars, a, aLen);
    memcpy(&AS_TINY_STR(str).chars + aLen, b, bLen);
    AS_TINY_STR(str).chars[length] = '\0';
    return str;
  }
}

Value copyString(
    GC* gc, Table* strings, const char* chars, int length) {
  return concatStrings(gc, strings, chars, length, "", 0);
}

ObjUpvalue* newUpvalue(GC* gc, Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(gc, ObjUpvalue, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

static void printFunction(FILE* fout, ObjFunction* function) {
  if (function->name == NULL) {
    fprintf(fout, "<script>");
    return;
  }
  fprintf(fout, "<fn %s>", strChars(function->name));
}

void printObject(FILE* fout, Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_BOUND_METHOD:
      printFunction(fout, AS_BOUND_METHOD(value)->method->function);
      break;
    case OBJ_CLASS:
      fprintf(fout, "%s", strChars(AS_CLASS(value)->name));
      break;
    case OBJ_CLOSURE:
      printFunction(fout, AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION: printFunction(fout, AS_FUNCTION(value)); break;
    case OBJ_INSTANCE:
      fprintf(fout, "%s instance",
          strChars(AS_INSTANCE(value)->klass->name));
      break;
    case OBJ_NATIVE: fprintf(fout, "<native fn>"); break;
    case OBJ_STRING: fprintf(fout, "%s", AS_CSTRING(value)); break;
    case OBJ_UPVALUE: fprintf(fout, "upvalue"); break;
  }
}
