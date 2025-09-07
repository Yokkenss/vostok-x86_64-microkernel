#include "memory.h"

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;

    for (size_t index = 0; index < count; ++index) {
        to[index] = from[index];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *bytes = destination;

    for (size_t index = 0; index < count; ++index) {
        bytes[index] = (unsigned char)value;
    }
    return destination;
}
