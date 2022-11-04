#include "debug.h"

#include <stdio.h>

#include "value.h"

void disassembleChunk(FILE* ferr, Chunk* chunk, const char* name) {
  fprintf(ferr, "== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(ferr, chunk, offset);
  }
}

static int constantInstruction(
    FILE* ferr, const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  fprintf(ferr, "%-16s %4d '", name, constant);
  printValue(ferr, chunk->constants.values[constant]);
  fprintf(ferr, "'\n");
  return offset + 2;
}

static int simpleInstruction(FILE* ferr, const char* name, int offset) {
  fprintf(ferr, "%s\n", name);
  return offset + 1;
}

static int byteInstruction(
    FILE* ferr, const char* name, Chunk* chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  fprintf(ferr, "%-16s %4d\n", name, slot);
  return offset + 2;
}

static int jumpInstruction(
    FILE* ferr, const char* name, int sign, Chunk* chunk, int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  fprintf(ferr, "%-16s %4d -> %d\n", name, offset,
      offset + 3 + sign * jump);
  return offset + 3;
}

int disassembleInstruction(FILE* ferr, Chunk* chunk, int offset) {
  fprintf(ferr, "%04d ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    fprintf(ferr, "   | ");
  } else {
    fprintf(ferr, "%4d ", chunk->lines[offset]);
  }

  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_CONSTANT:
      return constantInstruction(ferr, "OP_CONSTANT", chunk, offset);
    case OP_NIL: return simpleInstruction(ferr, "OP_NIL", offset);
    case OP_TRUE: return simpleInstruction(ferr, "OP_TRUE", offset);
    case OP_FALSE: return simpleInstruction(ferr, "OP_FALSE", offset);
    case OP_POP: return simpleInstruction(ferr, "OP_POP", offset);
    case OP_GET_LOCAL:
      return byteInstruction(ferr, "OP_GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL:
      return byteInstruction(ferr, "OP_SET_LOCAL", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction(ferr, "OP_GET_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction(
          ferr, "OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction(ferr, "OP_SET_GLOBAL", chunk, offset);
    case OP_EQUAL: return simpleInstruction(ferr, "OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction(ferr, "OP_GREATER", offset);
    case OP_LESS: return simpleInstruction(ferr, "OP_LESS", offset);
    case OP_ADD: return simpleInstruction(ferr, "OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction(ferr, "OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction(ferr, "OP_MULTIPLY", offset);
    case OP_DIVIDE: return simpleInstruction(ferr, "OP_DIVIDE", offset);
    case OP_NOT: return simpleInstruction(ferr, "OP_NOT", offset);
    case OP_NEGATE: return simpleInstruction(ferr, "OP_NEGATE", offset);
    case OP_PRINT: return simpleInstruction(ferr, "OP_PRINT", offset);
    case OP_JUMP:
      return jumpInstruction(ferr, "OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction(
          ferr, "OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_LOOP:
      return jumpInstruction(ferr, "OP_LOOP", -1, chunk, offset);
    case OP_RETURN: return simpleInstruction(ferr, "OP_RETURN", offset);
    default:
      fprintf(ferr, "Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}
