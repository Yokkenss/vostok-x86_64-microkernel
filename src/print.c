#include "print.h"

#include "serial.h"

#include <stdint.h>

static void print_unsigned(uint64_t value, unsigned int base, unsigned int width)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[16];
    unsigned int count = 0;

    do {
        buffer[count++] = digits[value % base];
        value /= base;
    } while (value != 0);

    while (count < width) {
        buffer[count++] = '0';
    }
    while (count != 0) {
        serial_putc(buffer[--count]);
    }
}

static void print_signed(int64_t value)
{
    uint64_t magnitude = (uint64_t)value;
    if (value < 0) {
        serial_putc('-');
        magnitude = (uint64_t)(-(value + 1)) + 1;
    }
    print_unsigned(magnitude, 10, 0);
}

void kvprintf(const char *format, va_list arguments)
{
    while (*format != '\0') {
        if (*format != '%') {
            serial_putc(*format++);
            continue;
        }

        ++format;
        if (*format == '\0') {
            serial_putc('%');
            break;
        }
        if (*format == '%') {
            serial_putc('%');
        } else if (*format == 'c') {
            serial_putc((char)va_arg(arguments, int));
        } else if (*format == 's') {
            const char *text = va_arg(arguments, const char *);
            serial_write(text == 0 ? "(null)" : text);
        } else if (*format == 'u') {
            print_unsigned(va_arg(arguments, unsigned int), 10, 0);
        } else if (*format == 'd') {
            print_signed(va_arg(arguments, int));
        } else if (*format == 'x') {
            print_unsigned(va_arg(arguments, unsigned int), 16, 0);
        } else if (*format == 'p') {
            serial_write("0x");
            print_unsigned((uintptr_t)va_arg(arguments, void *), 16, 16);
        } else if (*format == 'l' && format[1] == 'u') {
            print_unsigned(va_arg(arguments, unsigned long), 10, 0);
            ++format;
        } else if (*format == 'l' && format[1] == 'd') {
            print_signed(va_arg(arguments, long));
            ++format;
        } else if (*format == 'l' && format[1] == 'x') {
            print_unsigned(va_arg(arguments, unsigned long), 16, 16);
            ++format;
        } else {
            serial_putc('%');
            serial_putc(*format);
        }
        ++format;
    }
}

void kprintf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    kvprintf(format, arguments);
    va_end(arguments);
}

static void log_message(const char *level, const char *format, va_list arguments)
{
    kprintf("[%s] ", level);
    kvprintf(format, arguments);
    serial_putc('\n');
}

#define DEFINE_LOG_FUNCTION(name, level)                 \
    void name(const char *format, ...)                   \
    {                                                    \
        va_list arguments;                               \
        va_start(arguments, format);                     \
        log_message(level, format, arguments);           \
        va_end(arguments);                               \
    }

DEFINE_LOG_FUNCTION(log_info, "info")
DEFINE_LOG_FUNCTION(log_warn, "warn")
DEFINE_LOG_FUNCTION(log_error, "error")
