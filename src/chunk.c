#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lineCount = 0;
  chunk->lineCapacity = 0;
  chunk->lines = NULL;
  initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  freeValueArray(&chunk->constants);
  initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code,
        oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->count++;

  if (line > chunk->lineCapacity) {
    int oldLineCapacity = chunk->lineCapacity;
    while (line > chunk->lineCapacity) {
      chunk->lineCapacity = GROW_CAPACITY(chunk->lineCapacity);
    }
    chunk->lines = GROW_ARRAY(int, chunk->lines,
        oldLineCapacity, chunk->lineCapacity);
    for (int i = oldLineCapacity; i < chunk->lineCapacity; ++i) {
      chunk->lines[i] = 0;
    }
  }

  if (line > chunk->lineCount) {
    chunk->lineCount = line;
  }

  chunk->lines[line - 1]++;
}

int getLine(Chunk* chunk, int instructionIndex) {
  int line = 0;
  int instructionsPassed = 0;
  while (line < chunk->lineCapacity && instructionsPassed <= instructionIndex) {
    instructionsPassed += chunk->lines[line];
    line++;
  }
  return line;
}

int addConstant(Chunk* chunk, Value value) {
  writeValueArray(&chunk->constants, value);
  return chunk->constants.count - 1;
}
