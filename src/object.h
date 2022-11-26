#pragma once
#ifndef clox_object_h
#define clox_object_h

#include <stdio.h>

#include "chunk.h"
#include "common.h"
#include "gc.h"
#include "table.h"
#include "value.h"

// clang-format off
#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)       isObjType(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      strChars((ObjString*)AS_OBJ(value))
// clang-format on

typedef enum {
  OBJ_BOUND_METHOD,
  OBJ_CLASS,
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_INSTANCE,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE,
} ObjType;

struct Obj {
  ObjType type;
  bool isMarked;
  struct Obj* next;
};

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  Value name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;

struct ObjString {
  Obj obj;
  int length;
  uint32_t hash;
  union {
    char small[sizeof(char*)];
    char* ptr;
  } chars;
};

#define SMALL_STR_MAX_CHARS ((int)sizeof((ObjString){}.chars.small))

typedef struct ObjUpvalue {
  Obj obj;
  Value* location;
  Value closed;
  struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
} ObjClosure;

typedef struct {
  Obj obj;
  Value name;
  Table methods;
} ObjClass;

typedef struct {
  Obj obj;
  ObjClass* klass;
  Table fields;
} ObjInstance;

typedef struct {
  Obj obj;
  Value receiver;
  ObjClosure* method;
} ObjBoundMethod;

ObjBoundMethod* newBoundMethod(
    GC* gc, Value receiver, ObjClosure* method);
ObjClass* newClass(GC* gc, Value name);
ObjClosure* newClosure(GC* gc, ObjFunction* function);
ObjFunction* newFunction(GC* gc);
ObjInstance* newInstance(GC* gc, ObjClass* klass);
ObjNative* newNative(GC* gc, NativeFn function);
Value concatStrings(GC* gc, Table* strings, const char* a, int aLen,
    const char* b, int bLen);
Value copyString(GC* gc, Table* strings, const char* chars, int length);
ObjUpvalue* newUpvalue(GC* gc, Value* slot);
void printObject(FILE* fout, Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

static inline int strLen(Value str) {
  return IS_TINY_STR(str) ? AS_TINY_STR(str).length
                          : AS_STRING(str)->length;
}

static inline char* strChars(Value* str) {
  if (str->type == VAL_TINY_STR) {
    return (char*)&str->as.tinyStr.chars;
  } else {
    ObjString* oStr = (ObjString*)str->as.obj;
    if (oStr->length < SMALL_STR_MAX_CHARS) {
      return (char*)&oStr->chars.small;
    } else {
      return oStr->chars.ptr;
    }
  }
}

#endif
