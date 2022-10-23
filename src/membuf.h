#pragma once
#ifndef membuf_h
#define membuf_h

#include <stdio.h>

// FILE* stream memory buffer.
typedef struct {
  char* buf;
  size_t size;
  FILE* fptr;
} MemBuf;

void initMemBuf(MemBuf* mb);
void freeMemBuf(MemBuf* mb);

#endif
