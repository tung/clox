#include "table.h"

#include <assert.h>

#include "ubench.h"

#include "memory.h"

#define ufx ubench_fixture

struct Table {
  Table t;
  Obj* objects;
  Table strings;
};

UBENCH_F_SETUP(Table) {
  initTable(&ufx->t, 0.75);
  ufx->objects = NULL;
  initTable(&ufx->strings, 0.75);
}

UBENCH_F_TEARDOWN(Table) {
  freeTable(&ufx->t);
  freeTable(&ufx->strings);
  freeObjects(ufx->objects);
}

UBENCH_EX_F(Table, SetGetDelete) {
#define COUNT 12500

  static_assert(COUNT <= 26 * 26 * 26, "Table.SetGetDelete");

  ObjString* oStrs[COUNT];
  char baseStr[] = "aaa";

  for (size_t i = 0; i < COUNT; ++i) {
    baseStr[0] = 'a' + i / (26 * 26);
    baseStr[1] = 'a' + (i % (26 * 26)) / 26;
    baseStr[2] = 'a' + i % 26;
    oStrs[i] = copyString(&ufx->objects, &ufx->strings, baseStr, 3);
  }

  UBENCH_DO_BENCHMARK() {
    for (size_t i = 0; i < COUNT; ++i) {
      tableSet(&ufx->t, oStrs[i], NUMBER_VAL(i));
    }

    for (size_t i = 0; i < COUNT; ++i) {
      Value v;
      tableGet(&ufx->t, oStrs[i], &v);
      UBENCH_DO_NOTHING(&v);
    }

    for (size_t i = 0; i < COUNT; ++i) {
      tableDelete(&ufx->t, oStrs[i]);
    }
  }

#undef COUNT
}

UBENCH_MAIN();