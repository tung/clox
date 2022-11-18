#include "memory.h"

#include <stdlib.h>

#include "debug.h"
#include "gc.h"

#define GC_HEAP_GROW_FACTOR 2

bool debugLogGC = false;
bool debugStressGC = false;

void* reallocate(
    GC* gc, void* pointer, size_t oldSize, size_t newSize) {
  gc->bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
    if (debugStressGC) {
      collectGarbage(gc);
    }

    if (gc->bytesAllocated > gc->nextGC) {
      collectGarbage(gc);
    }
  }

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

void markObject(GC* gc, Obj* object) {
  if (object == NULL) {
    return;
  }
  if (object->mark == gc->mark) {
    return;
  }
  // GCOV_EXCL_START
  if (debugLogGC) {
    fprintf(stderr, "%p mark ", (void*)object);
    printValue(stderr, OBJ_VAL(object));
    fprintf(stderr, "\n");
  }
  // GCOV_EXCL_STOP

  object->mark = gc->mark;
  if (gc->grayCapacity < gc->grayCount + 1) {
    gc->grayCapacity = GROW_CAPACITY(gc->grayCapacity);
    gc->grayStack =
        (Obj**)realloc(gc->grayStack, sizeof(Obj*) * gc->grayCapacity);

    // GCOV_EXCL_START
    if (gc->grayStack == NULL) {
      exit(1);
    }
    // GCOV_EXCL_STOP
  }

  gc->grayStack[gc->grayCount++] = object;
}

void markValue(GC* gc, Value value) {
  if (IS_OBJ(value)) {
    markObject(gc, AS_OBJ(value));
  }
}

static void markArray(GC* gc, ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(gc, array->values[i]);
  }
}

static void blackenObject(GC* gc, Obj* object) {
  // GCOV_EXCL_START
  if (debugLogGC) {
    fprintf(stderr, "%p blacken ", (void*)object);
    printValue(stderr, OBJ_VAL(object));
    fprintf(stderr, "\n");
  }
  // GCOV_EXCL_STOP

  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject(gc, (Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject(gc, (Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject(gc, (Obj*)function->name);
      markArray(gc, &function->chunk.constants);
      break;
    }
    case OBJ_UPVALUE:
      markValue(gc, ((ObjUpvalue*)object)->closed);
      break;
    case OBJ_NATIVE:
    case OBJ_STRING: break;
  }
}

static void freeObject(GC* gc, Obj* object) {
  // GCOV_EXCL_START
  if (debugLogGC) {
    fprintf(stderr, "%p free type %d\n", (void*)object, object->type);
  }
  // GCOV_EXCL_STOP

  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(
          gc, ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      FREE(gc, ObjClosure, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(gc, &function->chunk);
      FREE(gc, ObjFunction, object);
      break;
    }
    case OBJ_NATIVE: FREE(gc, ObjNative, object); break;
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(gc, char, string->chars, string->length + 1);
      FREE(gc, ObjString, object);
      break;
    }
    case OBJ_UPVALUE: FREE(gc, ObjUpvalue, object); break;
  }
}

static void traceReferences(GC* gc) {
  while (gc->grayCount > 0) {
    Obj* object = gc->grayStack[--gc->grayCount];
    blackenObject(gc, object);
  }
}

static void sweep(GC* gc) {
  Obj* previous = NULL;
  Obj* object = gc->objects;
  while (object != NULL) {
    if (object->mark == gc->mark) {
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        gc->objects = object;
      }

      freeObject(gc, unreached);
    }
  }
}

void collectGarbage(GC* gc) {
  // GCOV_EXCL_START
  if (debugLogGC) {
    fprintf(stderr, "-- gc begin\n");
  }
  // GCOV_EXCL_STOP
  size_t before = gc->bytesAllocated;

  for (int i = 0; i < gc->tempCount; i++) {
    markValue(gc, gc->tempStack[i]);
  }
  if (gc->markRoots) {
    gc->markRoots(gc, gc->markRootsArg);
  }
  traceReferences(gc);
  if (gc->fixWeak) {
    gc->fixWeak(gc, gc->fixWeakArg);
  }
  sweep(gc);
  gc->mark = !gc->mark;

  gc->nextGC = gc->bytesAllocated * GC_HEAP_GROW_FACTOR;

  // GCOV_EXCL_START
  if (debugLogGC) {
    fprintf(stderr, "-- gc end\n");
    fprintf(stderr,
        "   collected %zu bytes (from %zu to %zu) next at %zu\n",
        before - gc->bytesAllocated, before, gc->bytesAllocated,
        gc->nextGC);
  }
  // GCOV_EXCL_STOP
}

// Should only be called by freeGC(); call that instead.
void freeObjects(GC* gc) {
  Obj* object = gc->objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(gc, object);
    object = next;
  }

  free(gc->grayStack);
}
