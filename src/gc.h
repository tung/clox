#pragma once
#ifndef clox_gc_h
#define clox_gc_h

#include "common.h"
#include "value.h"

typedef struct Obj Obj;

typedef struct GC {
  Obj* objects;
  size_t bytesAllocated;
  size_t nextGC;
  bool mark;

  int grayCount;
  int grayCapacity;
  Obj** grayStack;

  int tempCount;
  int tempCapacity;
  Value* tempStack;

  void (*markRoots)(struct GC*, void*);
  void* markRootsArg;
  void (*fixWeak)(struct GC*, void*);
  void* fixWeakArg;
} GC;

void initGC(GC* gc);
void freeGC(GC* gc);
void pushTemp(GC* gc, Value value);
void popTemp(GC* gc);

#endif
