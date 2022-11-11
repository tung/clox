#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "ubench.h"

UBENCH_EX(Loop, Math) {
  VM vm;
  InterpretResult ires;
  const char src[] =
      "var x=0;"
      "for(var i=0;i<2000000;i=i+1){x=x+(1+2*3/4-5);x=x-(1+2*3/4-5);}";

  initVM(&vm, stdout, stderr);
  UBENCH_DO_BENCHMARK() {
    ires = interpret(&vm, src);
  }
  assert(ires == INTERPRET_OK);
  freeVM(&vm);
}

UBENCH_MAIN();
