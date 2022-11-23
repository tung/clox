#pragma once
#ifndef clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#ifndef NAN_BOXING
#define NAN_BOXING 1
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
