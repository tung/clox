#include "debug.h"

#include <stdio.h>

#include "value.h"

void disassembleChunk(
    FILE* fout, FILE* ferr, Chunk* chunk, const char* name) {
  fprintf(fout, "== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(fout, ferr, chunk, offset);
  }
}

static int constantInstruction(
    FILE* fout, const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  fprintf(fout, "%-16s %4d '", name, constant);
  printValue(fout, chunk->constants.values[constant]);
  fprintf(fout, "'\n");
  return offset + 2;
}

static int simpleInstruction(FILE* fout, const char* name, int offset) {
  fprintf(fout, "%s\n", name);
  return offset + 1;
}

int disassembleInstruction(
    FILE* fout, FILE* ferr, Chunk* chunk, int offset) {
  fprintf(fout, "%04d ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    fprintf(fout, "   | ");
  } else {
    fprintf(fout, "%4d ", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_CONSTANT:
      return constantInstruction(fout, "OP_CONSTANT", chunk, offset);
    case OP_NIL: return simpleInstruction(fout, "OP_NIL", offset);
    case OP_TRUE: return simpleInstruction(fout, "OP_TRUE", offset);
    case OP_FALSE: return simpleInstruction(fout, "OP_FALSE", offset);
    case OP_EQUAL: return simpleInstruction(fout, "OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction(fout, "OP_GREATER", offset);
    case OP_LESS: return simpleInstruction(fout, "OP_LESS", offset);
    case OP_ADD: return simpleInstruction(fout, "OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction(fout, "OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction(fout, "OP_MULTIPLY", offset);
    case OP_DIVIDE: return simpleInstruction(fout, "OP_DIVIDE", offset);
    case OP_NOT: return simpleInstruction(fout, "OP_NOT", offset);
    case OP_NEGATE: return simpleInstruction(fout, "OP_NEGATE", offset);
    case OP_RETURN: return simpleInstruction(fout, "OP_RETURN", offset);
    default:
      fprintf(ferr, "Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}
