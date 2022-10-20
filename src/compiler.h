#pragma once
#ifndef clox_compiler_h
#define clox_compiler_h

#include <stdio.h>

#include "chunk.h"
#include "common.h"

bool compile(FILE* fout, FILE* ferr, const char* source, Chunk* chunk);

#endif
