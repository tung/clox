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

ObjBoundMethod* newBoundMethod(GC* gc, Value receiver, Obj* method) {
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

static ObjString* allocateString(GC* gc) {
  ObjString* string = ALLOCATE_OBJ(gc, ObjString, OBJ_STRING);
  string->length = 0;
  string->hash = 0u;
  string->chars.ptr = NULL;
  return string;
}

#define INIT_HASH 2166136261u

static uint32_t hashAnotherString(
    uint32_t hash, const char* key, int length) {
  for (int i = 0; i < length; ++i) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

ObjString* concatStrings(GC* gc, Table* strings, const char* a,
    int aLen, uint32_t aHash, const char* b, int bLen) {
  assert(aLen + bLen >= 0); // GCOV_EXCL_LINE

  uint32_t hash = hashAnotherString(aHash, b, bLen);
  Entry* entry =
      tableJoinedStringsEntry(gc, strings, a, aLen, b, bLen, hash);
  if (entry->key != NULL) {
    // Concatenated string already interned.
    return entry->key;
  }

  int length = aLen + bLen;
  char* heapChars = NULL;
  if (length >= SMALL_STRING_SPACE) {
    heapChars = ALLOCATE(gc, char, length + 1);
    memcpy(heapChars, a, aLen);
    memcpy(heapChars + aLen, b, bLen);
    heapChars[length] = '\0';
  }

  ObjString* string = allocateString(gc);
  string->length = length;
  string->hash = hash;

  if (heapChars) {
    string->chars.ptr = heapChars;
  } else {
    char* chars = string->chars.small;
    memcpy(chars, a, aLen);
    memcpy(chars + aLen, b, bLen);
    chars[length] = '\0';
  }

  tableSetEntry(strings, entry, string, NIL_VAL);
  return string;
}

ObjString* copyString(
    GC* gc, Table* strings, const char* chars, int length) {
  uint32_t hash = hashAnotherString(INIT_HASH, chars, length);
  return concatStrings(gc, strings, chars, length, hash, "", 0);
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
    case OBJ_BOUND_METHOD: {
      Obj* method = AS_BOUND_METHOD(value)->method;
      ObjFunction* methodFun;
      if (method->type == OBJ_CLOSURE) {
        methodFun = ((ObjClosure*)method)->function;
      } else {
        methodFun = (ObjFunction*)method;
      }
      printFunction(fout, methodFun);
      break;
    }
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
