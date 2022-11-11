#include "memory.h"

#include <stdlib.h>

#include "obj_native.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  (void)oldSize;

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);
  // GCOV_EXCL_START
  if (result == NULL)
    exit(1);
  // GCOV_EXCL_STOP
  return result;
}

static void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_NATIVE: FREE(ObjNative, object); break;
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
  }
}

void freeObjects(Obj* objects) {
  Obj* object = objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}

void prependObjects(Obj** to, Obj* from) {
  if (from != NULL) {
    Obj* last = from;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = *to;
    *to = from;
  }
}
