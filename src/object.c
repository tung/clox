#include "object.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"

#define ALLOCATE_OBJ(objects, type, objectType) \
  (type*)allocateObject(objects, sizeof(type), objectType)

static Obj* allocateObject(Obj** objects, size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;

  assert(objects != NULL); // GCOV_EXCL_LINE
  object->next = *objects;
  *objects = object;
  return object;
}

ObjClosure* newClosure(Obj** objects, ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }

  ObjClosure* closure = ALLOCATE_OBJ(objects, ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjFunction* newFunction(Obj** objects) {
  ObjFunction* function =
      ALLOCATE_OBJ(objects, ObjFunction, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjNative* newNative(Obj** objects, NativeFn function) {
  ObjNative* native = ALLOCATE_OBJ(objects, ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

static ObjString* allocateString(Obj** objects, Table* strings,
    char* chars, int length, uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(objects, ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  tableSet(strings, string, NIL_VAL);
  return string;
}

static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; ++i) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

ObjString* takeString(
    Obj** objects, Table* strings, char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = tableFindString(strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }

  return allocateString(objects, strings, chars, length, hash);
}

ObjString* copyString(
    Obj** objects, Table* strings, const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = tableFindString(strings, chars, length, hash);
  if (interned != NULL) {
    return interned;
  }

  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(objects, strings, heapChars, length, hash);
}

ObjUpvalue* newUpvalue(Obj** objects, Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(objects, ObjUpvalue, OBJ_UPVALUE);
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
  fprintf(fout, "<fn %s>", function->name->chars);
}

void printObject(FILE* fout, Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_CLOSURE:
      printFunction(fout, AS_CLOSURE(value)->function);
      break;
    case OBJ_FUNCTION: printFunction(fout, AS_FUNCTION(value)); break;
    case OBJ_NATIVE: fprintf(fout, "<native fn>"); break;
    case OBJ_STRING: fprintf(fout, "%s", AS_CSTRING(value)); break;
    case OBJ_UPVALUE: fprintf(fout, "upvalue"); break;
  }
}
