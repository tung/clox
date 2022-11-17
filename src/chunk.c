#include "chunk.h"

#include <stdlib.h>

#include "gc.h"
#include "memory.h"

void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueArray(&chunk->constants);
}

void freeChunk(GC* gc, Chunk* chunk) {
  FREE_ARRAY(gc, uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(gc, int, chunk->lines, chunk->capacity);
  freeValueArray(gc, &chunk->constants);
  initChunk(chunk);
}

void writeChunk(GC* gc, Chunk* chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(
        gc, uint8_t, chunk->code, oldCapacity, chunk->capacity);
    chunk->lines =
        GROW_ARRAY(gc, int, chunk->lines, oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

int addConstant(GC* gc, Chunk* chunk, Value value) {
  pushTemp(gc, value);
  writeValueArray(gc, &chunk->constants, value);
  popTemp(gc);
  return chunk->constants.count - 1;
}
