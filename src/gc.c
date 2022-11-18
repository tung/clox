#include "gc.h"

#include <assert.h>
#include <stdlib.h>

#include "memory.h"

void initGC(GC* gc) {
  gc->objects = NULL;
  gc->bytesAllocated = 0;
  gc->nextGC = 1024 * 1024;
  gc->mark = true;

  gc->grayCount = 0;
  gc->grayCapacity = 0;
  gc->grayStack = NULL;

  gc->tempCount = 0;
  gc->tempCapacity = 0;
  gc->tempStack = NULL;

  gc->markRoots = NULL;
  gc->markRootsArg = NULL;
  gc->fixWeak = NULL;
  gc->fixWeakArg = NULL;
}

void freeGC(GC* gc) {
  freeObjects(gc);
  gc->objects = NULL;
  free(gc->tempStack);
}

void pushTemp(GC* gc, Value value) {
  if (gc->tempCapacity < gc->tempCount + 1) {
    gc->tempCapacity = GROW_CAPACITY(gc->tempCapacity);
    gc->tempStack = (Value*)realloc(
        gc->tempStack, sizeof(Value) * gc->tempCapacity);
    assert(gc->tempStack != NULL); // GCOV_EXCL_LINE
  }

  gc->tempStack[gc->tempCount++] = value;
}

void popTemp(GC* gc) {
  assert(gc->tempCount > 0); // GCOV_EXCL_LINE
  --gc->tempCount;
}
