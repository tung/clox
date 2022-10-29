#include "memory.h"

#include <stdlib.h>

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  (void)oldSize;

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);
  // GCOV_EXCL_START
  if (result == NULL)
    exit(1);
  // GCOV_EXCL_STOP
  return result;
}
