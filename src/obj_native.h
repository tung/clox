#pragma once
#ifndef clox_obj_native_h
#define clox_obj_native_h

#include "object.h"
#include "vm.h"

// clang-format off
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)

#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
// clang-format on

typedef bool (*NativeFn)(VM* vm, int argCount, Value* args);

typedef struct {
  Obj obj;
  int arity;
  NativeFn function;
} ObjNative;

ObjNative* newNative(Obj** objects, NativeFn function, int arity);

#endif
