#ifndef ISKRA_SOURCE_H
#define ISKRA_SOURCE_H

#include <stddef.h>

struct source {
    const char *path;
    const char *text;
    size_t length;
};

#endif
