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

static ObjString* allocateString(
    Obj** objects, char* chars, int length) {
  ObjString* string = ALLOCATE_OBJ(objects, ObjString, OBJ_STRING);
  string->length = length;
  string->chars.rw = chars;
  string->borrowed = false;
  return string;
}

ObjString* takeString(Obj** objects, char* chars, int length) {
  return allocateString(objects, chars, length);
}

ObjString* copyString(Obj** objects, const char* chars, int length) {
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(objects, heapChars, length);
}

ObjString* borrowString(Obj** objects, const char* chars, int length) {
  ObjString* string = allocateString(objects, NULL, length);
  string->chars.ro = chars;
  string->borrowed = true;
  return string;
}

void printObject(FILE* fout, Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      fprintf(
          fout, "%.*s", AS_STRING(value)->length, AS_CSTRING(value));
      break;
  }
}
