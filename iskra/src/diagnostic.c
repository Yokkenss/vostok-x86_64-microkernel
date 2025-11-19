#include "diagnostic.h"

#include <stdarg.h>
#include <stdio.h>

void diagnostic_error(const struct source *source, size_t offset, size_t length,
                      size_t line, size_t column, const char *category,
                      const char *format, ...)
{
    size_t line_start = offset;
    size_t line_end = offset;
    va_list arguments;

    while (line_start > 0 && source->text[line_start - 1] != '\n') {
        --line_start;
    }
    while (line_end < source->length && source->text[line_end] != '\n') {
        ++line_end;
    }

    fprintf(stderr, "%s:%zu:%zu: error[%s]: ", source->path, line, column,
            category);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);

    fprintf(stderr, "  %.*s\n  ", (int)(line_end - line_start),
            source->text + line_start);
    for (size_t index = line_start; index < offset; ++index) {
        fputc(source->text[index] == '\t' ? '\t' : ' ', stderr);
    }
    if (length == 0) {
        length = 1;
    }
    for (size_t index = 0; index < length && offset + index <= line_end; ++index) {
        fputc(index == 0 ? '^' : '~', stderr);
    }
    fputc('\n', stderr);
}
