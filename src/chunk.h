#pragma once
#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef struct GC GC;

typedef enum {
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_GET_GLOBAL_I,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_SET_GLOBAL_I,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_INDEX,
  OP_SET_INDEX,
  OP_GET_SUPER,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_LESS_C,
  OP_ADD,
  OP_ADD_C,
  OP_SUBTRACT,
  OP_SUBTRACT_C,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_MODULO,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_PJMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_INVOKE,
  OP_SUPER_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_LIST_INIT,
  OP_LIST_DATA,
  OP_MAP_INIT,
  OP_MAP_DATA,
  OP_RETURN,
  OP_CLASS,
  OP_INHERIT,
  OP_METHOD,
  MAX_OPCODES
} OpCode;

typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  int* lines;
  ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(GC* gc, Chunk* chunk);
void writeChunk(GC* gc, Chunk* chunk, uint8_t byte, int line);
int addConstant(GC* gc, Chunk* chunk, Value value);
int findConstant(Chunk* chunk, Value value);

#endif
