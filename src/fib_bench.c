#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "ubench.h"

UBENCH_EX(Fibonacci, Slow) {
  VM vm;
  InterpretResult ires;
  const char src[] =
      "fun fib(n){if(n<2)return n;return fib(n-1)+fib(n-2);}fib(32);";

  initVM(&vm, stdout, stderr);
  UBENCH_DO_BENCHMARK() {
    ires = interpret(&vm, src);
  }
  assert(ires == INTERPRET_OK);
  freeVM(&vm);
}

UBENCH_MAIN();
