#pragma once
#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(gc, type, count) \
  (type*)reallocate(gc, NULL, 0, sizeof(type) * (count))

#define FREE(gc, type, pointer) reallocate(gc, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(gc, type, pointer, oldCount, newCount) \
  (type*)reallocate(gc, pointer, sizeof(type) * (oldCount), \
      sizeof(type) * (newCount))

#define FREE_ARRAY(gc, type, pointer, oldCount) \
  reallocate(gc, pointer, sizeof(type) * (oldCount), 0)

void* reallocate(GC* gc, void* pointer, size_t oldSize, size_t newSize);
void markObject(GC* gc, Obj* object);
void markValue(GC* gc, Value value);
void collectGarbage(GC* gc);
void freeObjects(GC* gc); // Should only be called by freeGC().

extern bool debugLogGC;
extern bool debugStressGC;

#endif
