#pragma once
#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct GC GC;

typedef struct {
  Value key;
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
bool tableGet(Table* table, Value key, Value* value);
bool tableSet(GC* gc, Table* table, Value key, Value value);
bool tableDelete(Table* table, Value key);
void tableAddAll(GC* gc, Table* from, Table* to);
Value tableFindString(
    Table* table, const char* chars, int length, uint32_t hash);
void tableRemoveWhite(Table* table);
void markTable(GC* gc, Table* table);

#endif
