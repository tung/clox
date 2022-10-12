#pragma once
#ifndef clox_debug_h
#define clox_debug_h

#include <stdio.h>

#include "chunk.h"

void disassembleChunk(
    FILE* fout, FILE* ferr, Chunk* chunk, const char* name);
int disassembleInstruction(
    FILE* fout, FILE* ferr, Chunk* chunk, int offset);

#endif
