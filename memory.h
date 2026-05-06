#pragma once

#include "types.h"

typedef unsigned long size_t;

void *memcpy(void *dest, const void *src, size_t n);

void *memset(void *dest, int c, size_t n);

void *memmove(void *dest, const void *src, size_t n);
