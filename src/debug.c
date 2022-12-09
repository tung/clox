#include "debug.h"

#include <stdio.h>

#include "object.h"
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

static int invokeInstruction(
    FILE* ferr, const char* name, Chunk* chunk, int offset) {
  uint8_t constant = chunk->code[offset + 1];
  uint8_t argCount = chunk->code[offset + 2];
  fprintf(ferr, "%-16s (%d args) %4d '", name, argCount, constant);
  printValue(ferr, chunk->constants.values[constant]);
  fprintf(ferr, "'\n");
  return offset + 3;
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

static int shortInstruction(
    FILE* ferr, const char* name, Chunk* chunk, int offset) {
  uint16_t slot = (uint16_t)(chunk->code[offset + 1] << 8);
  slot |= chunk->code[offset + 2];
  fprintf(ferr, "%-16s %4d\n", name, slot);
  return offset + 3;
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
      return constantInstruction(ferr, "OP_GET_GLOBAL", chunk, offset) +
          1;
    case OP_GET_GLOBAL_I:
      return shortInstruction(ferr, "OP_GET_GLOBAL_I", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction(
          ferr, "OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction(ferr, "OP_SET_GLOBAL", chunk, offset) +
          1;
    case OP_SET_GLOBAL_I:
      return shortInstruction(ferr, "OP_SET_GLOBAL_I", chunk, offset);
    case OP_GET_UPVALUE:
      return byteInstruction(ferr, "OP_GET_UPVALUE", chunk, offset);
    case OP_SET_UPVALUE:
      return byteInstruction(ferr, "OP_SET_UPVALUE", chunk, offset);
    case OP_GET_PROPERTY:
      return constantInstruction(
          ferr, "OP_GET_PROPERTY", chunk, offset);
    case OP_SET_PROPERTY:
      return constantInstruction(
          ferr, "OP_SET_PROPERTY", chunk, offset);
    case OP_GET_INDEX:
      return simpleInstruction(ferr, "OP_GET_INDEX", offset);
    case OP_SET_INDEX:
      return simpleInstruction(ferr, "OP_SET_INDEX", offset);
    case OP_GET_SUPER:
      return constantInstruction(ferr, "OP_GET_SUPER", chunk, offset);
    case OP_EQUAL: return simpleInstruction(ferr, "OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction(ferr, "OP_GREATER", offset);
    case OP_LESS: return simpleInstruction(ferr, "OP_LESS", offset);
    case OP_LESS_C:
      return constantInstruction(ferr, "OP_LESS_C", chunk, offset);
    case OP_ADD: return simpleInstruction(ferr, "OP_ADD", offset);
    case OP_ADD_C:
      return constantInstruction(ferr, "OP_ADD_C", chunk, offset);
    case OP_SUBTRACT:
      return simpleInstruction(ferr, "OP_SUBTRACT", offset);
    case OP_SUBTRACT_C:
      return constantInstruction(ferr, "OP_SUBTRACT_C", chunk, offset);
    case OP_MULTIPLY:
      return simpleInstruction(ferr, "OP_MULTIPLY", offset);
    case OP_DIVIDE: return simpleInstruction(ferr, "OP_DIVIDE", offset);
    case OP_MODULO: return simpleInstruction(ferr, "OP_MODULO", offset);
    case OP_NOT: return simpleInstruction(ferr, "OP_NOT", offset);
    case OP_NEGATE: return simpleInstruction(ferr, "OP_NEGATE", offset);
    case OP_PRINT: return simpleInstruction(ferr, "OP_PRINT", offset);
    case OP_JUMP:
      return jumpInstruction(ferr, "OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction(
          ferr, "OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_PJMP_IF_FALSE:
      return jumpInstruction(
          ferr, "OP_PJMP_IF_FALSE", 1, chunk, offset);
    case OP_LOOP:
      return jumpInstruction(ferr, "OP_LOOP", -1, chunk, offset);
    case OP_CALL:
      return byteInstruction(ferr, "OP_CALL", chunk, offset);
    case OP_INVOKE:
      return invokeInstruction(ferr, "OP_INVOKE", chunk, offset);
    case OP_SUPER_INVOKE:
      return invokeInstruction(ferr, "OP_SUPER_INVOKE", chunk, offset);
    case OP_CLOSURE: {
      offset++;
      uint8_t constant = chunk->code[offset++];
      fprintf(ferr, "%-16s %4d ", "OP_CLOSURE", constant);
      printValue(ferr, chunk->constants.values[constant]);
      fprintf(ferr, "\n");

      ObjFunction* function =
          AS_FUNCTION(chunk->constants.values[constant]);
      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        fprintf(ferr, "%04d      |                     %s %d\n",
            offset - 2, isLocal ? "local" : "upvalue", index);
      }

      return offset;
    }
    case OP_CLOSE_UPVALUE:
      return simpleInstruction(ferr, "OP_CLOSE_UPVALUE", offset);
    case OP_LIST_INIT:
      return simpleInstruction(ferr, "OP_LIST_INIT", offset);
    case OP_LIST_DATA:
      return simpleInstruction(ferr, "OP_LIST_DATA", offset);
    case OP_MAP_INIT:
      return simpleInstruction(ferr, "OP_MAP_INIT", offset);
    case OP_MAP_DATA:
      return simpleInstruction(ferr, "OP_MAP_DATA", offset);
    case OP_RETURN: return simpleInstruction(ferr, "OP_RETURN", offset);
    case OP_CLASS:
      return constantInstruction(ferr, "OP_CLASS", chunk, offset);
    case OP_INHERIT:
      return simpleInstruction(ferr, "OP_INHERIT", offset);
    case OP_METHOD:
      return constantInstruction(ferr, "OP_METHOD", chunk, offset);
    default:
      fprintf(ferr, "Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}
