# clox

This is my C implementation of the bytecode virtual machine interpreter for the Lox programming language.
I made this while following along the final part of [Robert Nystrom's Crafting Interpreters](https://craftinginterpreters.com/), and then added some extra stuff on top of that; this is the result.

This is was a learning project for a toy language, so I probably won't respond to issues or pull requests.
Still, it's small, fast and handy for quick scripts, so here it is.

## Features

This has all of the features of baseline Lox ([described in detail here](https://craftinginterpreters.com/the-lox-language.html)):

- dynamically-typed values
- garbage collection
- expressions and statements
- variables
- branches and loops
- functions
- closures
- classes with instances and inheritance

There's also a bunch of extra stuff I added on top of all of this:

- modulo operator (`%`)
- anonymous function values (a.k.a. lambdas)
- lists
- maps
- element/field indexing (`[]`) for strings, lists, maps and instances
- methods for strings, lists and maps
- optional trailing commas for comma-separated lists
- more native functions, mostly for dealing with strings, numbers and other values
- higher limit for constants per function (65535 versus baseline clox's 255)
- REPL input improvements: multi-line input, line editing and more
- shebang support: ignore the first line of a script if it starts with a `#` character
- support for scripts piped in via standard input
- various optimizations: computed gotos, faster global variable access, fused opcodes

## Code Examples

Stuff that baseline Lox features:

```
// Line comments start with two slashes.

// Global variables are declared before use.
var a;           // = nil
var b = nil;
var c = true;    // Booleans.
var d = false;
var e = 123.456; // Numbers are double-precision floating point values.
var f = "hi";

// Local variables in block scopes.
{
  var x = "foo";
  var y = "bar";
  print x + y; // foobar
}

// Expressions.
print 1 + 2 * 3 / 4 - 5;       // -2.5
print "Hello" + " " + "world." // Hello world.

// Branches, comparisons and short-circuiting logical expressions.
if (1 < 2 and !(3 >= 4)) {
  print "Yeah!";
} else {
  print "oh...";
}
if (true or false) {
  print "okay";
}

// While loops.
var g = 0;
while (g < 5) {
  print g;
  g = g + 1;
}

// For loops.
for (var i = 0; i < 5; i = i + 1) {
  print i;
}

// Functions.
fun greet(greeting, noun) {
  print greeting + " " + noun;
}
greet("hello", "world"); // Hello world.

// Closures.
fun makeCounter(x) {
  fun printAndIncrement() {
    print x;
    x = x + 1;
  }
  return printAndIncrement;
}
var myCounter = makeCounter(1);
print myCounter(); // 1
print myCounter(); // 2
print myCounter(); // 3

// Classes.
class Animal {
  init(name, noise) {
    this.name = name;
    this.noise = noise;
  }
  speak() {
    print "The " + this.name + " " + this.noise + ".";
  }
  act() {
    print "The " + this.name + " does something.";
  }
}
class Dog < Animal {
  init() {
    super.init("dog", "barks");
  }
  act() {
    print "The " + this.name + " runs around!";
  }
}
var cat = Animal("Cat", "meows");
cat.speak();     // The cat meows.
cat.act();       // The cat does something.
var dog = Dog();
dog.speak();     // The dog barks.
dog.act();       // The dog runs around!

// clock - seconds since launch
fun slow() {
  fun fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
  }
  var start = clock();
  print fib(30);
  return clock() - start;
}
print slow(); // Print time to run fib(30) in seconds.
```

Stuff I added on top of baseline Lox:

```
// str - convert values into strings.
// "1 plus 2 equals 3"
print str(1) + " plus " + str(2) + " equals " + str(1 + 2);

// string indexing and chr
print "a"[0];  // 97
print chr(97); // a

// string methods
print "hello world".size();                // 11
print "123".parsenum() + "456".parsenum(); // 579
print "hello world".substr(0, 5);          // hello
print "hello world".substr(-6, -1);        // world

// ceil, floor, round
print ceil(1.5);  // 2
print floor(1.5); // 1
print round(1.5); // 2

// type
print type(nil);  // nil
print type(true); // boolean
print type(0);    // number
print type("hi"); // string
fun f() {}
print type(f);    // function
print type(type); // native function
class C{}
print type(C);    // class
print type(C());  // instance
print type([]);   // list
print type({});   // map

// argc and argv - argument count and values
for (var i = 0; i < argc(); i = i + 1) {
  // Print each command line argument.
  print argv(i);
}

// eprint and exit
if (false) {
  eprint("This shouldn't have happened!");
  exit(1);
}

// Lists.
var a = [1, 2 + 3, nil, "hi"];
// Print list indices and values.
// a[0] = 1;
// a[1] = 5;
// a[2] = nil;
// a[3] = hi;
for (var i = 0; i < a.size(); i = i + 1) {
  print "a[" + str(i) + "] = " + str(a[i]);
}
print a.size();     // 4
a.push("foo");      // Append to end of list.
print a.pop();      // foo
a.insert(0, "bar"); // Insert "bar" at position 0.
print a.remove(0);  // bar

// Maps.
var m = {one: 1, ["t" + "wo"]: 1 + 1};
m["three"] = 3;
print m.count(); // 3
if (m.has("three")) {
  m.remove("three");
}
var keys = m.keys();
// Print map keys and values; order not guaranteed.
// one: 1
// two: 2
for (var k = 0; k < keys.size(); k = k + 1) {
  print keys[k] + ": " + m[keys[k]];
}
```

## New Native Functions

### argc()

Return the number of command line arguments the interpreter was launched with.

### argv(i)

Return the command line argument at position *i*;
zero should be the interpreter program, and numbers up to (but not including) the return value of `argc` are additional arguments.

### ceil(n)

Returns the number *n* rounded up to the nearest whole number towards positive infinity.

### chr(b)

Returns a one-character string whose first byte is the number represented by *b*.

The valid range for *b* depends on how the platform treats C's `char` type;
for signed `char` it can be any whole number between -128 and 127 inclusive.

### eprint(v)

Prints the value *v* to the standard error stream.

### exit(n)

Exit the process with the number *n* as the exit code.
*n* must be a whole number between 0 and 255 inclusive.

### floor(n)

Return the number *n* rounded down to the nearest whole number towards negative infinity.

### round(n)

Return the number *n* rounded to the nearest whole number; 0.5 is rounded up.

### str(v)

Convert the value *v* into a string.

### type(v)

Return the type of the value *v* as a string, which can be one of:

- `boolean`
- `nil`
- `number`
- `function`
- `native function`
- `class`
- `instance`
- `list`
- `map`
- `string`

## New Methods

### String Methods

#### string.parsenum()

Parse a number from the string and return it.
Leading and trailing whitespace are ignored.
If any other characters exist or there's no number to parse, nil is returned instead.

#### string.size()

Return the length of the string, in bytes.
This doesn't include the internal null terminator, so an empty string returns zero.

#### string.substr(s, e)

Return the substring starting from position *s*, up to (but not including) position *e*.

If either *s* or *e* are negative, the string length plus one is added to it;
this offsets positions relative to the end of the string.

The final values of *s* and *e* are clamped between zero and the string length.
A non-empty string is returned only if *e* is greater than *s* at this point.

### List Methods

#### list.insert(i, v)

Insert the value *v* into position *i* of the list, shuffling all elements after back by one position.

The index *i* must be between zero and the length of the list minus one.

#### list.pop()

Remove and return the last element of the list.

An error is raised if the list is empty.

#### list.push(v)

Append the value *v* to the end of the list.

#### list.remove(i)

Remove and return the element at position *i* in the list.

The index *i* must be between zero and the length of the list.

#### list.size()

Return the number of elements in the list.

### Map Methods

#### map.count()

Scan through the storage of the map and return the count of the valid keys.

#### map.has(k)

Return true if the string key *k* is present in the map.

#### map.keys()

Return a list of strings containing all of the keys of the map.

#### map.remove(k)

Remove the entry with the string key *k* from the map and return true if it was present.

## How to Build

Build requirements:

- GCC or Clang
- GNU Make
- Bash

Type `make MODE=release build` and run `./build/release/clox`.

## Running clox

Run `clox` with `-h`, `-?` or `--help` for the following help text:

```
$ clox -h
clox release-git-dedbeef

Usage: clox [options] [path]

   -D, --dump           (debug) Dump disassembled script
   -T, --trace          (debug) Trace script execution
   -L, --log-gc         (debug) Log garbage collector
   -S, --stress-gc      (debug) Always collect garbage
   -h, -?, --help       Show help (this message) and exit
   -v, --version        Show version information and exit
```

Provide a path to a script to run it, e.g. `clox path/to/script.lox`.
Giving `-` as the path will read the script from standard input.
The first line of a script is treated as a comment if it starts with a `#` character, so this is allowed:

```
$ cat - >EOF foo.lox
#!/path/to/clox
print "hello from clox";
EOF
$ chmod +x foo.lox
$ ./foo.lox
hello from clox
```

Bring up the REPL by running `clox` without a path.

```
$ clox
clox release-git-dedbeef
> print 1 + 2;
3
```

Put `=` at the start of your input to quickly print an expression.

```
> = 1 + 2
3
```

Put `\` at the end of a line for multi-line input:

```
> print "hello" +\
" world";
hello world
```

## Working On and Testing clox

Type `make test` to run all of the tests.

All the tests live in the `src/` directory as source files ending with `_test.c`.
The `interpret_test.c` file contains all of the integration tests; the others are unit tests.

Type `make gcovr` to run all of the tests in code coverage mode and produce a HTML report;
you'll need [gcovr](https://gcovr.com) installed (`pip install gcovr`).

At the time of writing, there are over 950 tests, but testing should finish in an instant.
The current set of tests have 99% line and function coverage and 98% branch coverage, though some lines are excluded for things like memory exhaustion and integer count limits.

Type `make MODE=release bench` to run all of the benchmarks (files ending with `_bench.c`).
Benchmark times can vary, so you'll want to run them a few times for consistent numbers.

All source code is formatted with `make format` using `clang-format` before committing.
This can be set up as a git pre-commit hook as follows:

```
$ ln -s ../../git-pre-commit.sh .git/hooks/pre-commit
```

You can create a `compile_commands.json` for LSP-enabled code editors using [Bear](https://github.com/rizsotto/Bear):

```
$ make clean && bear make
```

## Interesting Branches

Most of the end-of-chapter challenges of Crafting Interpreters exist as git branches:

- 9481810 - `14-chunks-of-bytecode-extra`
- 9e5ce99 - `15-a-virtual-machine-extra`
- 17d0a98 - `17-compiling-expressions-extra`
- 1e38a34 - `19-strings-extra`
- 42138d0 - `20-hash-tables-extra`
- 7191851 - `21-global-variables-extra`
- 8ac0413 - `22-local-variables-extra`
- fa731e4 - `23-jumping-back-and-forth-extra`
- 629749c - `24-calls-and-functions-extra`
- df9c9f2 - `25-closures-extra`
- 381e1d0 - `26-garbage-collection-extra`
- d703065 - `27-classes-and-instances-extra`
- 7e340cb - `28-methods-and-initializers-extra`

Performance experment branches that didn't work out:

- 1d3e5b4 - `push-pop-inline`: Inlining VM stack push/pop operations.
- 3c0ecaf - `small-strings-delete-me`: Small string optimization (inline with values).

## Code Compared to Baseline clox

I use "baseline clox" to refer to the code from following the book up to its final chapter.
That chapter is "Optimization"; here's the commit: d5fc557

I tried to stay close to the code as written in Crafting Interpreters, but there are still some differences.

### Output and Error Stream Handling

The VM is initialized with custom output and error streams in the form of `FILE*`.
This is needed so that tests can swap in custom streams and check outputs.
These custom streams are made using `open_memstream` within `membuf.c` and `membuf.h`.

### Less Global Variables

Unlike the original code, the parser and the VM are *not* global variables;
instead, they're created explicitly and passed around as arguments.
This has some knock-on effects, such as the parser's borrowing of the VM's string table being explicit.
Other global variables in `parser.c` and `vm.c` are moved into either the `Parser` or `VM` structs.

The biggest impact this has, however, is...

### Garbage Collector split off from VM

All object memory management that the VM does instead lives in the `gc.c` and `gc.h` files.
The whole object chain and all garbage collection book-keeping lives in a dedicated `GC` struct.
This is required for tests that don't have a VM at their disposal.

GC roots are marked by setting the `markRoots` callback field of the GC struct:
the VM sets this to `vmMarkRoots`, while the compiler set it to `compilerMarkRoots`.

Clearing string weak references is done by setting the `fixWeak` callback of the `GC` struct to `tableRemoveWhite`.

There are points in the code where temporary object allocations need to be pushed to the VM stack in order to stop them from being freed too soon.
The `GC` struct has a temporary value stack for this purpose, and most places where this is needed will use it instead via the `pushTemp` and `popTemp` functions.

### Debug Flags as Arguments

The preprocessor defines starting with `DEBUG_` are converted into global variables that are set via command line arguments.
This makes them much easier to toggle on and off; their presence in `common.h` in baseline clox triggers full recompiles with every change.

## Licenses

This implementation of clox, like the code it was based on, is available under the MIT license, copyright Tung Nguyen; see [LICENSE.txt](/LICENSE.txt).

The original code of clox upon which this is based is available under the MIT license, copyright Robert Nystrom; see [LICENSE.lox.txt](/LICENSE.lox.txt).

Linenoise is available under the BSD-2-Clause license, copyright Salvatore Sanfilippo and Pieter Noordhuis; see [src/linenoise.h](/src/linenoise.h).

utest.h and ubench.h are public domain under the Unlicense, thanks to Neil Henning; see the respective files the `src` directory.

## See Also

- *Crafting Interpreters* by Robert Nystrom: <https://craftinginterpreters.com/>
- Big list of Lox implementations: <https://github.com/munificent/craftinginterpreters/wiki/Lox-Implementations>
- Linenoise, used by the REPL for line editing: <https://github.com/antirez/linenoise>
- utest.h, Neil Henning's single header unit testing framework for C: <https://www.duskborn.com/utest_h/>
- ubench.h, Neil Henning's single header benchmark framework for C: <https://github.com/sheredom/ubench.h>
