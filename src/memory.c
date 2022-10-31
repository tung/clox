#include "memory.h"

#include <stdlib.h>

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
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      if (!string->borrowed) {
        FREE_ARRAY(char, string->chars.rw, string->length + 1);
      }
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
