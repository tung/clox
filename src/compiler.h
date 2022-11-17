#pragma once
#ifndef clox_compiler_h
#define clox_compiler_h

#include <stdio.h>

#include "chunk.h"
#include "object.h"
#include "table.h"

ObjFunction* compile(
    FILE* fout, FILE* ferr, const char* source, GC* gc, Table* strings);

extern bool debugPrintCode;

#endif
