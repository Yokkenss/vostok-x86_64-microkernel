#ifndef VOSTOK_PRINT_H
#define VOSTOK_PRINT_H

#include <stdarg.h>

void kvprintf(const char *format, va_list arguments);
void kprintf(const char *format, ...);
void log_info(const char *format, ...);
void log_warn(const char *format, ...);
void log_error(const char *format, ...);

#endif
