#ifndef ISKRA_DIAGNOSTIC_H
#define ISKRA_DIAGNOSTIC_H

#include "source.h"

#include <stddef.h>

void diagnostic_error(const struct source *source, size_t offset, size_t length,
                      size_t line, size_t column, const char *category,
                      const char *format, ...);

#endif
