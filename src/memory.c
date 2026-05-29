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

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t index = 0; index < count; ++index) {
        if (a[index] != b[index]) return (int)a[index] - (int)b[index];
    }
    return 0;
}
