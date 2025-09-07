#ifndef VOSTOK_MEMORY_H
#define VOSTOK_MEMORY_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);

#endif
