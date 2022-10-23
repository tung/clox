#include "membuf.h"

#include <stdlib.h>

void initMemBuf(MemBuf* mb) {
  mb->fptr = open_memstream(&mb->buf, &mb->size);
}

void freeMemBuf(MemBuf* mb) {
  fclose(mb->fptr);
  free(mb->buf);
}
