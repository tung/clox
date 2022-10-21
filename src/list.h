#pragma once
#ifndef list_h
#define list_h

// Count number of variadic macro arguments passed.
// Adapted from https://stackoverflow.com/a/2124433
#define NUM_VA_ARGS(type, ...) \
  (sizeof((type[]){ __VA_ARGS__ }) / sizeof(type))

// A size-and-list-pointer pair for literal lists, e.g.
// LIST(int, 4, 5) -> 2, (int[]){ 4, 5 }
#define LIST(type, ...) \
  NUM_VA_ARGS(type, __VA_ARGS__), (type[]) { \
    __VA_ARGS__ \
  }

#endif
