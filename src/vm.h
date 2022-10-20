#pragma once
#ifndef clox_vm_h
#define clox_vm_h

#include <stdio.h>

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value stack[STACK_MAX];
  Value* stackTop;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(VM* vm);
void freeVM(VM* vm);
void push(VM* vm, Value value);
Value pop(VM* vm);
InterpretResult interpretChunk(
    FILE* fout, FILE* ferr, VM* vm, Chunk* chunk);
InterpretResult interpret(
    FILE* fout, FILE* ferr, VM* vm, const char* source);

#endif
