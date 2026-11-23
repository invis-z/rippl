#pragma once

#include <stddef.h>

void *malloc(size_t size);
void free(void *block);
void *calloc(size_t count, size_t size);
void *realloc(void *block, size_t size);
