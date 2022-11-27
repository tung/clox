#pragma once
#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct GC GC;

typedef struct {
  ObjString* key;
  Value value;
} Entry;

typedef struct {
  int count;
  int capacity;
  Entry* entries;
  double maxLoad;
} Table;

void initTable(Table* table, double maxLoad);
void freeTable(GC* gc, Table* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSetEntry(
    Table* table, Entry* entry, ObjString* key, Value value);
bool tableSet(GC* gc, Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(GC* gc, Table* from, Table* to);
Entry* tableJoinedStringsEntry(GC* gc, Table* table, const char* a,
    int aLen, const char* b, int bLen, uint32_t hash);
void tableRemoveWhite(Table* table);
void markTable(GC* gc, Table* table);

#endif
