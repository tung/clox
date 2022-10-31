#pragma once
#ifndef clox_object_h
#define clox_object_h

#include <stdio.h>

#include "common.h"
#include "value.h"

// clang-format off
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

#define IS_STRING(value)       isObjType(value, OBJ_STRING)

#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars.ro)
// clang-format on

typedef enum {
  OBJ_STRING,
} ObjType;

struct Obj {
  ObjType type;
  struct Obj* next;
};

struct ObjString {
  Obj obj;
  int length;
  union {
    char* rw;
    const char* ro;
  } chars;
  bool borrowed;
};

ObjString* takeString(Obj** objects, char* chars, int length);
ObjString* copyString(Obj** objects, const char* chars, int length);
ObjString* borrowString(Obj** objects, const char* chars, int length);
void printObject(FILE* fout, Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
