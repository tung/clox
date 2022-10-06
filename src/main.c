#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  Chunk chunk;
  initChunk(&chunk);

  int constant = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT, 123);
  writeChunk(&chunk, constant, 123);

  writeChunk(&chunk, OP_RETURN, 123);

  int longConstant = addConstant(&chunk, 3.4);
  writeChunk(&chunk, OP_CONSTANT_LONG, 124);
  writeChunk(&chunk, (longConstant >> 16) & 0xff, 124);
  writeChunk(&chunk, (longConstant >> 8) & 0xff, 124);
  writeChunk(&chunk, longConstant & 0xff, 124);

  disassembleChunk(&chunk, "test chunk");
  freeChunk(&chunk);
  return 0;
}
