#pragma once
#ifndef clox_vm_h
#define clox_vm_h

#include <stdio.h>

#include "gc.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct {
  FILE* fout;
  FILE* ferr;

  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
  Value* stackTop;
  Table globals;
  Table strings;
  ObjString* initString;
  ObjUpvalue* openUpvalues;

  GC gc;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(VM* vm, FILE* fout, FILE* ferr);
void freeVM(VM* vm);
void push(VM* vm, Value value);
Value pop(VM* vm);
InterpretResult interpretChunk(VM* vm, Chunk* chunk);
InterpretResult interpret(VM* vm, const char* source);

extern bool debugTraceExecution;

#endif
