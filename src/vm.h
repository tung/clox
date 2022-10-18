#pragma once
#ifndef clox_vm_h
#define clox_vm_h

#include <stdio.h>

#include "chunk.h"
#include "value.h"

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value* stack;
  size_t stackCount;
  size_t stackCapacity;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(VM* vm);
void freeVM(VM* vm);
InterpretResult interpret(FILE* fout, FILE* ferr, VM* vm, Chunk* chunk);
void push(VM* vm, Value value);
Value pop(VM* vm);

#endif
