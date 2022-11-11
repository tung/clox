#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "ubench.h"

UBENCH_EX(Clock, Clock) {
  VM vm;
  InterpretResult ires;
  const char src[] =
      "for(var i=0;i<200000;i=i+1){clock();clock();clock();clock();}";

  initVM(&vm, stdout, stderr);
  UBENCH_DO_BENCHMARK() {
    ires = interpret(&vm, src);
  }
  assert(ires == INTERPRET_OK);
  freeVM(&vm);
}

UBENCH_MAIN();
