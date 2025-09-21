#include "panic.h"

#include "print.h"

#include <stdarg.h>

__attribute__((noreturn))
void halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

__attribute__((noreturn))
void panic(const char *format, ...)
{
    va_list arguments;

    kprintf("[panic] ");
    va_start(arguments, format);
    kvprintf(format, arguments);
    va_end(arguments);
    kprintf("\n");
    halt_forever();
}
