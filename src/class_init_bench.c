#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "ubench.h"

UBENCH_EX(Class, Init) {
  VM vm;
  InterpretResult ires;
  const char src[] =
      "class F{init(){}}"
      "for(var i=0;i<500000;i=i+1){F();F();F();F();F();F();F();F();}";

  initVM(&vm, stdout, stderr);
  UBENCH_DO_BENCHMARK() {
    ires = interpret(&vm, src);
  }
  assert(ires == INTERPRET_OK);
  freeVM(&vm);
}

UBENCH_MAIN();
