#include "table.h"

#include <assert.h>

#include "ubench.h"

#include "gc.h"
#include "object.h"

#define ufx ubench_fixture

struct Table {
  GC gc;
  Table t;
  Table strings;
};

UBENCH_F_SETUP(Table) {
  initGC(&ufx->gc);
  initTable(&ufx->t, 0.75);
  initTable(&ufx->strings, 0.75);
}

UBENCH_F_TEARDOWN(Table) {
  freeTable(&ufx->gc, &ufx->t);
  freeTable(&ufx->gc, &ufx->strings);
  freeGC(&ufx->gc);
}

UBENCH_EX_F(Table, SetGetDelete) {
#define COUNT 12500

  static_assert(COUNT <= 26 * 26 * 26, "Table.SetGetDelete");

  ObjString* oStrs[COUNT];
  char baseStr[] = "aaa";
  size_t temps = 0;

  for (size_t i = 0; i < COUNT; ++i) {
    baseStr[0] = 'a' + i / (26 * 26);
    baseStr[1] = 'a' + (i % (26 * 26)) / 26;
    baseStr[2] = 'a' + i % 26;
    oStrs[i] = copyString(&ufx->gc, &ufx->strings, baseStr, 3);
    pushTemp(&ufx->gc, OBJ_VAL(oStrs[i]));
    temps++;
  }

  UBENCH_DO_BENCHMARK() {
    for (size_t i = 0; i < COUNT; ++i) {
      tableSet(&ufx->gc, &ufx->t, oStrs[i], NUMBER_VAL(i));
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

  while (temps > 0) {
    popTemp(&ufx->gc);
    temps--;
  }

#undef COUNT
}

UBENCH_MAIN();